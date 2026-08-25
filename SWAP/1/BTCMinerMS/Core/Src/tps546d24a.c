#include "tps546d24a.h"
#include <string.h>
#include <stdio.h>

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
#define PMBUS_TON_DELAY            0x60U
#define PMBUS_TON_RISE             0x61U
#define PMBUS_TON_MAX_FAULT_LIMIT  0x62U
#define PMBUS_TON_MAX_FAULT_RESPONSE 0x63U
#define PMBUS_TOFF_DELAY           0x64U
#define PMBUS_TOFF_FALL            0x65U
/* Remaining commands from the Bitaxe reference config (pmbus_commands.h)
   that the vendor module NVM may hold arbitrary values for. */
#define PMBUS_PHASE                0x04U
#define PMBUS_VIN_ON               0x35U
#define PMBUS_VIN_OFF              0x36U
#define PMBUS_IOUT_OC_FAULT_LIMIT  0x46U
#define PMBUS_IOUT_OC_FAULT_RESPONSE 0x47U
#define PMBUS_IOUT_OC_WARN_LIMIT   0x4AU
#define PMBUS_OT_FAULT_LIMIT       0x4FU
#define PMBUS_OT_FAULT_RESPONSE    0x50U
#define PMBUS_OT_WARN_LIMIT        0x51U
#define PMBUS_VIN_OV_FAULT_LIMIT   0x55U
#define PMBUS_VIN_OV_FAULT_RESPONSE 0x56U
#define PMBUS_VIN_UV_WARN_LIMIT    0x58U
#define PMBUS_PIN_DETECT_OVERRIDE  0xEEU
/* TI manufacturer-specific commands (see Bitaxe pmbus_commands.h). */
#define PMBUS_SYNC_CONFIG        0xE4U
#define PMBUS_STACK_CONFIG       0xECU
#define PMBUS_STATUS_WORD        0x79U
#define PMBUS_STATUS_VOUT        0x7AU
#define PMBUS_STATUS_IOUT        0x7BU
#define PMBUS_STATUS_CML         0x7EU
#define PMBUS_VOUT_OV_FAULT_RESPONSE 0x41U
#define PMBUS_VOUT_UV_FAULT_RESPONSE 0x45U
#define PMBUS_IOUT_OC_FAULT_RESPONSE 0x47U
#define PMBUS_STATUS_MFR          0x80U
#define PMBUS_READ_VIN           0x88U
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
/* VIN_ON/OFF writes are disabled until the actual board input rail is
   confirmed. The unmodified NVM VIN profile already worked on this board;
   writing 11 V VIN_ON on a 5 V rail keeps the regulator OFF and the ASICs
   unpowered (BM1366 count == 0). Set to 1 only after checking READ_VIN. */
#define TPS546_CONFIGURE_VIN 0
/* Soft-start timing (Bitaxe TPS546_INIT_TON_* / TOFF_*): a module NVM from
   another application can carry a near-zero TON_RISE; ramping into a loop
   compensated for a different output stage then overshoots the OV limit
   and latches OFF at every start. */
#define TPS_BOARD_TON_RISE_MS    3U
#define TPS_BOARD_TON_MAX_FAULT_RESPONSE 0x3BU
#define TPS_BOARD_VOUT_MIN_MV    1000U
/* With SCALE_LOOP=1.0 the VOUT registers are limited by the internal
   reference ceiling (~1.37 V): 2000 mV here trips VOUT_MIN_MAX and the
   rail never starts. 1300 mV also bounds the OV/margin ratio limits
   below. Rail reads ~1.7 % above the command (0.983 sense ratio). */
#define TPS_BOARD_VOUT_MAX_MV    1300U
/* WARNING: must match the actual VOSNS feedback network on the PCB.
   Measured on this board: VOSNS -[1.37k]- VOUT and VOSNS -[80k]- GOSNS,
   i.e. ratio = 80/(80+1.37) = 0.983 - effectively direct sensing, NOT a
   divider. Scale must therefore be 1.0, and VOUT_MAX must stay inside the
   scale-1.0 internal reference range (~1.37 V ceiling): with VOUT_MAX=2.0 V
   the device flags STATUS_VOUT VOUT_MIN_MAX and never starts.
   (Bitaxe MAX/GAMMA uses 0.25 because its own PCB has a real 4:1 divider -
   different hardware, do not copy the value.)
   NOTE: this only regulates correctly once AGND/GOSNS are tied to GND;
   a floating AGND island corrupts the differential sense and trips VOUT_OV. */
#define TPS_BOARD_SCALE_LOOP     1.0f
/* Single-phase stand-alone module (Bitaxe MAX: PHASE=0x00). */
#define TPS_BOARD_PHASE          0x00U
/* VIN monitoring for the 12 V input rail (Bitaxe 12 V families:
   VIN_ON 11 V, VIN_OFF 10.5 V, VIN_OV fault 14 V, response 0xB7). */
#define TPS_BOARD_VIN_ON_V       11.0f
#define TPS_BOARD_VIN_OFF_V      10.5f
#define TPS_BOARD_VIN_UV_WARN_V  11.0f
#define TPS_BOARD_VIN_OV_FAULT_V 14.0f
#define TPS_BOARD_VIN_OV_FAULT_RESPONSE 0xB7U
/* Core-rail current limits (Bitaxe MAX: warn 25 A, fault 30 A, response
   0xC0 = shutdown, no retry). */
#define TPS_BOARD_IOUT_OC_WARN_A  25.0f
#define TPS_BOARD_IOUT_OC_FAULT_A 30.0f
#define TPS_BOARD_IOUT_OC_FAULT_RESPONSE 0xC0U
/* Temperature limits (Bitaxe: warn 105 C, fault 145 C, 0xFF = retry after
   cooling). */
#define TPS_BOARD_OT_WARN_C       105
#define TPS_BOARD_OT_FAULT_C      145
#define TPS_BOARD_OT_FAULT_RESPONSE 0xFFU
/* Boot config takes phase/sync detection from pins, not registers
   (Bitaxe INIT_PIN_DETECT_OVERRIDE). */
#define TPS_BOARD_PIN_DETECT_OVERRIDE 0xFFFFU
/* Fault/warn limits are absolute volts in the VOUT domain (PMBus spec for
   the VOUT_OV/WARN/UV registers). The Bitaxe header's "%/100 above
   VOUT_COMMAND" comment is a misreading - their values only look like
   ratios because their command is 1.2 V. Constraints: each limit must
   stay above VOUT_COMMAND and inside VOUT_MAX, all within the scale-1.0
   reference ceiling. Values below are the proven Bitaxe set: with
   VOUT_COMMAND=1.2 V the OV fault trips at 1.25 V absolute. */
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

/* Decode an SLINEAR11 word (e.g. VOUT_SCALE_LOOP 0x29) as value x 1000,
   so logs read "0.25" / "1.00" instead of a raw hex word. */
uint16_t tps546d24a_slinear11_x1000(uint16_t raw) {
    int32_t value = scale_linear11(raw, 1000);
    if (value < 0) value = 0;
    if (value > 65535) value = 65535;
    return (uint16_t)value;
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
    if (!write_byte_retry(device, PMBUS_PHASE, TPS_BOARD_PHASE)) return 21;
    if (!write_word_retry(device, PMBUS_FREQUENCY_SWITCH,
                    float_to_slinear11((float)TPS_BOARD_SWITCH_FREQ_KHZ))) return 5;
#if TPS546_CONFIGURE_VIN
    /* VIN monitoring: only write these after the real input rail has been
       measured. A 5 V board with VIN_ON=11 V will never start the ASIC rail. */
    if (!write_word_retry(device, PMBUS_VIN_UV_WARN_LIMIT,
                    float_to_slinear11(TPS_BOARD_VIN_UV_WARN_V))) return 22;
    if (!write_word_retry(device, PMBUS_VIN_ON,
                    float_to_slinear11(TPS_BOARD_VIN_ON_V))) return 23;
    if (!write_word_retry(device, PMBUS_VIN_OFF,
                    float_to_slinear11(TPS_BOARD_VIN_OFF_V))) return 24;
    if (!write_word_retry(device, PMBUS_VIN_OV_FAULT_LIMIT,
                    float_to_slinear11(TPS_BOARD_VIN_OV_FAULT_V))) return 25;
    if (!write_byte_retry(device, PMBUS_VIN_OV_FAULT_RESPONSE,
                    TPS_BOARD_VIN_OV_FAULT_RESPONSE)) return 26;
#endif
    if (!write_word_retry(device, PMBUS_VOUT_SCALE_LOOP,
                    float_to_slinear11(TPS_BOARD_SCALE_LOOP))) return 6;
    HAL_Delay(2);
    /* The device can silently ignore a VOUT_SCALE_LOOP write it rejects,
       and every later VOUT-domain command is interpreted against whatever
       scale is actually active - so verify the register took the value. */
    if (!read_word(device, PMBUS_VOUT_SCALE_LOOP, &raw) ||
        scale_linear11(raw, 1000) !=
            (int32_t)(TPS_BOARD_SCALE_LOOP * 1000.0f + 0.5f)) {
        return 20;
    }

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

    /* Core-rail current and temperature limits - vendor NVM values are
       unknown and can shut the rail down at soft-start. */
    if (!write_word_retry(device, PMBUS_IOUT_OC_WARN_LIMIT,
                    float_to_slinear11(TPS_BOARD_IOUT_OC_WARN_A))) return 27;
    if (!write_word_retry(device, PMBUS_IOUT_OC_FAULT_LIMIT,
                    float_to_slinear11(TPS_BOARD_IOUT_OC_FAULT_A))) return 28;
    if (!write_byte_retry(device, PMBUS_IOUT_OC_FAULT_RESPONSE,
                    TPS_BOARD_IOUT_OC_FAULT_RESPONSE)) return 29;
    if (!write_word_retry(device, PMBUS_OT_WARN_LIMIT,
                    float_to_slinear11((float)TPS_BOARD_OT_WARN_C))) return 30;
    if (!write_word_retry(device, PMBUS_OT_FAULT_LIMIT,
                    float_to_slinear11((float)TPS_BOARD_OT_FAULT_C))) return 31;
    if (!write_byte_retry(device, PMBUS_OT_FAULT_RESPONSE,
                    TPS_BOARD_OT_FAULT_RESPONSE)) return 32;

    /* Timing block, same values as Bitaxe write_entire_config(): 3 ms
       soft-start ramp, TON_MAX fault disabled. TON_DELAY/TOFF_DELAY/
       TOFF_FALL = 0. Without this the module NVM's original soft-start
       timing is active. */
    if (!write_word_retry(device, PMBUS_TON_DELAY, 0x0000U)) return 16;
    if (!write_word_retry(device, PMBUS_TON_RISE,
                    float_to_slinear11((float)TPS_BOARD_TON_RISE_MS))) return 17;
    if (!write_word_retry(device, PMBUS_TON_MAX_FAULT_LIMIT, 0x0000U)) return 18;
    if (!write_byte_retry(device, PMBUS_TON_MAX_FAULT_RESPONSE,
                    TPS_BOARD_TON_MAX_FAULT_RESPONSE)) return 18;
    if (!write_word_retry(device, PMBUS_TOFF_DELAY, 0x0000U)) return 19;
    if (!write_word_retry(device, PMBUS_TOFF_FALL, 0x0000U)) return 19;

    /* Boot-time phase/sync detection follows the PCB strapping, matching
       the single-module stack/sync registers written above. */
    if (!write_word_retry(device, PMBUS_PIN_DETECT_OVERRIDE,
                    TPS_BOARD_PIN_DETECT_OVERRIDE)) return 33;

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

/* Round-4 experiment: with the OV response ignored the rail came up
   (619 mV at 5 ms, 2x the 0.31 V target - so the power stage switches),
   then all status words read back 0x0000 with the rail at 0 mV by
   150 ms - a latch-off would keep its fault bits, so that looks like a
   chip reset or dying I2C. Sustain the run for 5 s with BOTH the OV and
   UV fault responses set to ignore, sample telemetry every 250 ms
   (READ_VOUT / READ_VIN / READ_IOUT + status words + an I2C error flag
   per sample) and read back the response registers to identify which
   write the earlier CML=0x40 complaint was about.
   Round 5: the 5 s run held the rail at 23 mV with the unit OFF even
   though OV+UV were ignored - IOUT_OC_FAULT (response 0xC0 = shutdown
   and latch) was the remaining shutdown source, flagged while
   READ_IOUT reported 0 mA. So the OC response is ignored as well, and
   STATUS_MFR / temperature are sampled to see TI's own fault record.
   Have a DMM on the output caps during the run and note the real VOUT.
   SAFE ONLY WHILE VOUT IS UNLOADED AND UNSHORTED. */
void tps546d24a_diag_hold(const tps546d24a_t *device, uint16_t vout_mv) {
    uint16_t raw;
    uint8_t cml = 0U;
    uint8_t resp = 0U;
    uint8_t k;

    if (device == 0 || device->i2c == 0) return;

    printf("[HOLD] scale 0.25, cmd 1.2 V, OV+UV+OC responses ignored, 5 s run\r\n");
    printf("[HOLD] put a DMM on the output caps NOW and note the real VOUT\r\n");

    (void)write_byte_retry(device, PMBUS_OPERATION, 0x00U);
    HAL_Delay(5U);
    (void)tps546d24a_clear_faults(device);
    HAL_Delay(5U);

    raw = float_to_slinear11(0.25f);
    (void)write_word_retry(device, PMBUS_VOUT_SCALE_LOOP, raw);
    if (float_to_ulinear16_retry(device, 1.0f, &raw))
        (void)write_word_retry(device, PMBUS_VOUT_MIN, raw);
    if (float_to_ulinear16_retry(device, 2.0f, &raw))
        (void)write_word_retry(device, PMBUS_VOUT_MAX, raw);
    if (float_to_ulinear16_retry(device, 1.25f, &raw))
        (void)write_word_retry(device, PMBUS_VOUT_OV_FAULT_LIMIT, raw);
    if (float_to_ulinear16_retry(device, 0.9f, &raw))
        (void)write_word_retry(device, PMBUS_VOUT_UV_WARN_LIMIT, raw);
    if (float_to_ulinear16_retry(device, 0.75f, &raw))
        (void)write_word_retry(device, PMBUS_VOUT_UV_FAULT_LIMIT, raw);
    if (float_to_ulinear16_retry(device, 1.1f, &raw))
        (void)write_word_retry(device, PMBUS_VOUT_MARGIN_HIGH, raw);
    if (float_to_ulinear16_retry(device, 0.9f, &raw))
        (void)write_word_retry(device, PMBUS_VOUT_MARGIN_LOW, raw);
    if (float_to_ulinear16_retry(device, 1.2f, &raw))
        (void)write_word_retry(device, PMBUS_VOUT_COMMAND, raw);
    (void)write_byte_retry(device, PMBUS_VOUT_OV_FAULT_RESPONSE, 0x00U);
    (void)write_byte_retry(device, PMBUS_VOUT_UV_FAULT_RESPONSE, 0x00U);
    (void)write_byte_retry(device, PMBUS_IOUT_OC_FAULT_RESPONSE, 0x00U);

    /* Readbacks identify any rejected write and prove ignore mode. */
    {
        uint8_t vout_mode = 0U;
        if (read_byte(device, PMBUS_VOUT_MODE, &vout_mode))
            printf("[HOLD] VOUT_MODE=0x%02X (0x17 = exp -9, LINEAR)\r\n",
                   (unsigned int)vout_mode);
    }
    if (tps546d24a_read_vout16_cmd_mv(device, PMBUS_VOUT_OV_FAULT_LIMIT, &raw))
        printf("[HOLD] VOUT_OV_FAULT_LIMIT=%u mV\r\n", (unsigned int)raw);
    if (read_byte(device, PMBUS_VOUT_OV_FAULT_RESPONSE, &resp))
        printf("[HOLD] VOUT_OV_FAULT_RESPONSE=0x%02X (0x00 = ignore)\r\n",
               (unsigned int)resp);
    if (read_byte(device, PMBUS_VOUT_UV_FAULT_RESPONSE, &resp))
        printf("[HOLD] VOUT_UV_FAULT_RESPONSE=0x%02X (0x00 = ignore)\r\n",
               (unsigned int)resp);
    if (read_byte(device, PMBUS_IOUT_OC_FAULT_RESPONSE, &resp))
        printf("[HOLD] IOUT_OC_FAULT_RESPONSE=0x%02X (0x00 = ignore)\r\n",
               (unsigned int)resp);
    (void)read_byte(device, PMBUS_STATUS_CML, &cml);
    printf("[HOLD] CML=0x%02X (bit6 = a write above was rejected)\r\n",
           (unsigned int)cml);

    (void)write_byte_retry(device, PMBUS_OPERATION, 0x80U);
    /* 30 s window: enough time to probe VOUT, the VOSNS pin and the
       GOSNS pin with a DMM while the (unloaded) rail is held on. */
    for (k = 0U; k < 60U; k++) {
        uint8_t status_vout = 0U;
        uint8_t status_iout = 0U;
        uint8_t status_mfr = 0U;
        uint16_t status_word = 0U;
        uint16_t rail_mv = 0U;
        uint16_t rail_raw = 0U;
        uint16_t vin_raw = 0U;
        uint16_t iout_raw = 0U;
        uint16_t temp_raw = 0U;
        uint8_t err = 0U;

        HAL_Delay(500U);
        if (!tps546d24a_read_vout16_cmd_mv(device, PMBUS_READ_VOUT, &rail_mv)) err++;
        if (!tps546d24a_read_word_cmd(device, PMBUS_READ_VOUT, &rail_raw)) err++;
        if (!tps546d24a_read_word_cmd(device, PMBUS_READ_VIN, &vin_raw)) err++;
        if (!tps546d24a_read_word_cmd(device, PMBUS_READ_IOUT, &iout_raw)) err++;
        if (!tps546d24a_read_word_cmd(device, PMBUS_READ_TEMPERATURE_1, &temp_raw)) err++;
        if (!read_byte(device, PMBUS_STATUS_VOUT, &status_vout)) err++;
        if (!read_byte(device, PMBUS_STATUS_IOUT, &status_iout)) err++;
        if (!read_byte(device, PMBUS_STATUS_MFR, &status_mfr)) err++;
        if (!read_word(device, PMBUS_STATUS_WORD, &status_word)) err++;
        printf("[HOLD] t=%u ms: rail=%u mV (raw %u) vin=%d mV iout=%d mA temp=%d "
               "VOUT=0x%02X IOUT=0x%02X MFR=0x%02X WORD=0x%04X%s\r\n",
               (unsigned int)((uint32_t)(k + 1U) * 500U),
               (unsigned int)rail_mv, (unsigned int)rail_raw,
               (int)tps546d24a_slinear11_x1000(vin_raw),
               (int)tps546d24a_slinear11_x1000(iout_raw),
               (int)(tps546d24a_slinear11_x1000(temp_raw) / 10),
               (unsigned int)status_vout, (unsigned int)status_iout,
               (unsigned int)status_mfr, (unsigned int)status_word,
               (err != 0U) ? "  <-- I2C-ERR" : "");
    }

    (void)write_byte_retry(device, PMBUS_OPERATION, 0x00U);
    HAL_Delay(5U);
    (void)tps546d24a_clear_faults(device);
    HAL_Delay(5U);
    /* Do not leave the ignore responses in place: restore protective
       responses, then the normal board config. */
    (void)write_byte_retry(device, PMBUS_VOUT_OV_FAULT_RESPONSE, 0xB7U);
    (void)write_byte_retry(device, PMBUS_VOUT_UV_FAULT_RESPONSE, 0xB7U);
    (void)write_byte_retry(device, PMBUS_IOUT_OC_FAULT_RESPONSE, 0xC0U);
    (void)tps546d24a_apply_board_config(device, vout_mv);
}

uint8_t tps546d24a_read_vin_mv(const tps546d24a_t *device, uint16_t *vin_mv) {
    uint16_t raw;
    if (device == 0 || vin_mv == 0) return 0;
    if (!read_word(device, PMBUS_READ_VIN, &raw)) return 0;
    *vin_mv = (uint16_t)scale_linear11(raw, 1000);
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

/* Rapid READ_VOUT sampler for the soft-start window: caches VOUT_MODE
   once, then reads only READ_VOUT back-to-back (no delays), so a
   sub-millisecond overshoot is still caught. The existing probe that
   re-reads VOUT_MODE every sample sees ~1 point per 2.5 ms while
   TON_RISE is only 3 ms - it cannot resolve the trip peak. */
uint8_t tps546d24a_sample_vout_mv_range(const tps546d24a_t *device,
                                        uint16_t samples,
                                        uint16_t *min_mv, uint16_t *max_mv) {
    uint8_t vout_mode;
    uint16_t raw;
    uint32_t lo = 65535U, hi = 0U;

    if (device == 0 || min_mv == 0 || max_mv == 0) return 0;
    if (!read_byte(device, PMBUS_VOUT_MODE, &vout_mode)) return 0;
    while (samples > 0U) {
        if (!read_word(device, PMBUS_READ_VOUT, &raw)) return 0;
        {
            uint32_t scaled = scale_linear16(raw, vout_mode, 1000U);
            if (scaled > 65535U) scaled = 65535U;
            if (scaled < lo) lo = scaled;
            if (scaled > hi) hi = scaled;
        }
        samples--;
    }
    *min_mv = (uint16_t)lo;
    *max_mv = (uint16_t)hi;
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
