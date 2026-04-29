#ifndef MODBUS_SIM_H
#define MODBUS_SIM_H

#include <stdint.h>

/*
 * Modbus RTU slave simulator — emulates a Technowrapp PLC.
 *
 * Now:  simulated values in RAM
 * Later: real reads via MAX3485 + UART (Modbus FC 0x03)
 *
 * Register map (to be confirmed with Technowrapp):
 *   40001 — machine state code
 *   40002 — pallet_id HIGH word
 *   40003 — pallet_id LOW word
 *   40004 — wrap_time             (seconds)
 *   40005 — wrap_transit_time     (seconds)
 *   40006 — pallet_rotations
 *   40007 — program_number
 *   40008 — pallet_perimeter      (unit TBD with Technowrapp)
 *   40009 — running_seconds HIGH word
 *   40010 — running_seconds LOW word
 *   40011 — alarm_seconds HIGH word
 *   40012 — alarm_seconds LOW word
 *   40013 — timestamp HIGH word
 *   40014 — timestamp LOW word    (Unix epoch)
 */

#define MB_REG_STATE         0   /* 40001 */
#define MB_REG_PALLET_HIGH   1   /* 40002 */
#define MB_REG_PALLET_LOW    2   /* 40003 */
#define MB_REG_WRAP_TIME     3   /* 40004 */
#define MB_REG_WRAP_TRANSIT  4   /* 40005 */
#define MB_REG_ROTATIONS     5   /* 40006 */
#define MB_REG_PROGRAM       6   /* 40007 */
#define MB_REG_PERIMETER     7   /* 40008 */
#define MB_REG_RUN_HIGH      8   /* 40009 */
#define MB_REG_RUN_LOW       9   /* 40010 */
#define MB_REG_ALARM_HIGH   10   /* 40011 */
#define MB_REG_ALARM_LOW    11   /* 40012 */
#define MB_REG_TS_HIGH      12   /* 40013 */
#define MB_REG_TS_LOW       13   /* 40014 */
#define MB_NUM_REGS         14

void modbus_sim_init(void);
int  modbus_sim_read_registers(uint16_t *regs, uint8_t count);
void modbus_sim_tick(uint16_t seq);

#endif /* MODBUS_SIM_H */