#include "modbus_sim.h"
#include <string.h>
#include <zephyr/sys/printk.h>

static uint16_t sim_regs[MB_NUM_REGS];

void modbus_sim_init(void)
{
    sim_regs[MB_REG_STATE]        = 30;
    sim_regs[MB_REG_PALLET_HIGH]  = (159307 >> 16) & 0xFFFF;
    sim_regs[MB_REG_PALLET_LOW]   = 159307 & 0xFFFF;
    sim_regs[MB_REG_WRAP_TIME]    = 458;      /* valore reale dal dataset */
    sim_regs[MB_REG_WRAP_TRANSIT] = 710;      /* valore reale dal dataset */
    sim_regs[MB_REG_ROTATIONS]    = 15;       /* valore reale dal dataset */
    sim_regs[MB_REG_PROGRAM]      = 0;        /* sempre 0 dai dati reali */
    sim_regs[MB_REG_PERIMETER]    = 3;        /* sempre 3 dai dati reali */
    sim_regs[MB_REG_RUN_HIGH]     = 0;
    sim_regs[MB_REG_RUN_LOW]      = 4000;     /* 4000 dai dati reali */
    sim_regs[MB_REG_ALARM_HIGH]   = 0;
    sim_regs[MB_REG_ALARM_LOW]    = 0;
    sim_regs[MB_REG_TS_HIGH]      = (1774605738U >> 16) & 0xFFFF;
    sim_regs[MB_REG_TS_LOW]       = 1774605738U & 0xFFFF;
}

void modbus_sim_tick(uint16_t seq)
{
    /*
     * Simulates realistic machine activity on each poll cycle.
     * Production: this block does not exist —
     * values are read directly from the PLC via UART Modbus RTU.
     */
    sim_regs[MB_REG_STATE] = ((seq % 20) < 10) ? 30 : 31;

    uint32_t pallet_id = 159307 + (seq / 10);
    sim_regs[MB_REG_PALLET_HIGH] = (pallet_id >> 16) & 0xFFFF;
    sim_regs[MB_REG_PALLET_LOW]  = pallet_id & 0xFFFF;

    sim_regs[MB_REG_WRAP_TIME]    = 450 + (seq % 30);
    sim_regs[MB_REG_WRAP_TRANSIT] = 700 + (seq % 50);
    sim_regs[MB_REG_ROTATIONS]    = 820 + (seq % 20);
    sim_regs[MB_REG_PROGRAM]      = 15;
    sim_regs[MB_REG_PERIMETER]    = 3;

    uint32_t run_s = (uint32_t)seq * 60;
    sim_regs[MB_REG_RUN_HIGH] = (run_s >> 16) & 0xFFFF;
    sim_regs[MB_REG_RUN_LOW]  = run_s & 0xFFFF;

    uint32_t alarm_s = (seq > 5) ? (seq - 5) * 2 : 0;
    sim_regs[MB_REG_ALARM_HIGH] = (alarm_s >> 16) & 0xFFFF;
    sim_regs[MB_REG_ALARM_LOW]  = alarm_s & 0xFFFF;

    uint32_t ts = 1774138000U + (uint32_t)seq * 60;
    sim_regs[MB_REG_TS_HIGH] = (ts >> 16) & 0xFFFF;
    sim_regs[MB_REG_TS_LOW]  = ts & 0xFFFF;
}

int modbus_sim_read_registers(uint16_t *regs, uint8_t count)
{
    if (count > MB_NUM_REGS) return -1;
    memcpy(regs, sim_regs, count * sizeof(uint16_t));
    return 0;
}