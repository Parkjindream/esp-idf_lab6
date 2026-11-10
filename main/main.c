#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "sdkconfig.h"
#include "lcd_hd44780.h"

#if CONFIG_LAB_MISSION_3 && CONFIG_LAB_BAR_SRC_ADC
#include "esp_adc/adc_oneshot.h"
#endif
#if CONFIG_LAB_MISSION_3 && CONFIG_LAB_BAR_SRC_SENSORS
#include "sensors.h"
#endif

static const char *TAG = "main_app";

/* ------------------------------------------------------------------ */
/* Pin map  (GPIO numbers = labels on the ESP32 header of the board)   */
/* Selected in menuconfig -> Lab6 LCD HD44780 -> Pin map               */
/*                                                                     */
/* Profile "DIAGRAM" (default, matches lcd.svg):                       */
/*   LCD1 (4-bit): RS=16 EN=17 D4..D7 = 23,25,26,27   RW -> GND        */
/*   LCD2 (8-bit): RS=21 EN=22 D0..D7 = 18,19,13,14,32,33,4,5          */
/* Profile "SHEET" (lab-sheet example):                                */
/*   LCD1 (4-bit): RS=4  EN=5  D4..D7 = 16,17,18,19   RW -> GND        */
/*   LCD2 (8-bit): RS=21 EN=22 D0..D7 = 13,14,23,25,26,27,32,33        */
/* ------------------------------------------------------------------ */
#if CONFIG_LAB_PINMAP_SHEET
#define LCD1_RS  GPIO_NUM_4
#define LCD1_EN  GPIO_NUM_5
#define LCD1_D4  GPIO_NUM_16
#define LCD1_D5  GPIO_NUM_17
#define LCD1_D6  GPIO_NUM_18
#define LCD1_D7  GPIO_NUM_19
#define LCD2_RS  GPIO_NUM_21
#define LCD2_EN  GPIO_NUM_22
#define LCD2_D0  GPIO_NUM_13
#define LCD2_D1  GPIO_NUM_14
#define LCD2_D2  GPIO_NUM_23
#define LCD2_D3  GPIO_NUM_25
#define LCD2_D4  GPIO_NUM_26
#define LCD2_D5  GPIO_NUM_27
#define LCD2_D6  GPIO_NUM_32
#define LCD2_D7  GPIO_NUM_33
#else /* CONFIG_LAB_PINMAP_DIAGRAM */
#define LCD1_RS  GPIO_NUM_16
#define LCD1_EN  GPIO_NUM_17
#define LCD1_D4  GPIO_NUM_23
#define LCD1_D5  GPIO_NUM_25
#define LCD1_D6  GPIO_NUM_26
#define LCD1_D7  GPIO_NUM_27
#define LCD2_RS  GPIO_NUM_21
#define LCD2_EN  GPIO_NUM_22
#define LCD2_D0  GPIO_NUM_18
#define LCD2_D1  GPIO_NUM_19
#define LCD2_D2  GPIO_NUM_13
#define LCD2_D3  GPIO_NUM_14
#define LCD2_D4  GPIO_NUM_32
#define LCD2_D5  GPIO_NUM_33
#define LCD2_D6  GPIO_NUM_4
#define LCD2_D7  GPIO_NUM_5
#endif

static const lcd_config_t lcd1_config = {
    .bus_mode = LCD_BUS_MODE_4BIT,
    .rs_io = LCD1_RS,
    .rw_io = GPIO_NUM_NC,                    // RW tied to GND
    .en_io = LCD1_EN,
    .data_ios = { LCD1_D4, LCD1_D5, LCD1_D6, LCD1_D7 }
};

#if CONFIG_LAB_MISSION_2
static const lcd_config_t lcd2_config = {
    .bus_mode = LCD_BUS_MODE_8BIT,
    .rs_io = LCD2_RS,
    .rw_io = GPIO_NUM_NC,                    // RW tied to GND
    .en_io = LCD2_EN,
    .data_ios = { LCD2_D0, LCD2_D1, LCD2_D2, LCD2_D3,
                  LCD2_D4, LCD2_D5, LCD2_D6, LCD2_D7 }
};
#endif

/* Print a full 16-character line (padded with spaces so old text is overwritten) */
static void lcd_line(lcd_handle_t lcd, uint8_t row, const char *fmt, ...)
{
    char tmp[40];
    char line[17];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    snprintf(line, sizeof(line), "%-16.16s", tmp);
    lcd_write_string_at(lcd, row, 0, line);
}

/* ================================================================== */
/* Mission 1 : timing verification                                     */
/* ================================================================== */
#if CONFIG_LAB_MISSION_1
static void timing_task(void *arg)
{
    lcd_handle_t lcd = (lcd_handle_t)arg;
    while (1) {
        // Burst of 16 characters = 32 E pulses (4-bit), repeated every ~50 ms
        lcd_write_string_at(lcd, 1, 0, "UUUUUUUUUUUUUUUU");   // 'U' = 0x55
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void run_mission(void)
{
    lcd_handle_t lcd = NULL;
    ESP_ERROR_CHECK(lcd_new_device(&lcd1_config, &lcd));
    lcd_line(lcd, 0, "M1 Timing Test");
    xTaskCreate(timing_task, "timing", 3072, (void *)lcd, 4, NULL);
}
#endif

/* ================================================================== */
/* Mission 2 : dual instance, mixed mode                               */
/* ================================================================== */
#if CONFIG_LAB_MISSION_2
static void countdown_task(void *arg)      // LCD1 (4-bit)
{
    lcd_handle_t lcd = (lcd_handle_t)arg;
    int remaining = 60;
    lcd_line(lcd, 0, "Countdown (s)");
    while (1) {
        lcd_line(lcd, 1, "T- %02d:%02d", remaining / 60, remaining % 60);
        vTaskDelay(pdMS_TO_TICKS(1000));
        remaining = (remaining == 0) ? 60 : remaining - 1;
    }
}

static void status_task(void *arg)         // LCD2 (8-bit)
{
    lcd_handle_t lcd = (lcd_handle_t)arg;
    while (1) {
        unsigned long up_s = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
        lcd_line(lcd, 0, "Up:%lus OK", up_s);
        lcd_line(lcd, 1, "Heap:%lu B", (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void run_mission(void)
{
    lcd_handle_t lcd1 = NULL, lcd2 = NULL;
    ESP_ERROR_CHECK(lcd_new_device(&lcd1_config, &lcd1));
    ESP_ERROR_CHECK(lcd_new_device(&lcd2_config, &lcd2));
    xTaskCreate(countdown_task, "countdown", 3072, (void *)lcd1, 5, NULL);
    xTaskCreate(status_task, "status", 3072, (void *)lcd2, 4, NULL);
}
#endif

/* ================================================================== */
/* Mission 3 : analog bargraph + message queue server                  */
/* ================================================================== */
#if CONFIG_LAB_MISSION_3

typedef struct {
    uint8_t line;
    uint8_t column;
    char text[17];
} lcd_msg_t;

static QueueHandle_t lcd_queue = NULL;

static void lcd_server_task(void *pvParameters)
{
    lcd_handle_t lcd = (lcd_handle_t)pvParameters;
    lcd_msg_t msg;
    while (1) {
        if (xQueueReceive(lcd_queue, &msg, portMAX_DELAY) == pdTRUE) {
            lcd_write_string_at(lcd, msg.line, msg.column, msg.text);   // atomic cursor + text
        }
    }
}

#if CONFIG_LAB_BAR_SRC_ADC
static adc_oneshot_unit_handle_t adc_handle;
static adc_channel_t adc_channel;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
#define LAB_ADC_ATTEN ADC_ATTEN_DB_12
#else
#define LAB_ADC_ATTEN ADC_ATTEN_DB_11
#endif

static esp_err_t sensor_init(void)
{
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(CONFIG_LAB_ADC_GPIO, &unit, &adc_channel);
    if (err != ESP_OK || unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC1 pin", CONFIG_LAB_ADC_GPIO);
        return ESP_ERR_INVALID_ARG;
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    err = adc_oneshot_new_unit(&unit_cfg, &adc_handle);
    if (err != ESP_OK) {
        return err;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = LAB_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_oneshot_config_channel(adc_handle, adc_channel, &chan_cfg);
}

static uint8_t sensor_read_percent(void)
{
    int sum = 0, raw = 0;
    for (int i = 0; i < 8; i++) {              // average 8 samples to reduce noise
        if (adc_oneshot_read(adc_handle, adc_channel, &raw) == ESP_OK) {
            sum += raw;
        }
    }
    int avg = sum / 8;                          // 0..4095 (12-bit)
    int pct = (avg * 100) / 4095;
    return (uint8_t)(pct > 100 ? 100 : pct);
}
#elif CONFIG_LAB_BAR_SRC_RAMP   /* software ramp */
static esp_err_t sensor_init(void) { return ESP_OK; }

static uint8_t sensor_read_percent(void)
{
    static uint8_t p = 0;
    uint8_t cur = p;
    p = (p + 5) % 105;                          // 0,5,...,100,0,...
    return cur;
}
#else   /* ultrasonic + DHT */
static esp_err_t sensor_init(void) { return sensors_init(); }
#endif

#if CONFIG_LAB_BAR_SRC_SENSORS
/* ------------------------------------------------------------------
 * Ultrasonic + DHT screen (single page, always visible)
 *   Row 0: "D20.1 T24.7 H53"  (distance, temperature, humidity)
 *   Row 1: bargraph of distance (0..100% of CONFIG_LAB_US_MAX_CM)
 * Row-0 text goes through the LCD queue server (dispatch pattern);
 * the bargraph is drawn directly (driver is mutex-protected).
 * ------------------------------------------------------------------ */
static void send_row0(const char *text)
{
    lcd_msg_t msg = { .line = 0, .column = 0 };
    snprintf(msg.text, sizeof(msg.text), "%-16.16s", text);
    xQueueSend(lcd_queue, &msg, 0);
}

static int median3(int a, int b, int c)
{
    if (a > b) { int t = a; a = b; b = t; }
    if (b > c) { b = c; }
    return (a > b) ? a : b;
}

static void sensors_task(void *pvParameters)
{
    lcd_handle_t lcd = (lcd_handle_t)pvParameters;
    const int DHT_LOOPS = 20;       /* DHT read every 2 s */

    int loop = 0;
    int mm = -1, temp10 = 250, hum10 = 0;
    bool have_dht = false;
    int win[3] = { -1, -1, -1 };
    char last_row0[64] = "";
    int last_bar = -1;

    while (1) {
        /* --- ultrasonic: one shot every 100 ms, median of the last 3 --- */
        int us = ultrasonic_read_us();
        int new_mm = (us < 0) ? -1 : ultrasonic_us_to_mm((uint32_t)us, have_dht ? temp10 : 250);
        win[loop % 3] = new_mm;
        if (new_mm >= 0) {
            if (win[0] >= 0 && win[1] >= 0 && win[2] >= 0) {
                mm = median3(win[0], win[1], win[2]);
            } else {
                mm = new_mm;
            }
        }

        /* --- DHT every 2 s --- */
        if (loop % DHT_LOOPS == 0) {
            int t, h;
            esp_err_t r = dht_read(&t, &h);
            if (r == ESP_OK) {
                temp10 = t; hum10 = h; have_dht = true;
                ESP_LOGI(TAG, "Dist=%s Temp=%d.%d C Hum=%d.%d %%",
                         mm >= 0 ? "ok" : "---", temp10 / 10, abs(temp10 % 10), hum10 / 10, hum10 % 10);
            } else {
                ESP_LOGW(TAG, "DHT read failed (%s) - check DATA pin / pull-up", esp_err_to_name(r));
            }
        }

        /* --- row 0: distance + temperature + humidity on one page --- */
        char row0[64], dpart[16], tpart[16], hpart[16];
        if (mm >= 0) {
            snprintf(dpart, sizeof(dpart), "D%d.%d", mm / 10, mm % 10);
        } else {
            snprintf(dpart, sizeof(dpart), "D---");
        }
        if (have_dht) {
            snprintf(tpart, sizeof(tpart), "T%d.%d", temp10 / 10, abs(temp10 % 10));
            snprintf(hpart, sizeof(hpart), "H%d", hum10 / 10);
        } else {
            snprintf(tpart, sizeof(tpart), "T--");
            snprintf(hpart, sizeof(hpart), "H--");
        }
        snprintf(row0, sizeof(row0), "%s %s %s", dpart, tpart, hpart);

        if (strcmp(row0, last_row0) != 0) {
            strcpy(last_row0, row0);
            send_row0(row0);
        }

        /* --- row 1: distance bargraph, full width --- */
        int bar = 0;
        if (mm >= 0) {
            int pct = (mm / 10) * 100 / CONFIG_LAB_US_MAX_CM;
            bar = pct > 100 ? 100 : pct;
        }
        if (bar != last_bar) {
            last_bar = bar;
            lcd_draw_bargraph(lcd, 1, 0, 16, (uint8_t)bar);
        }

        loop++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#else
static void bargraph_task(void *pvParameters)
{
    lcd_handle_t lcd = (lcd_handle_t)pvParameters;
    int last = -1;
    while (1) {
        uint8_t pct = sensor_read_percent();
        if (pct != last) {                      // update only when the value changed (flicker-free)
            last = pct;
            lcd_draw_bargraph(lcd, 1, 0, 16, pct);
            lcd_msg_t msg = { .line = 0, .column = 0 };
            snprintf(msg.text, sizeof(msg.text), "Level: %3u%%     ", (unsigned)pct);
            xQueueSend(lcd_queue, &msg, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif /* !CONFIG_LAB_BAR_SRC_SENSORS */

static void run_mission(void)
{
    lcd_handle_t lcd = NULL;
    ESP_ERROR_CHECK(lcd_new_device(&lcd1_config, &lcd));
    ESP_ERROR_CHECK(lcd_init_bargraph_glyphs(lcd));
    ESP_ERROR_CHECK(sensor_init());

    lcd_line(lcd, 0, "System Ready!");
    vTaskDelay(pdMS_TO_TICKS(1000));

    lcd_queue = xQueueCreate(10, sizeof(lcd_msg_t));
    if (lcd_queue == NULL) {
        ESP_LOGE(TAG, "Queue creation failed");
        return;
    }
    xTaskCreate(lcd_server_task, "lcd_server", 3072, (void *)lcd, 5, NULL);
#if CONFIG_LAB_BAR_SRC_SENSORS
    xTaskCreate(sensors_task, "sensors", 4096, (void *)lcd, 4, NULL);
#else
    xTaskCreate(bargraph_task, "bargraph", 3072, (void *)lcd, 4, NULL);
#endif
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Component-Based LCD System...");
    run_mission();
}
