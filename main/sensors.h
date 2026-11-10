#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pure helpers (no hardware access, unit-testable on a PC) */

/** Decode the 5 raw DHT bytes. Returns false on checksum error.
 *  Results are in tenths: temp_x10 = 251 -> 25.1 C, hum_x10 = 652 -> 65.2 %RH */
bool dht_decode(const uint8_t d[5], bool is_dht22, int *temp_x10, int *hum_x10);

/** Convert an HC-SR04 echo pulse width (us) to millimetres.
 *  Speed of sound is compensated with the air temperature (tenths of deg C).
 *  Returns -1 if the pulse is outside the sensor's 2..400 cm range. */
int ultrasonic_us_to_mm(uint32_t echo_us, int temp_x10);

#if CONFIG_LAB_MISSION_3 && CONFIG_LAB_BAR_SRC_SENSORS
/* Hardware access (pins come from menuconfig) */
esp_err_t sensors_init(void);
esp_err_t dht_read(int *temp_x10, int *hum_x10);   /* ESP_OK / ESP_ERR_TIMEOUT / ESP_ERR_INVALID_CRC */
int ultrasonic_read_us(void);                        /* echo width in us, -1 on timeout */
#endif

#ifdef __cplusplus
}
#endif

#endif // SENSORS_H
