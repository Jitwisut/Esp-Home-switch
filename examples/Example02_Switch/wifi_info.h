/*
 * wifi_info.h
 *
 *  Created on: 2020-05-15
 *      Author: Mixiaoxiao (Wang Bin)
 */

#ifndef WIFI_INFO_H_
#define WIFI_INFO_H_

// Copy this file to wifi_info.local.h and edit that local copy. The sketch
// prefers wifi_info.local.h when it exists, so credentials stay out of Git.
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

// HiveMQ Cloud connection (MQTT over TLS/TCP, not WebSocket).
// Create these credentials in HiveMQ Cloud > Access Management and replace
// both placeholders before flashing. HiveMQ Cloud does not allow anonymous
// connections on the normal cluster endpoint.
const char *mqtt_host = "7cce5e05b0f24f18940c600d2e71e09d.s1.eu.hivemq.cloud";
const uint16_t mqtt_port = 8883;
const char *mqtt_username = "YOUR_HIVEMQ_USERNAME";
const char *mqtt_password = "YOUR_HIVEMQ_PASSWORD";

// The external app publishes ON/OFF here. The device publishes retained state
// and availability on the other two topics so every client sees the same state.
const char *mqtt_command_topic = "home/esp01/switch/set";
const char *mqtt_state_topic = "home/esp01/switch/state";
const char *mqtt_availability_topic = "home/esp01/switch/availability";

#endif /* WIFI_INFO_H_ */
