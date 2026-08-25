#ifndef __TPS546D24A_H
#define __TPS546D24A_H

#include "main.h"
#include <stdint.h>

#define TPS546D24A_I2C_ADDRESS  0x24U

typedef struct {
    I2C_HandleTypeDef *i2c;
    uint8_t address;
} tps546d24a_t;

typedef struct {
    uint16_t vout_mv;
    uint32_t iout_ma;
    uint32_t power_mw;
    int16_t temperature_centi_c;
    uint16_t status_word;
    uint8_t enabled;
} tps546d24a_telemetry_t;

void tps546d24a_init(tps546d24a_t *device, I2C_HandleTypeDef *i2c);
uint8_t tps546d24a_probe(const tps546d24a_t *device);
uint8_t tps546d24a_set_enabled(const tps546d24a_t *device, uint8_t enabled);
uint8_t tps546d24a_read_telemetry(const tps546d24a_t *device,
                                  tps546d24a_telemetry_t *telemetry);

#endif
