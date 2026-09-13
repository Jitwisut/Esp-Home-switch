# ESP-01 HomeKit switch with MQTT

ตัวอย่างนี้ทำให้รีเลย์ตัวเดียวควบคุมได้จาก Apple Home และจากแอปอื่นผ่าน MQTT
โดยใช้ HiveMQ Cloud เป็น broker สถานะจะถูกส่งกลับเป็น retained message เพื่อให้
แอปที่เพิ่งเชื่อมต่อเห็นสถานะปัจจุบันทันที

## ติดตั้งไลบรารี

สเก็ตช์ต้องใช้ไลบรารีสองตัวนี้ใน Arduino IDE:

1. ติดตั้งโฟลเดอร์ repository นี้เป็นไลบรารี โดยคัดลอกทั้งโฟลเดอร์
   `Arduino-HomeKit-ESP8266` ไปไว้ที่
   `C:\Users\<ชื่อผู้ใช้>\Documents\Arduino\libraries\HomeKit-ESP8266` แล้วเปิด Arduino IDE ใหม่
2. ติดตั้ง **PubSubClient** จาก **Tools > Manage Libraries...** แล้วค้นหา
   `PubSubClient` (เลือกเวอร์ชัน 2.8.0 หรือใหม่กว่า)
3. เปิดไฟล์ `Example02_Switch.ino` ในโฟลเดอร์นี้ หรือเปิดจาก
   **File > Examples > HomeKit-ESP8266 > Example02_Switch**

หากขึ้น `arduino_homekit_server.h: No such file or directory` แสดงว่ายังไม่ได้
ติดตั้ง repository เป็นไลบรารีตามข้อ 1 หรือ Arduino IDE ยังไม่ได้เปิดใหม่

## ตั้งค่า Wi-Fi และ HiveMQ Cloud

คัดลอก `wifi_info.h` เป็น `wifi_info.local.h` ในโฟลเดอร์เดียวกัน แล้วแก้ไฟล์
`wifi_info.local.h` ก่อนอัปโหลด ไฟล์นี้ถูก ignore ใน Git เพื่อไม่ให้รหัสผ่านถูก
ส่งขึ้น repository:

```cpp
const char *ssid = "ชื่อ WiFi 2.4GHz";
const char *password = "รหัสผ่าน WiFi";

const char *mqtt_host =
    "7cce5e05b0f24f18940c600d2e71e09d.s1.eu.hivemq.cloud";
const uint16_t mqtt_port = 8883;       // MQTT over TLS/TCP สำหรับ ESP8266
const char *mqtt_username = "ชื่อผู้ใช้ HiveMQ";
const char *mqtt_password = "รหัสผ่าน HiveMQ";
```

สร้าง username/password ใน HiveMQ Cloud ที่ **Access Management** แล้วใส่ใน
สองบรรทัดสุดท้าย ห้ามใช้ค่าตัวอย่าง `YOUR_HIVEMQ_USERNAME` และ
`YOUR_HIVEMQ_PASSWORD` ตอนใช้งานจริง พอร์ต `8883` เป็น MQTT over TLS/TCP ที่
ใช้กับ PubSubClient ในสเก็ตช์นี้ ส่วน `8884/mqtt` เป็น WebSocket สำหรับแอปที่
ทำงานในเว็บเบราว์เซอร์

สเก็ตช์เปิด TLS แต่เรียก `setInsecure()` เพื่อไม่ต้องฝัง CA certificate ใน
ESP-01 จึงเข้ารหัสข้อมูลระหว่างทาง แต่ไม่ได้ตรวจสอบใบรับรอง broker หากต้องการ
ตรวจสอบ CA แบบเข้มงวด ให้เปลี่ยนเป็น `setTrustAnchors()` และใส่ root CA ของ
HiveMQ Cloud ในโค้ดก่อนนำไปใช้บนเครือข่ายที่ไม่ไว้วางใจ

## หัวข้อ MQTT และ payload

ค่าเริ่มต้นใน `wifi_info.h`:

| หน้าที่ | Topic | Payload |
| --- | --- | --- |
| สั่งงาน | `home/esp01/switch/set` | `ON`, `OFF`, `1`, `0`, `TRUE`, `FALSE` |
| สถานะปัจจุบัน | `home/esp01/switch/state` | `ON` หรือ `OFF` (retained) |
| สถานะการเชื่อมต่อ | `home/esp01/switch/availability` | `online` หรือ `offline` (retained) |

พฤติกรรมการซิงก์:

- เลื่อนสวิตช์ใน Apple Home: รีเลย์เปลี่ยน, HomeKit ใช้ค่าใหม่นั้น และบอร์ด
  publish ค่าเดียวกันไปที่ `state`
- publish คำสั่งไปที่ `set`: รีเลย์เปลี่ยนและ Home app ได้รับ notification ทันที
- เมื่อ MQTT ต่อสำเร็จ บอร์ด publish `online` และ publish state ปัจจุบันซ้ำ
- ถ้าไฟดับหรือ Wi-Fi หลุด broker จะเปลี่ยน availability เป็น `offline` จาก LWT

## ทดสอบจากคอมพิวเตอร์หรือแอปอื่น

ใช้ MQTT client ใดก็ได้ที่รองรับ TLS และ username/password ตัวอย่างด้านล่างใช้
HiveMQ MQTT CLI (เปลี่ยนค่าตัวพิมพ์ใหญ่เป็นข้อมูลของคุณ):

```text
mqtt sub -h 7cce5e05b0f24f18940c600d2e71e09d.s1.eu.hivemq.cloud -p 8883 --secure -u YOUR_HIVEMQ_USERNAME -pw YOUR_HIVEMQ_PASSWORD -t "home/esp01/switch/#" -v

mqtt pub -h 7cce5e05b0f24f18940c600d2e71e09d.s1.eu.hivemq.cloud -p 8883 --secure -u YOUR_HIVEMQ_USERNAME -pw YOUR_HIVEMQ_PASSWORD -t "home/esp01/switch/set" -m ON

mqtt pub -h 7cce5e05b0f24f18940c600d2e71e09d.s1.eu.hivemq.cloud -p 8883 --secure -u YOUR_HIVEMQ_USERNAME -pw YOUR_HIVEMQ_PASSWORD -t "home/esp01/switch/set" -m OFF
```

แอปเว็บให้เชื่อมต่อด้วย WebSocket URL
`wss://7cce5e05b0f24f18940c600d2e71e09d.s1.eu.hivemq.cloud:8884/mqtt` แล้ว
subscribe `home/esp01/switch/state` และ publish `ON`/`OFF` ไปที่
`home/esp01/switch/set` โดยใช้ credentials เดียวกัน

ถ้าใช้ Mosquitto ให้ใช้ `mosquitto_pub`/`mosquitto_sub` กับ `-h`, `-p 8883`,
`-u`, `-P`, `--tls-version tlsv1.2` และ topic เดียวกัน (ติดตั้ง CA หรือใช้
ตัวเลือก TLS ตามนโยบายของเครื่องคุณ)

## ตั้งค่ารีเลย์ ESP-01

บอร์ดรีเลย์ ESP-01 ที่พบบ่อยใช้ GPIO0:

```cpp
#define RELAY_GPIO 0
#define RELAY_ACTIVE_LOW 0
```

`RELAY_ACTIVE_LOW` ต้องตรงกับบอร์ดของคุณ: ใส่ `1` ถ้ารีเลย์ทำงานเมื่อขาเป็น
`LOW` และใส่ `0` ถ้าทำงานเมื่อขาเป็น `HIGH` สเก็ตช์ตั้งขาเป็น OFF ก่อนเปิดใช้งาน
GPIO ทุกครั้ง หากรีเลย์กลับด้านให้เปลี่ยนค่านี้แล้วอัปโหลดใหม่

สำหรับความหมายปกติ `OFF = ไฟดับ`, `ON = ไฟติด` ให้ต่อสายไฟเข้า `COM` และต่อ
โหลดออกทาง `NO`; `NC` จะปิดวงจรตอนรีเลย์ยังไม่ทำงานจึงทำให้ความหมายกลับด้าน
ห้ามต่อแรงดันไฟบ้านเข้าขา ESP-01 และห้ามจ่ายกระแสคอยล์รีเลย์จาก GPIO โดยตรง

## Apple Home และการแก้ Wi-Fi หลุด

HomeKit pairing code ของตัวอย่างคือ `111-11-111` (ดูใน `my_accessory.c`)
หลังอัปโหลดให้เปิด Serial Monitor ที่ 115200 baud รอข้อความ IP และข้อความ
`MQTT: connected` ก่อนทดสอบจากแอป

โค้ดปิด Wi-Fi sleep, retry ทุก 10 วินาที และรีสตาร์ตเมื่อ Wi-Fi กลับมาหลังการ
หลุดจริงเพื่อสร้าง HomeKit listener/mDNS ใหม่ หากค้างโดยไม่มี IP เกิน 2 นาทีจะ
รีสตาร์ตอีกครั้ง pairing ในแฟลชยังอยู่ แต่รีเลย์จะเริ่มที่ OFF หลังบูตเพื่อความ
ปลอดภัย ตรวจสอบแหล่งจ่าย 3.3 V และสัญญาณ Wi-Fi หากหลุดซ้ำ

อย่าเผยแพร่ `wifi_info.local.h` เพราะมี Wi-Fi และ HiveMQ credentials ของคุณอยู่
