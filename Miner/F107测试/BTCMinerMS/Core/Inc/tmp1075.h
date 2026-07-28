#ifndef __TMP1075_H
#define __TMP1075_H

#include "main.h"
#include <stdint.h>

#define TMP1075_ADDRESS_FIRST  0x48U
#define TMP1075_ADDRESS_LAST   0x4FU

typedef struct {
    I2C_HandleTypeDef *i2c;
    uint8_t address;
} tmp1075_t;

void tmp1075_init(tmp1075_t *device, I2C_HandleTypeDef *i2c);
uint8_t tmp1075_probe(tmp1075_t *device);
uint8_t tmp1075_read_temperature(const tmp1075_t *device,
                                 int16_t *temperature_centi_c);

#endif
