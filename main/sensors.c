#include "sdkconfig.h"
#include "sensors.h"

/* ---------------- pure helpers (always compiled) ---------------- */

bool dht_decode(const uint8_t d[5], bool is_dht22, int *temp_x10, int *hum_x10)
{
    uint8_t sum = (uint8_t)(d[0] + d[1] + d[2] + d[3]);
    if (sum != d[4]) {
        return false;
    }
    if (is_dht22) {
        *hum_x10 = (d[0] << 8) | d[1];
        int t = ((d[2] & 0x7F) << 8) | d[3];
        *temp_x10 = (d[2] & 0x80) ? -t : t;
    } else {                                   /* DHT11: integer + decimal byte */
        *hum_x10 = d[0] * 10 + d[1];
        int t = (d[2] & 0x7F) * 10 + (d[3] & 0x0F);
        *temp_x10 = (d[2] & 0x80) ? -t : t;
    }
    return true;
}

int ultrasonic_us_to_mm(uint32_t echo_us, int temp_x10)
{
    if (echo_us < 116 || echo_us > 23200) {    /* ~2 cm .. ~400 cm */
        return -1;
    }
    /* v[m/s] = 331.4 + 0.6*T  ->  v*10 = 3314 + 0.6*temp_x10 */
    int v_x10 = 3314 + (6 * temp_x10) / 10;
    /* distance[mm] = echo_us * v[mm/us] / 2 = echo_us * v_x10 / 20000 */
    return (int)(((uint64_t)echo_us * (uint64_t)v_x10) / 20000u);
}

/* ---------------- hardware part (Mission 3, sensor mode) ---------------- */
#if CONFIG_LAB_MISSION_3 && CONFIG_LAB_BAR_SRC_SENSORS

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "sensors";

#define PIN_TRIG ((gpio_num_t)CONFIG_LAB_US_TRIG_GPIO)
#define PIN_ECHO ((gpio_num_t)CONFIG_LAB_US_ECHO_GPIO)
#define PIN_DHT  ((gpio_num_t)CONFIG_LAB_DHT_GPIO)

#if CONFIG_LAB_DHT_TYPE_DHT22
#define DHT_IS_22 true
#else
#define DHT_IS_22 false
#endif

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

esp_err_t sensors_init(void)
{
    gpio_config_t trig = {
        .pin_bit_mask = 1ULL << PIN_TRIG,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&trig);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(PIN_TRIG, 0);

    gpio_config_t echo = {
        .pin_bit_mask = 1ULL << PIN_ECHO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&echo);
    if (err != ESP_OK) {
        return err;
    }

    gpio_config_t dht = {
        .pin_bit_mask = 1ULL << PIN_DHT,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,       /* open-drain, line is pulled up */
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&dht);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(PIN_DHT, 1);

    ESP_LOGI(TAG, "Pins: TRIG=GPIO%d ECHO=GPIO%d DHT%d DATA=GPIO%d",
             CONFIG_LAB_US_TRIG_GPIO, CONFIG_LAB_US_ECHO_GPIO,
             DHT_IS_22 ? 22 : 11, CONFIG_LAB_DHT_GPIO);
    return ESP_OK;
}

/* Wait while the pin stays at 'level'. Returns elapsed us, or -1 on timeout. */
static int wait_while(gpio_num_t pin, int level, int timeout_us)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(pin) == level) {
        if (esp_timer_get_time() - t0 > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - t0);
}

static esp_err_t dht_read_raw(uint8_t d[5])
{
    for (int i = 0; i < 5; i++) {
        d[i] = 0;
    }

    /* Start signal: pull low >= 18 ms (busy wait: independent of the tick rate) */
    gpio_set_level(PIN_DHT, 0);
    esp_rom_delay_us(20000);
    gpio_set_level(PIN_DHT, 1);
    esp_rom_delay_us(30);

    esp_err_t res = ESP_OK;
    portENTER_CRITICAL(&s_mux);
    if (wait_while(PIN_DHT, 1, 100) < 0 ||        /* sensor pulls low (response) */
        wait_while(PIN_DHT, 0, 100) < 0 ||        /* 80 us low */
        wait_while(PIN_DHT, 1, 100) < 0) {        /* 80 us high */
        res = ESP_ERR_TIMEOUT;
    } else {
        for (int i = 0; i < 40; i++) {
            if (wait_while(PIN_DHT, 0, 80) < 0) {  /* 50 us low */
                res = ESP_ERR_TIMEOUT;
                break;
            }
            int high = wait_while(PIN_DHT, 1, 120); /* 26-28 us = 0, ~70 us = 1 */
            if (high < 0) {
                res = ESP_ERR_TIMEOUT;
                break;
            }
            d[i / 8] = (uint8_t)(d[i / 8] << 1);
            if (high > 40) {
                d[i / 8] |= 1;
            }
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return res;
}

esp_err_t dht_read(int *temp_x10, int *hum_x10)
{
    /* One attempt per call.  The caller reads every 2 s, which already
     * satisfies the DHT11 >= 1 s sampling period.  Retrying within a few
     * hundred ms only violates that period and makes the next read fail. */
    uint8_t d[5];
    esp_err_t err = dht_read_raw(d);
    if (err != ESP_OK) {
        return err;
    }
    if (!dht_decode(d, DHT_IS_22, temp_x10, hum_x10)) {
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

int ultrasonic_read_us(void)
{
    gpio_set_level(PIN_TRIG, 0);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_TRIG, 1);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_TRIG, 0);

    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(PIN_ECHO) == 0) {        /* wait for echo rising edge */
        if (esp_timer_get_time() - t0 > 30000) {
            return -1;
        }
    }
    int64_t t1 = esp_timer_get_time();
    while (gpio_get_level(PIN_ECHO) == 1) {        /* measure the high pulse */
        if (esp_timer_get_time() - t1 > 30000) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - t1);
}

#endif /* CONFIG_LAB_MISSION_3 && CONFIG_LAB_BAR_SRC_SENSORS */
