#ifndef MODBUS_SIM_H
#define MODBUS_SIM_H

#include <stdint.h>

/*
 * Simulatore Modbus RTU slave
 *
 * Emula i registri di un PLC Technowrapp.
 * Adesso: valori simulati in RAM
 * Dopo:   lettura reale via MAX3485 + UART
 *
 * Mappa registri (da confermare con Technowrapp):
 *   40001 — temperatura motore (x10, es. 235 = 23.5 C)
 *   40002 — umidita' (x2, es. 130 = 65.0 %)
 *   40003 — vibrazione (x64, es. 80 = 1.25 g)
 *   40004 — pressione HIGH word (offset -300, x16)
 *   40005 — pressione LOW word
 *   40006 — cicli PLC HIGH word
 *   40007 — cicli PLC LOW word (cicli = HIGH<<16 | LOW)
 *   40008 — ore funzionamento
 *   40009 — status flags (bit 0 = running, bit 1 = alarm)
 */

#define MB_REG_TEMP        0   /* 40001 */
#define MB_REG_HUMIDITY    1   /* 40002 */
#define MB_REG_VIBRATION   2   /* 40003 */
#define MB_REG_PRES_HIGH   3   /* 40004 */
#define MB_REG_PRES_LOW    4   /* 40005 */
#define MB_REG_CYCLES_HIGH 5   /* 40006 */
#define MB_REG_CYCLES_LOW  6   /* 40007 */
#define MB_REG_HOURS       7   /* 40008 */
#define MB_REG_STATUS      8   /* 40009 */
#define MB_NUM_REGS        9

/* Init: popola i registri con valori iniziali */
void modbus_sim_init(void);

/*
 * Simula un poll Modbus — legge i registri e li aggiorna.
 * Adesso: incrementa i valori ad ogni chiamata
 * Dopo:   UART TX Function Code 0x03 + RX response
 */
int modbus_sim_read_registers(uint16_t *regs, uint8_t count);

/* Aggiorna i registri simulati (simula variazioni macchina) */
void modbus_sim_tick(uint16_t seq);

#endif