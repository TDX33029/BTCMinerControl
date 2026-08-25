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
/* PMBus CLEAR_FAULTS (0x03): clears latched status bits so STATUS_WORD
   reflects the current condition instead of accumulated history. */
uint8_t tps546d24a_clear_faults(const tps546d24a_t *device);
/* Generic register reads for fault diagnosis (STATUS_VOUT 0x7A,
   STATUS_TEMPERATURE 0x7D, STATUS_CML 0x7E, VOUT_COMMAND 0x21, ...). */
uint8_t tps546d24a_read_word_cmd(const tps546d24a_t *device, uint8_t command,
                                 uint16_t *value);
uint8_t tps546d24a_read_byte_cmd(const tps546d24a_t *device, uint8_t command,
                                 uint8_t *value);
/* Configured output setpoint (VOUT_COMMAND, LINEAR16 -> mV). */
uint8_t tps546d24a_read_vout_command_mv(const tps546d24a_t *device,
                                        uint16_t *vout_mv);
/* Bitaxe-style full board configuration for the BM1366 core rail: safe
   setpoints, single-module sync/stack setup and relative OV/UV limits.
   Writes registers at runtime only (never STORE_USER_ALL). Call once at
   probe, before enabling the output. */
uint8_t tps546d24a_apply_board_config(const tps546d24a_t *device,
                                      uint16_t vout_mv);
/* Runtime VOUT_COMMAND change (1000..2000 mV window). */
uint8_t tps546d24a_set_vout_mv(const tps546d24a_t *device, uint16_t vout_mv);
/* Any ULINEAR16 voltage-type register (VOUT_MAX 0x24, VOUT_OV_FAULT_LIMIT
   0x40, VOUT_UV_FAULT_LIMIT 0x44, ...) scaled to mV. */
uint8_t tps546d24a_read_vout16_cmd_mv(const tps546d24a_t *device, uint8_t command,
                                      uint16_t *vout_mv);

#endif
