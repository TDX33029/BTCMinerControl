#include "tps546d24a.h"
#include <string.h>

#define PMBUS_VOUT_MODE          0x20U
#define PMBUS_VOUT_COMMAND       0x21U
#define PMBUS_VOUT_MAX           0x24U
#define PMBUS_VOUT_MARGIN_HIGH   0x25U
#define PMBUS_VOUT_MARGIN_LOW    0x26U
#define PMBUS_VOUT_SCALE_LOOP    0x29U
#define PMBUS_VOUT_MIN           0x2BU
#define PMBUS_FREQUENCY_SWITCH   0x33U
#define PMBUS_OPERATION          0x01U
#define PMBUS_ON_OFF_CONFIG      0x02U
#define PMBUS_CLEAR_FAULTS       0x03U
#define PMBUS_VOUT_OV_FAULT_LIMIT 0x40U
#define PMBUS_VOUT_OV_WARN_LIMIT 0x42U
#define PMBUS_VOUT_UV_WARN_LIMIT 0x43U
#define PMBUS_VOUT_UV_FAULT_LIMIT 0x44U
/* TI manufacturer-specific commands (see Bitaxe pmbus_commands.h). */
#define PMBUS_SYNC_CONFIG        0xE4U
#define PMBUS_STACK_CONFIG       0xECU
#define PMBUS_STATUS_WORD        0x79U
#define PMBUS_READ_VOUT          0x8BU
#define PMBUS_READ_IOUT          0x8CU
#define PMBUS_READ_TEMPERATURE_1 0x8DU
#define TPS_I2C_TIMEOUT_MS       25U

/* Board configuration for the BM1366 core rail, modelled on the Bitaxe
   ESP-Miner MAX/GAMMA single-module setup (main/power/TPS546.c,
   write_entire_config). The module's own NVM profile (648 mV setpoint,
   VOUT_SCALE_LOOP=0.5) belongs to a different application and trips the
   OV fault / SYNC fault combination seen in the logs. */
#define TPS_BOARD_ON_OFF_CONFIG  0x1BU  /* PMBus OPERATION controls power */
#define TPS_BOARD_STACK_CONFIG   0x0000U /* single module */
#define TPS_BOARD_SYNC_CONFIG    0x10U  /* single module: SYNC disabled */
#define TPS_BOARD_SWITCH_FREQ_KHZ 650U
#define TPS_BOARD_VOUT_MIN_MV    1000U
#define TPS_BOARD_VOUT_MAX_MV    2000U
/* WARNING: must match the actual VOSNS feedback network on the PCB
   (Bitaxe MAX/GAMMA = 0.25). A wrong value regulates the output to
   vout_mv / scale_loop. */
#define TPS_BOARD_SCALE_LOOP     0.25f
/* Fault/warn limits are fractions relative to VOUT_COMMAND (per Bitaxe
   TPS546.h: "1.25 %/100 above VOUT_COMMAND"). */
#define TPS_LIMIT_OV_FAULT       1.25f
#define TPS_LIMIT_OV_WARN        1.16f
#define TPS_LIMIT_MARGIN_HIGH    1.10f
#define TPS_LIMIT_MARGIN_LOW     0.90f
#define TPS_LIMIT_UV_WARN        0.90f
#define TPS_LIMIT_UV_FAULT       0.75f

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

/* Forward declarations: retry helpers are used by the public enable API
   and are implemented after the PMBus write primitives below. */
static uint8_t write_byte_retry(const tps546d24a_t *device, uint8_t command,
                                uint8_t value);
static uint8_t write_word_retry(const tps546d24a_t *device, uint8_t command,
                                uint16_t value);

/* PMBus words are transferred least-significant byte first. */
static uint8_t write_word(const tps546d24a_t *device, uint8_t command,
                          uint16_t value) {
    uint8_t data[2] = { (uint8_t)(value & 0xFFU),
                        (uint8_t)((value >> 8) & 0xFFU) };
    return HAL_I2C_Mem_Write(device->i2c, (uint16_t)(device->address << 1),
                             command, I2C_MEMADD_SIZE_8BIT, data, 2,
                             TPS_I2C_TIMEOUT_MS) == HAL_OK;
}

/* Encode volts as ULINEAR16 using the device's VOUT_MODE exponent. */
static uint8_t float_to_ulinear16(const tps546d24a_t *device, float volts,
                                  uint16_t *value) {
    uint8_t mode;
    int exponent;
    if (!read_byte(device, PMBUS_VOUT_MODE, &mode)) return 0;
    exponent = (int)(mode & 0x1FU);
    if (exponent & 0x10) exponent -= 0x20;
    /* ULINEAR16: raw = volts / 2^exponent. For the device's usual
       VOUT_MODE exponent of -9 this multiplies volts by 512. */
    while (exponent > 0) { volts /= 2.0f; exponent--; }
    while (exponent < 0) { volts *= 2.0f; exponent++; }
    if (volts < 0.0f) volts = 0.0f;
    if (volts > 65535.0f) volts = 65535.0f;
    *value = (uint16_t)(volts + 0.5f);
    return 1;
}

/* Encode a fraction (e.g. 0.25) as SLINEAR11. */
static uint16_t float_to_slinear11(float value) {
    int exponent = 0;
    int mantissa;
    while (value < 512.0f && exponent > -16) { value *= 2.0f; exponent--; }
    while (value >= 1024.0f && exponent < 15) { value /= 2.0f; exponent++; }
    mantissa = (int)(value + 0.5f);
    if (mantissa > 1023) mantissa = 1023;
    return (uint16_t)((((uint16_t)exponent & 0x1FU) << 11) |
                      ((uint16_t)mantissa & 0x7FFU));
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

    if (enabled) {
        /* Fault responses can latch the rail OFF. The reliable unlock
           sequence is OPERATION OFF -> CLEAR_FAULTS -> OPERATION ON. */
        (void)write_byte_retry(device, PMBUS_OPERATION, 0x00U);
        HAL_Delay(10);
        (void)tps546d24a_clear_faults(device);
        HAL_Delay(10);
    }
    if (!write_byte_retry(device, PMBUS_OPERATION, requested)) return 0;
    HAL_Delay(enabled ? 50U : 20U);
    if (!read_byte(device, PMBUS_OPERATION, &operation)) return 0;
    return ((operation & 0x80U) != 0U) == (enabled != 0U);
}

uint8_t tps546d24a_clear_faults(const tps546d24a_t *device) {
    /* CLEAR_FAULTS carries no data: send the command byte alone. */
    uint8_t command = PMBUS_CLEAR_FAULTS;
    if (device == 0 || device->i2c == 0) return 0;
    return HAL_I2C_Master_Transmit(device->i2c,
                                   (uint16_t)(device->address << 1),
                                   &command, 1,
                                   TPS_I2C_TIMEOUT_MS) == HAL_OK;
}

uint8_t tps546d24a_read_word_cmd(const tps546d24a_t *device, uint8_t command,
                                 uint16_t *value) {
    if (device == 0) return 0;
    return read_word(device, command, value);
}

uint8_t tps546d24a_read_byte_cmd(const tps546d24a_t *device, uint8_t command,
                                 uint8_t *value) {
    if (device == 0) return 0;
    return read_byte(device, command, value);
}

uint8_t tps546d24a_read_vout_command_mv(const tps546d24a_t *device,
                                        uint16_t *vout_mv) {
    if (device == 0 || vout_mv == 0) return 0;
    return tps546d24a_read_vout16_cmd_mv(device, PMBUS_VOUT_COMMAND, vout_mv);
}

/* Write with retry: the device can NACK for a few hundred microseconds
   while applying a previous command (observed after FREQUENCY_SWITCH /
   VOUT_SCALE_LOOP writes). */
static uint8_t write_byte_retry(const tps546d24a_t *device, uint8_t command,
                                uint8_t value) {
    uint8_t attempt;
    for (attempt = 0; attempt < 3U; attempt++) {
        if (write_byte(device, command, value)) return 1;
        HAL_Delay(2);
    }
    return 0;
}

static uint8_t write_word_retry(const tps546d24a_t *device, uint8_t command,
                                uint16_t value) {
    uint8_t attempt;
    for (attempt = 0; attempt < 3U; attempt++) {
        if (write_word(device, command, value)) return 1;
        HAL_Delay(2);
    }
    return 0;
}

static uint8_t float_to_ulinear16_retry(const tps546d24a_t *device, float volts,
                                        uint16_t *value) {
    uint8_t attempt;
    for (attempt = 0; attempt < 3U; attempt++) {
        if (float_to_ulinear16(device, volts, value)) return 1;
        HAL_Delay(2);
    }
    return 0;
}

uint8_t tps546d24a_apply_board_config(const tps546d24a_t *device,
                                      uint16_t vout_mv) {
    uint16_t raw;

    if (device == 0 || device->i2c == 0) return 0;
    if (vout_mv < TPS_BOARD_VOUT_MIN_MV) vout_mv = TPS_BOARD_VOUT_MIN_MV;
    if (vout_mv > TPS_BOARD_VOUT_MAX_MV) vout_mv = TPS_BOARD_VOUT_MAX_MV;

    /* Same register order as Bitaxe TPS546_write_entire_config():
       absolute VOUT registers first (COMMAND/MAX/MIN), then the OV/UV and
       margin limits, which are dimensionless ratios multiplied against
       VOUT_COMMAND by the device. The previous order tried to raise the OV
       ratio before VOUT_COMMAND and the device rejected it with CML
       invalid-data. */
    if (!write_byte_retry(device, PMBUS_OPERATION, 0x00U)) return 1; /* off first */
    HAL_Delay(2);
    tps546d24a_clear_faults(device);
    HAL_Delay(5);
    if (!write_byte_retry(device, PMBUS_ON_OFF_CONFIG, TPS_BOARD_ON_OFF_CONFIG)) return 2;
    if (!write_word_retry(device, PMBUS_STACK_CONFIG, TPS_BOARD_STACK_CONFIG)) return 3;
    if (!write_byte_retry(device, PMBUS_SYNC_CONFIG, TPS_BOARD_SYNC_CONFIG)) return 4;
    if (!write_word_retry(device, PMBUS_FREQUENCY_SWITCH,
                    float_to_slinear11((float)TPS_BOARD_SWITCH_FREQ_KHZ))) return 5;
    if (!write_word_retry(device, PMBUS_VOUT_SCALE_LOOP,
                    float_to_slinear11(TPS_BOARD_SCALE_LOOP))) return 6;
    HAL_Delay(2);

    /* Absolute VOUT setpoints use the VOUT_MODE ULINEAR16 exponent. */
    if (!float_to_ulinear16_retry(device, (float)vout_mv / 1000.0f, &raw)) return 7;
    if (!write_word_retry(device, PMBUS_VOUT_COMMAND, raw)) {
        tps546d24a_clear_faults(device);
        HAL_Delay(10);
        if (!write_word_retry(device, PMBUS_VOUT_COMMAND, raw)) return 7;
    }
    if (!float_to_ulinear16_retry(device, (float)TPS_BOARD_VOUT_MAX_MV / 1000.0f, &raw)) return 8;
    if (!write_word_retry(device, PMBUS_VOUT_MAX, raw)) return 8;
    if (!float_to_ulinear16_retry(device, (float)TPS_BOARD_VOUT_MIN_MV / 1000.0f, &raw)) return 9;
    if (!write_word_retry(device, PMBUS_VOUT_MIN, raw)) return 9;

    /* Fault/margin registers are ratios, not millivolts. */
    if (!float_to_ulinear16_retry(device, TPS_LIMIT_OV_FAULT, &raw)) return 10;
    if (!write_word_retry(device, PMBUS_VOUT_OV_FAULT_LIMIT, raw)) return 10;
    if (!float_to_ulinear16_retry(device, TPS_LIMIT_OV_WARN, &raw)) return 11;
    if (!write_word_retry(device, PMBUS_VOUT_OV_WARN_LIMIT, raw)) return 11;
    if (!float_to_ulinear16_retry(device, TPS_LIMIT_MARGIN_HIGH, &raw)) return 12;
    if (!write_word_retry(device, PMBUS_VOUT_MARGIN_HIGH, raw)) return 12;
    if (!float_to_ulinear16_retry(device, TPS_LIMIT_MARGIN_LOW, &raw)) return 13;
    if (!write_word_retry(device, PMBUS_VOUT_MARGIN_LOW, raw)) return 13;
    if (!float_to_ulinear16_retry(device, TPS_LIMIT_UV_WARN, &raw)) return 14;
    if (!write_word_retry(device, PMBUS_VOUT_UV_WARN_LIMIT, raw)) return 14;
    if (!float_to_ulinear16_retry(device, TPS_LIMIT_UV_FAULT, &raw)) return 15;
    if (!write_word_retry(device, PMBUS_VOUT_UV_FAULT_LIMIT, raw)) return 15;

    tps546d24a_clear_faults(device);
    HAL_Delay(20);   /* allow latched STATUS bits to settle before OPERATION ON */
    return 0;
}

uint8_t tps546d24a_set_vout_mv(const tps546d24a_t *device, uint16_t vout_mv) {
    uint16_t raw;

    if (device == 0 || device->i2c == 0) return 0;
    if (vout_mv < TPS_BOARD_VOUT_MIN_MV || vout_mv > TPS_BOARD_VOUT_MAX_MV) {
        return 0;
    }
    /* Absolute setpoint first, then the ratio registers. */
    if (float_to_ulinear16_retry(device, (float)TPS_BOARD_VOUT_MAX_MV / 1000.0f, &raw)) {
        (void)write_word_retry(device, PMBUS_VOUT_MAX, raw);
    }
    if (!float_to_ulinear16_retry(device, TPS_LIMIT_OV_FAULT, &raw)) return 0;
    if (!write_word_retry(device, PMBUS_VOUT_OV_FAULT_LIMIT, raw)) return 0;
    if (!float_to_ulinear16_retry(device, (float)vout_mv / 1000.0f, &raw)) return 0;
    if (!write_word_retry(device, PMBUS_VOUT_COMMAND, raw)) return 0;
    if (!float_to_ulinear16_retry(device, TPS_LIMIT_UV_WARN, &raw)) return 0;
    if (!write_word_retry(device, PMBUS_VOUT_UV_WARN_LIMIT, raw)) return 0;
    if (!float_to_ulinear16_retry(device, TPS_LIMIT_UV_FAULT, &raw)) return 0;
    if (!write_word_retry(device, PMBUS_VOUT_UV_FAULT_LIMIT, raw)) return 0;
    return 1;
}

uint8_t tps546d24a_read_vout16_cmd_mv(const tps546d24a_t *device, uint8_t command,
                                      uint16_t *vout_mv) {    uint8_t vout_mode;
    uint16_t raw;
    uint32_t scaled;

    if (device == 0 || vout_mv == 0) return 0;
    if (!read_byte(device, PMBUS_VOUT_MODE, &vout_mode)) return 0;
    if (!read_word(device, command, &raw)) return 0;
    scaled = scale_linear16(raw, vout_mode, 1000U);
    if (scaled > 65535U) scaled = 65535U;
    *vout_mv = (uint16_t)scaled;
    return 1;
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
