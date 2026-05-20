#include "modbus_sim.h"
#include <string.h>
#include <zephyr/sys/printk.h>

static uint16_t sim_regs[MB_NUM_REGS];

/* Injected by CMake at build time via target_compile_definitions() */
#ifndef SIM_BUILD_TS
#define SIM_BUILD_TS 1774138000U
#endif
static uint32_t sim_base_ts = SIM_BUILD_TS;

void modbus_sim_init(void)
{
    printk("[MODBUS SIM] Base timestamp: %u\n", sim_base_ts);

    sim_regs[MB_REG_STATE]        = 30;
    sim_regs[MB_REG_PALLET_HIGH]  = (159307 >> 16) & 0xFFFF;
    sim_regs[MB_REG_PALLET_LOW]   = 159307 & 0xFFFF;
    sim_regs[MB_REG_WRAP_TIME]    = 458;
    sim_regs[MB_REG_WRAP_TRANSIT] = 710;
    sim_regs[MB_REG_ROTATIONS]    = 15;
    sim_regs[MB_REG_PROGRAM]      = 0;
    sim_regs[MB_REG_PERIMETER]    = 3;
    sim_regs[MB_REG_RUN_HIGH]     = 0;
    sim_regs[MB_REG_RUN_LOW]      = 4000;
    sim_regs[MB_REG_ALARM_HIGH]   = 0;
    sim_regs[MB_REG_ALARM_LOW]    = 0;
    sim_regs[MB_REG_TS_HIGH]      = (sim_base_ts >> 16) & 0xFFFF;
    sim_regs[MB_REG_TS_LOW]       = sim_base_ts & 0xFFFF;
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

    uint32_t ts = sim_base_ts + (uint32_t)seq * 60;
    sim_regs[MB_REG_TS_HIGH] = (ts >> 16) & 0xFFFF;
    sim_regs[MB_REG_TS_LOW]  = ts & 0xFFFF;
}

uint32_t modbus_sim_base_ts(void)
{
    return sim_base_ts;
}

int modbus_sim_read_registers(uint16_t *regs, uint8_t count)
{
    if (count > MB_NUM_REGS) return -1;
    memcpy(regs, sim_regs, count * sizeof(uint16_t));
    return 0;
}