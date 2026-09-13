// ESP-01 relay module: GPIO0 is the usual relay control pin.
// RELAY_ACTIVE_LOW selects the electrical level that energizes the relay.
// Keep the relay load on COM/NO when OFF should mean load off.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <arduino_homekit_server.h>
#if __has_include("wifi_info.local.h")
#include "wifi_info.local.h"  // Per-device credentials; kept out of Git.
#else
#include "wifi_info.h"        // Safe template for a fresh checkout.
#endif

extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t cha_switch_on;

#define RELAY_GPIO 0
// Set to 1 when your relay energizes on LOW; set to 0 when it energizes on HIGH.
#define RELAY_ACTIVE_LOW 0

static const uint8_t PIN_SWITCH = RELAY_GPIO;
static const uint8_t RELAY_ON_LEVEL = RELAY_ACTIVE_LOW ? LOW : HIGH;
static const uint8_t RELAY_OFF_LEVEL = RELAY_ACTIVE_LOW ? HIGH : LOW;
static const uint32_t WIFI_RETRY_MS = 10000;
static const uint32_t WIFI_REBOOT_MS = 120000;
static const uint32_t MQTT_RETRY_MS = 5000;
static const uint32_t LOG_INTERVAL_MS = 30000;

static BearSSL::WiFiClientSecure mqtt_tls_client;
static PubSubClient mqtt_client(mqtt_tls_client);
static WiFiEventHandler disconnected_handler;
static bool services_started = false;
static bool wifi_lost = false;
static uint32_t wifi_lost_at = 0;
static uint32_t last_wifi_attempt = 0;
static uint32_t last_mqtt_attempt = 0;
static uint32_t last_log_at = 0;
static bool mqtt_credentials_warning_shown = false;

static void mqtt_publish_state();

static void set_switch(bool on, bool notify_homekit) {
  const bool changed = cha_switch_on.value.bool_value != on;
  digitalWrite(PIN_SWITCH, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
  cha_switch_on.value.bool_value = on;

  if (changed && notify_homekit && services_started) {
    homekit_characteristic_notify(&cha_switch_on, cha_switch_on.value);
  }
  // If MQTT is connected this publishes the same logical state that HomeKit
  // sees. The retained message also becomes the source of truth for clients
  // that connect later.
  mqtt_publish_state();
  Serial.printf("Switch: %s\n", on ? "ON" : "OFF");
}

// HomeKit writes arrive here. The HomeKit server handles its own notification;
// we only update the relay and publish the matching MQTT state.
void cha_switch_on_setter(const homekit_value_t value) {
  set_switch(value.bool_value, false);
}

static bool mqtt_command_is(const char *command, const char *expected) {
  return strcmp(command, expected) == 0;
}

static bool parse_mqtt_command(byte *payload, unsigned int length, bool &on) {
  char command[16];
  size_t count = length;
  if (count >= sizeof(command)) count = sizeof(command) - 1;
  memcpy(command, payload, count);
  command[count] = '\0';

  // Trim the whitespace commonly added by command-line MQTT clients.
  size_t first = 0;
  while (first < count && (command[first] == ' ' || command[first] == '\t' ||
                           command[first] == '\r' || command[first] == '\n')) {
    ++first;
  }
  while (count > first && (command[count - 1] == ' ' ||
                           command[count - 1] == '\t' ||
                           command[count - 1] == '\r' ||
                           command[count - 1] == '\n')) {
    --count;
  }
  const size_t normalized_length = count - first;
  if (normalized_length >= sizeof(command)) return false;
  if (first > 0) memmove(command, command + first, normalized_length);
  command[normalized_length] = '\0';
  for (size_t i = 0; i < normalized_length; ++i) {
    if (command[i] >= 'a' && command[i] <= 'z') command[i] -= ('a' - 'A');
  }

  if (mqtt_command_is(command, "ON") || mqtt_command_is(command, "1") ||
      mqtt_command_is(command, "TRUE")) {
    on = true;
    return true;
  }
  if (mqtt_command_is(command, "OFF") || mqtt_command_is(command, "0") ||
      mqtt_command_is(command, "FALSE")) {
    on = false;
    return true;
  }
  return false;
}

static void mqtt_callback(char *topic, byte *payload, unsigned int length) {
  if (strcmp(topic, mqtt_command_topic) != 0) return;

  bool on = false;
  if (!parse_mqtt_command(payload, length, on)) {
    Serial.println(F("MQTT: command must be ON/OFF (or 1/0)"));
    return;
  }
  set_switch(on, true);
}

static void mqtt_publish_state() {
  if (!mqtt_client.connected()) return;
  const char *state = cha_switch_on.value.bool_value ? "ON" : "OFF";
  if (!mqtt_client.publish(mqtt_state_topic, state, true)) {
    Serial.println(F("MQTT: state publish failed"));
  }
}

static void mqtt_disconnect_cleanly() {
  if (!mqtt_client.connected()) return;
  mqtt_client.publish(mqtt_availability_topic, "offline", true);
  mqtt_client.disconnect();
}

static void mqtt_setup() {
  // TLS is still used on port 8883. setInsecure() skips CA validation so the
  // sketch can work without embedding a certificate; keep the broker
  // credentials private and use a trusted LAN/network. A CA can be installed
  // later with mqtt_tls_client.setTrustAnchors() for strict verification.
  mqtt_tls_client.setInsecure();
  mqtt_client.setServer(mqtt_host, mqtt_port);
  mqtt_client.setCallback(mqtt_callback);
  mqtt_client.setKeepAlive(45);
  mqtt_client.setSocketTimeout(8);
  mqtt_client.setBufferSize(256);
}

static void mqtt_maintain() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (mqtt_username == nullptr || mqtt_password == nullptr ||
      mqtt_username[0] == '\0' || mqtt_password[0] == '\0' ||
      strcmp(mqtt_username, "YOUR_HIVEMQ_USERNAME") == 0 ||
      strcmp(mqtt_password, "YOUR_HIVEMQ_PASSWORD") == 0) {
    if (!mqtt_credentials_warning_shown) {
      mqtt_credentials_warning_shown = true;
      Serial.println(F("MQTT: set mqtt_username and mqtt_password in wifi_info.h"));
    }
    return;
  }

  if (!mqtt_client.connected()) {
    const uint32_t now = millis();
    if (now - last_mqtt_attempt < MQTT_RETRY_MS) return;
    last_mqtt_attempt = now;

    char client_id[32];
    snprintf(client_id, sizeof(client_id), "esp01-switch-%06X",
             ESP.getChipId());
    Serial.printf("MQTT: connecting to %s:%u\n", mqtt_host, mqtt_port);
    const bool connected = mqtt_client.connect(
        client_id, mqtt_username, mqtt_password, mqtt_availability_topic, 0,
        true, "offline");
    if (!connected) {
      Serial.printf("MQTT: connect failed, state=%d\n", mqtt_client.state());
      return;
    }

    mqtt_client.publish(mqtt_availability_topic, "online", true);
    if (!mqtt_client.subscribe(mqtt_command_topic)) {
      Serial.println(F("MQTT: command subscribe failed"));
    }
    mqtt_publish_state();
    Serial.println(F("MQTT: connected; state published and command subscribed"));
  }

  mqtt_client.loop();
}

static void start_services() {
  cha_switch_on.setter = cha_switch_on_setter;
  mqtt_setup();
  wifi_lost = false;
  wifi_lost_at = 0;
  services_started = true;
  arduino_homekit_setup(&config);
  Serial.printf("WiFi connected: %s; MQTT TLS %s:%u\n",
                WiFi.localIP().toString().c_str(), mqtt_host, mqtt_port);
}

static void maintain_wifi() {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!services_started) {
      start_services();
    } else if (wifi_lost) {
      // Rebuild HomeKit TCP sessions and mDNS after a real disconnect.
      // Pairing lives in flash and is intentionally left untouched.
      Serial.println(F("WiFi recovered; restarting to restore HomeKit"));
      mqtt_disconnect_cleanly();
      delay(100);
      ESP.restart();
    }
    return;
  }

  if (!wifi_lost) {
    wifi_lost = true;
    wifi_lost_at = now;
    Serial.println(F("WiFi disconnected"));
  }
  if (now - last_wifi_attempt >= WIFI_RETRY_MS) {
    last_wifi_attempt = now;
    WiFi.reconnect();
    Serial.println(F("Retrying WiFi"));
  }
  // A stuck WiFi stack also gets a fresh start, even if it never gets an IP.
  if (services_started && now - wifi_lost_at >= WIFI_REBOOT_MS) {
    Serial.println(F("WiFi recovery timed out; restarting"));
    mqtt_disconnect_cleanly();
    delay(100);
    ESP.restart();
  }
}

void setup() {
  digitalWrite(PIN_SWITCH, RELAY_OFF_LEVEL);  // Set latch before enabling pin.
  pinMode(PIN_SWITCH, OUTPUT);                // Fail-safe OFF after boot.
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  disconnected_handler = WiFi.onStationModeDisconnected(
      [](const WiFiEventStationModeDisconnected &) {
        if (services_started) {
          wifi_lost = true;
          wifi_lost_at = millis();
        }
      });
  WiFi.begin(ssid, password);
  last_wifi_attempt = millis();
  Serial.println(F("Connecting to WiFi"));
}

void loop() {
  maintain_wifi();
  if (services_started && WiFi.status() == WL_CONNECTED) {
    arduino_homekit_loop();
    mqtt_maintain();
  }
  const uint32_t now = millis();
  if (now - last_log_at >= LOG_INTERVAL_MS) {
    last_log_at = now;
    Serial.printf("WiFi: %d, MQTT: %s, heap: %u, HomeKit clients: %d\n",
                  WiFi.status(), mqtt_client.connected() ? "connected" : "offline",
                  ESP.getFreeHeap(), arduino_homekit_connected_clients_count());
  }
  delay(5);
}
