# Lab 6: HD44780 LCD Component Driver (ESP-IDF, ESP32)

รายงานประกอบใบงาน: การเขียนคอมโพเนนต์ไดรเวอร์จอ LCD 16x2 (HD44780) แบบ 4 บิตและ 8 บิต
บนบอร์ด KMITL (ESP32) ด้วย ESP-IDF

| รายการ | รายละเอียด |
|---|---|
| ชื่อ - รหัสนักศึกษา | `[กรอก]` |
| กลุ่ม / วันที่ส่ง | `[กรอก]` |
| ภารกิจที่ทำ | ภารกิจ 2 (จอ 2 ตัว: 4 บิต + 8 บิต) และภารกิจ 3 (Bargraph) |
| ภารกิจที่ไม่ได้ทำ | ภารกิจ 1 (ตรวจเวลาด้วย Logic Analyzer) ตามที่ตกลงกับอาจารย์ |
| เครื่องมือ | ESP-IDF v6.1, VS Code (ESP-IDF Extension), ESP32 (rev v3.0) |

---

## 1) โครงสร้างโปรเจกต์

```
lab6_lcd/
├── CMakeLists.txt                  # จำกัดคอมโพเนนต์ที่ build: main + lcd_hd44780
├── sdkconfig.defaults.m2           # ตั้งค่าภารกิจ 2
├── sdkconfig.defaults.m3           # ภารกิจ 3 อ่านค่า ADC
├── sdkconfig.defaults.m3_ramp      # ภารกิจ 3 ค่าจำลอง (ไม่ต้องต่อเซนเซอร์)
├── sdkconfig.defaults.m3_sensors   # ภารกิจ 3 Ultrasonic + DHT
├── components/lcd_hd44780/         # คอมโพเนนต์ไดรเวอร์ LCD
│   ├── include/lcd_hd44780.h       # API สาธารณะ (opaque handle)
│   └── lcd_hd44780.c               # การทำงานภายใน
└── main/
    ├── main.c                      # app_main + ภารกิจ 2, 3
    ├── sensors.c / sensors.h       # ไดรเวอร์ DHT และ HC-SR04 (ภารกิจ 3 แบบเซนเซอร์)
    ├── Kconfig.projbuild           # เมนูเลือกภารกิจ / ขา GPIO ใน menuconfig
    └── CMakeLists.txt
```

---

## 2) หลักการทำงานของไดรเวอร์

### 2.1 API (`lcd_hd44780.h`)

| ฟังก์ชัน | หน้าที่ |
|---|---|
| `lcd_new_device(config, &handle)` | สร้างอินสแตนซ์ ตั้งค่า GPIO และ init จอ (4 หรือ 8 บิต) |
| `lcd_del_device(handle)` | คืนทรัพยากร (GPIO, mutex, หน่วยความจำ) |
| `lcd_send_cmd` / `lcd_send_data` | ส่งคำสั่ง (RS=0) / ข้อมูล (RS=1) |
| `lcd_set_cursor(line, col)` | ย้ายเคอร์เซอร์ (บรรทัด 0 = 0x00, บรรทัด 1 = 0x40) |
| `lcd_write_string` | เขียนสตริง |
| `lcd_write_string_at` | ตั้งเคอร์เซอร์และเขียนสตริงใน mutex ครั้งเดียว (ส่วนที่เพิ่มเอง) |
| `lcd_clear` | ล้างจอ (รอ ≥ 1.52 ms) |
| `lcd_create_custom_char` | เขียนอักขระพิเศษลง CGRAM (ช่อง 0-7) |
| `lcd_init_bargraph_glyphs` | สร้าง 5 อักขระสำหรับแท่งกราฟ (1-5 พิกเซลต่อช่อง) |
| `lcd_draw_bargraph(row, col, width, %)` | วาดแท่งกราฟ 0-100% ละเอียด 5 พิกเซลต่อช่อง |

### 2.2 แนวคิดการออกแบบ
* **Opaque handle:** struct ภายในถูกซ่อนไว้ใน `.c` ผู้ใช้ถือได้แค่ pointer จึงแก้โครงสร้างภายในได้โดยไม่กระทบโค้ดผู้ใช้
* **หลายอินสแตนซ์:** แต่ละจอมี handle, ขา GPIO และ mutex ของตัวเอง จึงใช้จอ 4 บิตและ 8 บิตพร้อมกันได้ (ภารกิจ 2)
* **Thread-safe:** ทุกฟังก์ชันที่แตะบัส LCD ล็อก mutex ของอินสแตนซ์นั้น ป้องกันสองทาสก์เขียนสลับกลางนิบเบิล
* **RW ต่อ GND:** ใช้โหมดเขียนอย่างเดียว ไม่อ่าน Busy Flag จึงใช้การหน่วงเวลาตามค่าใน datasheet แทน
  (ป้องกัน 5V จากจอไหลเข้าขา ESP32 ด้วย)

### 2.3 ลำดับ Init แบบ 4 บิต (Initialization by Instruction)
1. รอ 50 ms หลังจ่ายไฟ (datasheet ต้อง > 40 ms)
2. ส่ง `0x3` → รอ 5 ms (> 4.1 ms) → ส่ง `0x3` → รอ 150 µs (> 100 µs) → ส่ง `0x3` → ส่ง `0x2` (เปลี่ยนเป็น 4 บิต)
3. Function Set `0x28` (4 บิต, 2 บรรทัด, ฟอนต์ 5x8)
4. Display Off `0x08` → Clear `0x01` (รอ 2 ms) → Entry Mode `0x06` → Display On `0x0C`

### 2.4 จังหวะสัญญาณ Enable (ค่าที่ตั้งในโค้ด)

| พารามิเตอร์ | ข้อกำหนด datasheet | ค่าในโค้ด |
|---|---|---|
| Data setup ก่อน E ลง | ≥ 195 ns | 1 µs |
| ความกว้างพัลส์ E (tPWE) | ≥ 450 ns | ≈ 1 µs + เวลาสลับ GPIO |
| คาบ E (tcycE) | ≥ 1000 ns | ≈ 52 µs |
| เวลาประมวลผลคำสั่งทั่วไป | 37 µs | 50 µs ต่อนิบเบิล |
| Clear / Home | 1.52 ms | 2 ms |

ข้อมูลถูก latch ที่ขอบขาลงของ E

---

## 3) การต่อวงจร

### 3.1 จอ LCD 16 ขา (โมดูลมาตรฐาน ไม่มีบอร์ด I2C)
ขา 1 อยู่ซ้ายสุดเมื่อหันหน้าจอเข้าหาตัว ขา 16 อยู่ขวาสุด (มีเลข 16 พิมพ์กำกับ)

| ขา | ชื่อ | LCD1 (4 บิต) | LCD2 (8 บิต) |
|---|---|---|---|
| 1 | VSS | GND | GND |
| 2 | VDD | 5V | 5V |
| 3 | V0 (คอนทราสต์) | GND | GND |
| 4 | RS | GPIO16 | GPIO21 |
| 5 | RW | GND | GND |
| 6 | E | GPIO17 | GPIO22 |
| 7 | D0 | ไม่ต่อ | GPIO18 |
| 8 | D1 | ไม่ต่อ | GPIO19 |
| 9 | D2 | ไม่ต่อ | GPIO13 |
| 10 | D3 | ไม่ต่อ | GPIO14 |
| 11 | D4 | GPIO23 | GPIO32 |
| 12 | D5 | GPIO25 | GPIO33 |
| 13 | D6 | GPIO26 | GPIO4 |
| 14 | D7 | GPIO27 | GPIO5 |
| 15 | A (LED+) | ไม่ต่อ | ไม่ต่อ |
| 16 | K (LED-) | ไม่ต่อ | ไม่ต่อ |

หมายเหตุ: LCD1 ตรงกับผังวงจร `lcd.svg` ที่ใช้ในแล็บ ส่วนขา LCD2 เลือกเองจากขาที่เหลือ
(เลี่ยง GPIO 0, 2, 12, 15 และ GPIO 34-39 ที่เป็นขา input only) ขา GPIO5 เป็น strapping pin
แต่ใช้เป็นเอาต์พุตไปยังจอได้ตามปกติ

### 3.2 ภารกิจ 3 แบบ Ultrasonic + DHT (เพิ่มจาก LCD1 ใช้จอเดียว)

| เซนเซอร์ | ขา | ต่อกับ |
|---|---|---|
| HC-SR04 | TRIG | GPIO `[กรอก]` |
| HC-SR04 | ECHO | GPIO `[กรอก]` |
| DHT11 | DATA | GPIO `[กรอก]` |
| ทั้งสอง | VCC / GND | 5V (HC-SR04), 3.3V หรือ 5V (DHT11) / GND |

ตั้งเลขขาให้ตรงที่ต่อจริงใน `menuconfig → Lab6 LCD HD44780` (ค่าเริ่มต้น 13 / 14 / 21 เป็นค่าสมมติ)

> **ข้อควรระวัง:** ขา ECHO ของ HC-SR04 ส่งสัญญาณ 5V แต่ขา ESP32 รับได้ 3.3V
> ให้ใช้วงจรแบ่งแรงดันหรือโมดูลที่มีวงจรลดระดับ ถ้าบอร์ดไม่มีให้

---

## 4) วิธี Build และ Flash

เปิดโฟลเดอร์ `lab6_lcd` ใน VS Code → เปิด **ESP-IDF Terminal** (F1 → `ESP-IDF: Open ESP-IDF Terminal`)
เมื่อเปลี่ยนภารกิจให้ลบ `sdkconfig` และ `build` ก่อนทุกครั้ง (PowerShell):

```powershell
Remove-Item sdkconfig -ErrorAction SilentlyContinue
Remove-Item build -Recurse -Force -ErrorAction SilentlyContinue
```

**ภารกิจ 2**
```powershell
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.m2" set-target esp32
idf.py -p COM4 build flash monitor
```

**ภารกิจ 3 (ทดสอบแบบค่าจำลองก่อน)**
```powershell
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.m3_ramp" set-target esp32
idf.py -p COM4 build flash monitor
```

**ภารกิจ 3 (Ultrasonic + DHT)**
```powershell
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults.m3_sensors" set-target esp32
idf.py menuconfig        # Lab6 LCD HD44780 -> ตั้งขา TRIG / ECHO / DHT DATA
idf.py -p COM4 build flash monitor
```

ออกจาก monitor ด้วย `Ctrl+]` (เปลี่ยน `COM4` เป็นพอร์ตของเครื่อง)

---

## 5) ผลการทดลอง

### 5.1 ภารกิจ 2: จอ 2 ตัว (4 บิต + 8 บิต) ผ่าน

| จอ | บรรทัด 1 | บรรทัด 2 | ผลที่ได้จริง |
|---|---|---|---|
| LCD1 (4 บิต) | `Countdown (s)` | `T- 01:00` ถึง `T- 00:00` วนซ้ำ | นับถอยหลังถูกต้อง |
| LCD2 (8 บิต) | `Up:177s OK` (เวลาที่ระบบทำงาน) | `Heap:300xxx B` (หน่วยความจำที่เหลือ) | ค่านับต่อเนื่องและอัปเดตต่อเนื่อง |

Log จาก monitor (ตัดตอน):
```
I (266) main_task: Calling app_main()
I (266) main_app: Starting Component-Based LCD System...
I (330) lcd_hd44780: Initialized LCD Instance successfully (4-Bit Mode)
I (387) lcd_hd44780: Initialized LCD Instance successfully (8-Bit Mode)
I (394) main_task: Returned from app_main()
```

รูปประกอบ: `[แทรกรูปถ่ายจอทั้งสองตัวและรูปการต่อสาย]`

### 5.2 ภารกิจ 3: Bargraph
ผลที่คาดหวังและต้องตรวจเมื่อรันจริง

| โหมด | บรรทัด 1 | บรรทัด 2 |
|---|---|---|
| Ramp / ADC | `Level:  50%` | แท่งกราฟยาว 0-100% ตามค่า |
| Ultrasonic + DHT หน้า A | `Dist: 20.1 cm` | แท่งตามระยะ (เต็มที่ 100 cm) |
| Ultrasonic + DHT หน้า B | `Temp:25.0 C` | `H: 80%` และแท่งความชื้น |

ผลที่ได้จริง: `[กรอกหลังรัน พร้อมแทรกรูปถ่ายจอ และภาพหน้าจอ monitor]`

---

## 6) คำตอบ Post-Lab
1. **ทำไมใช้ opaque handle:** ซ่อน struct ไว้ใน `.c` ผู้ใช้เห็นแค่ pointer จึงแก้โครงสร้างภายในโดยไม่ต้องแก้โค้ดผู้ใช้
   และผู้ใช้แก้สถานะภายในโดยตรงไม่ได้ ทำให้ API ปลอดภัยและเสถียร
2. **ทำไมส่ง `0x03` สามครั้งตอน init:** เพื่อบังคับให้ตัวควบคุมกลับเข้าโหมด 8 บิตที่รู้สถานะแน่นอน
   ไม่ว่าชิปจะค้างอยู่โหมดใด (หรือไฟเพิ่งมา) แล้วจึงสลับเป็น 4 บิตด้วย `0x02`
3. **ถ้าไม่มี mutex จะเกิดอะไร:** สองทาสก์เขียนสลับกลางนิบเบิลหรือกลางคำสั่ง ลำดับ E/RS/Data เพี้ยน
   ชิปได้ข้อมูลผิดนิบเบิล ตัวอักษรเพี้ยนหรือเคอร์เซอร์ผิดตำแหน่ง
4. **ถ้าจะรองรับ I2C:** เพิ่มโหมด `LCD_BUS_MODE_I2C` และฟิลด์พอร์ต/ที่อยู่ใน `lcd_config_t`
   แล้วเปลี่ยนเฉพาะชั้น low-level (`lcd_write_bus_low_level`) ให้ส่งผ่าน PCF8574 โดย API ชั้นบนไม่ต้องเปลี่ยน

---

## 7) ปัญหาที่พบและวิธีแก้ (ESP-IDF v6.1)

| ปัญหา | สาเหตุ | วิธีแก้ |
|---|---|---|
| `driver/gpio.h: No such file` | v6.1 แยกคอมโพเนนต์ `driver` ออก | เพิ่ม `esp_driver_gpio` ใน `REQUIRES` ของคอมโพเนนต์ |
| `esp32-arduino requires CONFIG_FREERTOS_HZ=1000` | มีคอมโพเนนต์ Arduino ปนในการติดตั้ง ESP-IDF | `set(COMPONENTS main lcd_hd44780)` และตั้ง `CONFIG_FREERTOS_HZ=1000` |
| `Could not open COM4 ... Access is denied` | มีโปรเซส monitor เก่าค้างจับพอร์ต | ปิดโปรเซส python ที่ค้าง แล้ว flash ใหม่ |
| `rm -f` ใช้ไม่ได้ | PowerShell ไม่ใช่ bash | ใช้ `Remove-Item` แทน |

## 8) ความแตกต่างจากโค้ดตัวอย่างในใบงาน
1. เพิ่ม Entry Mode `0x06` และ Display Off `0x08` ในลำดับ init
2. รอ 5 ms ด้วย `esp_rom_delay_us(5000)` แทน `vTaskDelay(pdMS_TO_TICKS(5))` ซึ่งที่ tick 100 Hz เท่ากับ 0 tick
3. เพิ่ม `lcd_write_string_at()` กันทาสก์อื่นแทรกระหว่างตั้งเคอร์เซอร์กับเขียนข้อความ
4. ตรวจค่า return ของ `gpio_config` และตรวจขอบเขตพารามิเตอร์ใน `lcd_draw_bargraph`
5. เขียนข้อความทุกบรรทัดให้ครบ 16 ตัวอักษร เพื่อไม่ให้ตัวอักษรเก่าค้าง
