



/*
 * makesmart_lock.ino
 * 
 *  Created on: 2021-02-05
 *      Author: cooper @ makesmart.net
 *      Thank you for this great library!
 *      
 * This example shows how to:
 * 1. define a lock accessory and its characteristics in my_accessory.c
 * 2. get the target-state sent from iOS Home APP.
 * 3. report the current-state value to HomeKit.
 * 
 * you can use both:
 *    void open_lock(){}
 * and
 *    void close_lock(){}
 *    
 * at the end of this file to let the lock-mechanism do whatever you want. 
 * 
 * 
 * Pairing Code: 123-45-678
 * 
 * 
 * You should:
 * 1. read and use the Example01_TemperatureSensor with detailed comments
 *    to know the basic concept and usage of this library before other examples。
 * 2. erase the full flash or call homekit_storage_reset() in setup()
 *    to remove the previous HomeKit pairing storage and
 *    enable the pairing with the new accessory of this new HomeKit example.
 * 
 */
 #include <Servo.h>
 #include <ESP8266HTTPClient.h>
 #include <WiFiClientSecure.h>
#include <Arduino.h>
#include <arduino_homekit_server.h>
#include "wifi_info.h"
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#define WIFI_SSID "JIitwisut 2.4G"
#define WIFI_PASSWORD "0949479336za"
const char* webhook="https://discord.com/api/webhooks/1313779726233370694/CXwvgcLQRxGZCdDmoByHfJyNutWgdNmWjiD2ybBV3VqnzVPLeXdYn8IC7gyS5vFvVxzn";
#define irint 4   // เซ็นเซอร์เข้า
#define irout 14  // เซ็นเซอร์ออก
Servo myservo;
int counter_in = 0;
int counter_out = 0;
int lastStatusIrIn = -1;
int lastStatusIrOut = -1;
bool isWalkIn = false;
bool isWalkOut = false;
long walkOutTime;
long walkInTime;
bool sensoron=false;
// กำหนดเวลา 2 วินาทีระหว่างการตรวจจับครั้งล่าสุด
long debounceTime = 2000;  // 2000 ms = 2 วินาที
static bool doorOpened = false;
WiFiClientSecure secured_client;
HTTPClient http;
ESP8266WebServer server(80);
int count = 0;
void setup() {
  Serial.begin(115200);
  wifi_connect();
 // homekit_storage_reset();
  my_homekit_setup();
  secured_client.setInsecure();
  pinMode(irint, INPUT);  // ตั้งค่าพินเซ็นเซอร์เข้า
  pinMode(irout, INPUT);  // ตั้งค่าพินเซ็นเซอร์ออก
   myservo.attach(2);
   myservo.write(0);
     server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain", "ESP8266 Web Server is running!");
  });
   server.on("/lock", []() {
    close_lock();

  });
  server.on("/unlock",[](){
    open_lock();  
  });
   server.begin();
}

void loop() {
  server.handleClient();
if(doorOpened){
  
  checkWalkIn();
  checkWalkOut();



     if (isWalkIn && !isWalkOut) {

      updateMessage(true);      // อัพเดตข้อความสำหรับการเดินเข้า

  } else if (!isWalkIn && isWalkOut) {

    updateMessage(false);  // อัพเดตข้อความสำหรับการเดินออก
  }
  delay(100);
}
  
  
  arduino_homekit_loop();  // เรียกใช้ฟังก์ชันนี้แทน my_homekit_loop()
 // delay(10);


/*
  static int lastValue = 1; // เก็บสถานะก่อนหน้าของเซ็นเซอร์

  if (doorOpened) {
    int value = digitalRead(irsensor);

    // ตรวจสอบว่ามีการเปลี่ยนสถานะจาก HIGH เป็น LOW
    if (value == 0 && lastValue == 1) {
      count += 1;
  String message = "The door has been unlocked. People walked: " + String(count);  // สร้างข้อความ
      sentmessage(message);  // ส่งข้อความไปยัง Discord Webhook
    }

    lastValue = value; // อัปเดตสถานะล่าสุดของเซ็นเซอร์
  }
  */
}

//==============================
// HomeKit setup and loop
//==============================

// access lock-mechanism HomeKit characteristics defined in my_accessory.c
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t cha_lock_current_state;
extern "C" homekit_characteristic_t cha_lock_target_state;

static uint32_t next_heap_millis = 0;


// called when the lock-mechanism target-set is changed by iOS Home APP
void set_lock(const homekit_value_t value) {
  
  uint8_t state = value.int_value;
  cha_lock_current_state.value.int_value = state;
  
  if(state == 0){
    // lock-mechanism was unsecured by iOS Home APP
    open_lock();
    counter_in=0;

  }
  if(state == 1){
    // lock-mechanism was secured by iOS Home APP
    close_lock();
  }
  
  //report the lock-mechanism current-sate to HomeKit
  homekit_characteristic_notify(&cha_lock_current_state, cha_lock_current_state.value);
  
}

void my_homekit_setup() {
  
  cha_lock_target_state.setter = set_lock;
  arduino_homekit_setup(&config);

  
}

void sentmessage(String message) {
 
  http.begin(secured_client, webhook);  // ใช้ WiFiClientSecure เพื่อเชื่อมต่อ HTTPS
  http.addHeader("Content-Type", "application/json");
   String jsonMessage = "{\"content\":\"" + message + "\"}";

  // ส่ง POST Request ไปยัง Webhook
  int httpResponseCode = http.POST(jsonMessage);
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response Code: ");
    Serial.println(httpResponseCode);  // แสดงรหัสสถานะ HTTP
    Serial.println("Message sent to Discord Webhook");
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);  // แสดงข้อผิดพลาด
  }

  // ปิดการเชื่อมต่อ
  http.end();
}




/* use this functions to let your lock mechanism do whatever yoi want */
void open_lock() {

   counter_in= 0;
   counter_out=0;
   myservo.write(160);
  Serial.println("unsecure");
  String message="ประตูเปิดแล้วค้าบบบบ";
  sentmessage(message);
  doorOpened = true; // ตั้งสถานะว่าประตูเปิดแล้ว
  sensoron=true;
}
void close_lock() {
  Serial.println("secure");
  doorOpened = false; // ตั้งสถานะว่าประตูปิดแล้ว
   myservo.write(0);
  String message="ประตูปิดแล้วจร้าาาาาาา";
  sentmessage(message);
}
void checkWalkIn() {
   //เก็บสถานะปัจจุบัน
  int currentStatus = digitalRead(irint);
// ตรวจสอบการเปลี่ยนแปลงจากสถานะเดิม
  if (lastStatusIrIn != currentStatus) {
    lastStatusIrIn = currentStatus;

    if (currentStatus == LOW && !isWalkIn && digitalRead(irout) == HIGH) {  // เมื่อเซ็นเซอร์เข้า (irint) ตรวจพบ
      if (millis() - walkInTime > debounceTime) { // ตรวจสอบระยะเวลา
        walkInTime = millis();  // บันทึกเวลาเดินเข้า
        isWalkIn = true;  // ตั้งค่าสถานะการเดินเข้า
      }
    }
  }
}

void checkWalkOut() {
  //เก็บสถานะปัจจุบัน
  int currentStatus = digitalRead(irout);
// ตรวจสอบการเปลี่ยนแปลงจากสถานะเดิม
  if (lastStatusIrOut != currentStatus) {
    lastStatusIrOut = currentStatus;

    if (currentStatus == LOW && !isWalkOut && digitalRead(irint) == HIGH) {  // เมื่อเซ็นเซอร์ออก (irout) ตรวจพบ
      if (millis() - walkOutTime > debounceTime) { // ตรวจสอบระยะเวลา
        walkOutTime = millis();  // บันทึกเวลาเดินออก
        isWalkOut = true;  // ตั้งค่าสถานะการเดินออก
      }
    }
  }
}

void updateMessage(bool isIn) {
  if (isIn) {
    String message;
    counter_in++;  // เพิ่มจำนวนคนเข้า
    message="คนเดินเข้า"+String(counter_in);
    sentmessage(message);
    isWalkIn = false;  // รีเซ็ตสถานะการเดินเข้า
  } else {
     String message;
    counter_out++;  // เพิ่มจำนวนคนออก
    message="คนเดินออก"+String(counter_out);
    sentmessage(message);
    isWalkOut = false;  // รีเซ็ตสถานะการเดินออก
  }
}
