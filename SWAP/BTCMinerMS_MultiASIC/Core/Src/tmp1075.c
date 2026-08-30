#include "tmp1075.h"

#define TMP1075_TEMPERATURE_REGISTER  0x00U
#define TMP1075_I2C_TIMEOUT_MS        20U

void tmp1075_init(tmp1075_t *device, I2C_HandleTypeDef *i2c) {
    device->i2c = i2c;
    device->address = 0;
}

uint8_t tmp1075_probe(tmp1075_t *device) {
    uint8_t address;
    if (device == 0 || device->i2c == 0) return 0;

    for (address = TMP1075_ADDRESS_FIRST;
         address <= TMP1075_ADDRESS_LAST; ++address) {
        if (HAL_I2C_IsDeviceReady(device->i2c, (uint16_t)(address << 1),
                                  1, TMP1075_I2C_TIMEOUT_MS) == HAL_OK) {
            device->address = address;
            return 1;
        }
    }
    device->address = 0;
    return 0;
}

uint8_t tmp1075_read_temperature(const tmp1075_t *device,
                                 int16_t *temperature_centi_c) {
    uint8_t data[2];
    uint16_t word;
    int32_t raw;
    int32_t scaled;

    if (device == 0 || device->i2c == 0 || device->address == 0 ||
        temperature_centi_c == 0) return 0;
    if (HAL_I2C_Mem_Read(device->i2c, (uint16_t)(device->address << 1),
                         TMP1075_TEMPERATURE_REGISTER, I2C_MEMADD_SIZE_8BIT,
                         data, 2, TMP1075_I2C_TIMEOUT_MS) != HAL_OK) {
        return 0;
    }

    /* TMP1075 temperature is a signed 12-bit value, MSB first, 0.0625 C/LSB. */
    word = ((uint16_t)data[0] << 8) | data[1];
    raw = (int32_t)(word >> 4);
    if (raw & 0x0800) raw -= 0x1000;
    scaled = raw * 625;
    if (scaled >= 0) scaled = (scaled + 50) / 100;
    else scaled = (scaled - 50) / 100;
    *temperature_centi_c = (int16_t)scaled;
    return 1;
}
