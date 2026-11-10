# Lab 6 – HD44780 LCD Component (ESP-IDF v5.x, ESP32)

## 1) วิธี Build / Flash ใน VS Code (ESP-IDF Extension)
เปิดโฟลเดอร์ `lab6_lcd` → กด F1 → `ESP-IDF: Set Espressif Device Target` เลือก **esp32** →
เลือกภารกิจ (เลือกวิธีใดวิธีหนึ่ง):

* **วิธีง่าย:** `ESP-IDF: SDK Configuration Editor (menuconfig)` → เมนู **Lab6 LCD HD44780** → Mission to build
* **วิธีเทอร์มินัล ESP-IDF** (เปลี่ยนภารกิจแล้วให้ลบ `sdkconfig` ก่อน):
  ```
  rm -f sdkconfig                      # Windows: del sdkconfig
  idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.m1" set-target esp32     # ภารกิจ 1
  idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.m2" set-target esp32     # ภารกิจ 2
  idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.m3" set-target esp32     # ภารกิจ 3 (อ่าน ADC)
  idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.m3_ramp" set-target esp32 # ภารกิจ 3 (ramp ไม่ต้องต่อ ADC)
  idf.py build flash monitor
  ```

## 2) เลือกชุดขา (Pin map) — ต้องตรงกับที่ต่อจริง
menuconfig → Lab6 LCD HD44780 → **Pin map** (ค่าเริ่มต้น = DIAGRAM ตามไฟล์ lcd.svg)

| | RS | E | D4 | D5 | D6 | D7 |
|---|---|---|---|---|---|---|
| **DIAGRAM** (ค่าเริ่มต้น) | 16 | 17 | 23 | 25 | 26 | 27 |
| SHEET (ตัวอย่างในใบงาน) | 4 | 5 | 16 | 17 | 18 | 19 |

ตารางด้านล่างเป็นชุด SHEET ถ้าใช้ DIAGRAM ให้ดูตารางในคำตอบแชท

## ตารางต่อสาย (ชุด SHEET) (ใช้เลข GPIO ตามป้ายบนหัว ESP32 ของบอร์ด)
LCD 16x2 ขา VSS=GND, VDD=5V, **RW=GND**, A(LED+)=5V (ผ่านตัวต้านทาน ~220Ω ถ้าโมดูลไม่มี), K(LED-)=GND,
V0(VO)=กลางของ VR ปรับคอนทราสต์ (หรือ GND ผ่าน ~1k ถ้าเห็นตัวอักษรชัดแล้ว)

**LCD1 (โหมด 4 บิต – ใช้ทุกภารกิจ)**

| ขา LCD | GPIO |
|---|---|
| RS | 4 |
| E | 5 |
| D4 | 16 |
| D5 | 17 |
| D6 | 18 |
| D7 | 19 |
| D0–D3 | ไม่ต่อ |

**LCD2 (โหมด 8 บิต – ภารกิจ 2 เท่านั้น ต้องมีจอที่สอง)**

| ขา LCD | GPIO |
|---|---|
| RS | 21 |
| E | 22 |
| D0 | 13 |
| D1 | 14 |
| D2 | 23 |
| D3 | 25 |
| D4 | 26 |
| D5 | 27 |
| D6 | 32 |
| D7 | 33 |

(เลี่ยง GPIO 0, 2, 12, 15 ที่เป็น strapping pin และ 34–39 ที่เป็น input only)

**ภารกิจ 3 – อินพุต ADC:** ต่อสัญญาณ 0–3.3V เข้า **GPIO34** (ADC1_CH6) เช่นกลางของ VR 10k ที่ต่อ 3.3V–GND
(ห้ามเกิน 3.3V ที่ขา ESP32) ถ้าเปลี่ยนขาให้ใช้ ADC1 เท่านั้น (GPIO32–39) ผ่าน menuconfig
ถ้ายังไม่ได้ต่อ ADC ให้ใช้ `sdkconfig.defaults.m3_ramp` ได้เลย

## 3) ผลลัพธ์ที่ควรเห็น
* **M1:** บรรทัด 1 "M1 Timing Test", บรรทัด 2 เขียนซ้ำทุก 50 ms จับสัญญาณ E, RS ด้วย Logic Analyzer
  แล้ววัด tPWE (ค่าที่ตั้ง ≈ 1–2 µs ≥ 450 ns) และ tcycE (ระหว่างนิบเบิลต่อกัน ≈ 52 µs ≥ 1000 ns) บันทึกค่าที่วัดจริง
* **M2:** LCD1 `Countdown (s)` / `T- 00:59` ... นับถอยหลัง 60→0 วนซ้ำ,
  LCD2 `Up:12s OK` / `Heap:xxxxxx B` อัปเดตทุก 0.5 วินาที
* **M3:** บรรทัด 1 `Level:  50%` บรรทัด 2 แท่งบาร์ 0–100% ตามค่า ADC (หรือ ramp)

## 4) สิ่งที่แก้/ปรับจากโค้ดตัวอย่างในใบงาน
1. เพิ่มคำสั่ง Entry Mode `0x06` และ Display OFF `0x08` ตามลำดับ init มาตรฐาน (ตัวอย่างข้ามไป)
2. รอ 5 ms ด้วย `esp_rom_delay_us(5000)` แทน `vTaskDelay(pdMS_TO_TICKS(5))` เพราะที่ tick 100 Hz ค่านี้ = 0 tick (ไม่รอจริง)
   และ clear/home รอ 2 ms ด้วย busy-wait เช่นกัน
3. ตั้งระดับ RS/EN เริ่มต้น, ตรวจค่า return ของ `gpio_config`, ตรวจ `max_cols`/ขอบเขตใน `lcd_draw_bargraph`
4. เพิ่มฟังก์ชัน `lcd_write_string_at()` ตั้งเคอร์เซอร์+เขียนข้อความใน mutex เดียว (กัน race ระหว่างทาสก์ ที่ `lcd_set_cursor` + `lcd_write_string` แยกกันอาจโดนแทรก)
5. เขียนข้อความทุกบรรทัดเต็ม 16 ตัวอักษร เพื่อไม่ให้เหลือตัวเก่าค้าง

## 5) ตอบคำถาม Post-Lab (สั้น)
1. Opaque handle: ซ่อน struct ไว้ใน .c จึงเปลี่ยนสมาชิกภายในได้โดยไม่ต้องแก้/คอมไพล์โค้ดผู้ใช้ใหม่ และผู้ใช้เข้าถึงสถานะภายในตรงๆ ไม่ได้
2. 0x03 สามครั้ง: เพื่อบังคับ reset ตัวควบคุมให้เข้าโหมด 8 บิตที่รู้สถานะแน่นอน ไม่ว่าชิปจะค้างอยู่โหมดใด (หรือไฟเพิ่งมา) แล้วจึงสลับเป็น 4 บิตด้วย 0x02
3. ไม่มี mutex: สองทาสก์สลับกันเขียนกลางนิบเบิล/กลางคำสั่ง ทำให้ลำดับ E/RS/Data เพี้ยน ชิปได้ข้อมูลผิดนิบเบิล ตัวอักษรเพี้ยนหรือเคอร์เซอร์ผิดตำแหน่ง
4. I2C: เพิ่ม enum โหมด `LCD_BUS_MODE_I2C` และ field `i2c_port`, `i2c_addr` ใน `lcd_config_t` (หรือ union) แล้วเปลี่ยนเฉพาะชั้น low-level `lcd_write_bus_low_level()` ให้ส่งผ่าน PCF8574; API ชั้นบนไม่เปลี่ยน

## 6) ข้อควรทราบ
* โค้ดนี้ทดสอบกับตัวจำลองพฤติกรรม HD44780 บนคอมพิวเตอร์แล้ว (init, 4/8 บิต, bargraph, เวลา busy) แต่ **ยังไม่เคยรันกับบอร์ดจริงหรือ ESP-IDF จริง** จึงต้องลอง build/flash เอง
* ผมยืนยันขา ADC/VOLUME ของบอร์ด KMITL จากภาพไม่ได้ ให้ตรวจสายที่ต่อ VOLUME ไปยัง GPIO ใด ถ้าไม่ใช่ GPIO34 ให้ปรับใน menuconfig หรือใช้โหมด ramp

## 7) ภารกิจ 3 แบบ Ultrasonic + DHT (sdkconfig.defaults.m3_sensors)
* หน้า A (3 วินาที): บรรทัด 1 `Dist: 20.1 cm` / บรรทัด 2 แท่ง bargraph ตามระยะ (100% = `LAB_US_MAX_CM`, ค่าเริ่มต้น 100 cm)
* หน้า B (3 วินาที): บรรทัด 1 `Temp:25.0 C` / บรรทัด 2 `H: 80%` + แท่ง bargraph ความชื้น
* ความเร็วเสียงชดเชยด้วยอุณหภูมิจาก DHT, ใช้ค่ามัธยฐาน 3 ค่าลดสัญญาณรบกวน, อัปเดตจอเฉพาะเมื่อค่าเปลี่ยน
* **ขา TRIG / ECHO / DHT DATA ใน menuconfig ต้องตรงกับสายที่ต่อจริง** (ค่าเริ่มต้น 13 / 14 / 21 เป็นค่าสมมติ)
* ECHO ของ HC-SR04 เป็น 5V แต่ขา ESP32 รับได้ 3.3V ควรถามอาจารย์/ดูว่าบอร์ดมีวงจรลดระดับแล้วหรือไม่
