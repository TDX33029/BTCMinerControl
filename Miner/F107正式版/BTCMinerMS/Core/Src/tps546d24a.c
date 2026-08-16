#include "tps546d24a.h"
#include <string.h>

#define PMBUS_VOUT_MODE          0x20U
#define PMBUS_OPERATION          0x01U
#define PMBUS_STATUS_WORD        0x79U
#define PMBUS_READ_VOUT          0x8BU
#define PMBUS_READ_IOUT          0x8CU
#define PMBUS_READ_TEMPERATURE_1 0x8DU
#define TPS_I2C_TIMEOUT_MS       25U

static uint8_t read_byte(const tps546d24a_t *device, uint8_t command,
                         uint8_t *value) {
    return HAL_I2C_Mem_Read(device->i2c, (uint16_t)(device->address << 1),
                            command, I2C_MEMADD_SIZE_8BIT, value, 1,
                            TPS_I2C_TIMEOUT_MS) == HAL_OK;
}

/* PMBus words are transferred least-significant byte first. */
static uint8_t read_word(const tps546d24a_t *device, uint8_t command,
                         uint16_t *value) {
    uint8_t data[2];
    if (HAL_I2C_Mem_Read(device->i2c, (uint16_t)(device->address << 1),
                         command, I2C_MEMADD_SIZE_8BIT, data, 2,
                         TPS_I2C_TIMEOUT_MS) != HAL_OK) {
        return 0;
    }
    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return 1;
}

static uint8_t write_byte(const tps546d24a_t *device, uint8_t command,
                          uint8_t value) {
    return HAL_I2C_Mem_Write(device->i2c, (uint16_t)(device->address << 1),
                             command, I2C_MEMADD_SIZE_8BIT, &value, 1,
                             TPS_I2C_TIMEOUT_MS) == HAL_OK;
}

static int32_t scale_linear11(uint16_t raw, int32_t scale) {
    int32_t mantissa = (int32_t)(raw & 0x07FFU);
    int32_t exponent = (int32_t)((raw >> 11) & 0x1FU);
    int32_t value;
    uint32_t divisor;

    if (mantissa & 0x0400) mantissa -= 0x0800;
    if (exponent & 0x10) exponent -= 0x20;

    value = mantissa * scale;
    if (exponent >= 0) return value << exponent;

    divisor = 1UL << (uint32_t)(-exponent);
    if (value >= 0) return (value + (int32_t)(divisor / 2U)) / (int32_t)divisor;
    return (value - (int32_t)(divisor / 2U)) / (int32_t)divisor;
}

static uint32_t scale_linear16(uint16_t raw, uint8_t mode, uint32_t scale) {
    int32_t exponent = (int32_t)(mode & 0x1FU);
    uint32_t value = (uint32_t)raw * scale;
    uint32_t divisor;

    if (exponent & 0x10) exponent -= 0x20;
    if (exponent >= 0) return value << exponent;
    divisor = 1UL << (uint32_t)(-exponent);
    return (value + divisor / 2U) / divisor;
}

void tps546d24a_init(tps546d24a_t *device, I2C_HandleTypeDef *i2c) {
    device->i2c = i2c;
    device->address = TPS546D24A_I2C_ADDRESS;
}

uint8_t tps546d24a_probe(const tps546d24a_t *device) {
    if (device == 0 || device->i2c == 0) return 0;
    return HAL_I2C_IsDeviceReady(device->i2c,
                                 (uint16_t)(device->address << 1),
                                 2, TPS_I2C_TIMEOUT_MS) == HAL_OK;
}

uint8_t tps546d24a_set_enabled(const tps546d24a_t *device, uint8_t enabled) {
    uint8_t operation;
    uint8_t requested = enabled ? 0x80U : 0x00U;
    if (device == 0 || device->i2c == 0) return 0;
    if (!write_byte(device, PMBUS_OPERATION, requested)) return 0;
    HAL_Delay(2);
    if (!read_byte(device, PMBUS_OPERATION, &operation)) return 0;
    return ((operation & 0x80U) != 0U) == (enabled != 0U);
}

uint8_t tps546d24a_read_telemetry(const tps546d24a_t *device,
                                  tps546d24a_telemetry_t *telemetry) {
    uint8_t vout_mode, operation;
    uint16_t vout_raw, iout_raw, temp_raw, status_word;
    uint32_t vout_mv;
    int32_t iout_ma, temp_centi;

    if (device == 0 || telemetry == 0) return 0;
    if (!read_byte(device, PMBUS_OPERATION, &operation) ||
        !read_byte(device, PMBUS_VOUT_MODE, &vout_mode) ||
        !read_word(device, PMBUS_READ_VOUT, &vout_raw) ||
        !read_word(device, PMBUS_READ_IOUT, &iout_raw) ||
        !read_word(device, PMBUS_READ_TEMPERATURE_1, &temp_raw) ||
        !read_word(device, PMBUS_STATUS_WORD, &status_word)) {
        return 0;
    }

    vout_mv = scale_linear16(vout_raw, vout_mode, 1000U);
    iout_ma = scale_linear11(iout_raw, 1000);
    temp_centi = scale_linear11(temp_raw, 100);
    if (iout_ma < 0) iout_ma = 0;
    if (vout_mv > 65535U) vout_mv = 65535U;
    if (temp_centi > 32767) temp_centi = 32767;
    if (temp_centi < -32768) temp_centi = -32768;

    memset(telemetry, 0, sizeof(*telemetry));
    telemetry->vout_mv = (uint16_t)vout_mv;
    telemetry->iout_ma = (uint32_t)iout_ma;
    telemetry->power_mw = (uint32_t)(((uint64_t)vout_mv *
                                      (uint32_t)iout_ma + 500ULL) / 1000ULL);
    telemetry->temperature_centi_c = (int16_t)temp_centi;
    telemetry->status_word = status_word;
    telemetry->enabled = (operation & 0x80U) ? 1U : 0U;
    return 1;
}
