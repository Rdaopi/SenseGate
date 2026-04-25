#include "modbus_sim.h"
#include <string.h>
#include <zephyr/sys/printk.h>

static uint16_t sim_regs[MB_NUM_REGS];

void modbus_sim_init(void)
{
    /*
     * Valori iniziali che simulano una macchina
     * Technowrapp in funzionamento normale.
     */
    sim_regs[MB_REG_TEMP]        = 235;    /* 23.5 C */
    sim_regs[MB_REG_HUMIDITY]    = 130;    /* 65.0 % */
    sim_regs[MB_REG_VIBRATION]   = 80;     /* 1.25 g */
    sim_regs[MB_REG_PRES_HIGH]   = 114;    /* pressione HIGH */
    sim_regs[MB_REG_PRES_LOW]    = 0;      /* pressione LOW */
    sim_regs[MB_REG_CYCLES_HIGH] = 1;      /* cicli HIGH */
    sim_regs[MB_REG_CYCLES_LOW]  = 0x86A0; /* cicli LOW → 100000 totale */
    sim_regs[MB_REG_HOURS]       = 500;    /* ore */
    sim_regs[MB_REG_STATUS]      = 0x01;   /* running */

    printk("[MODBUS SIM] Slave initialized with %d registers\n",
           MB_NUM_REGS);
}

void modbus_sim_tick(uint16_t seq)
{
    /*
     * Simula variazioni realistiche della macchina
     * ad ogni ciclo di polling.
     *
     * In produzione questo blocco non esiste —
     * i valori vengono letti dal PLC reale via UART.
     */

    /* Temperatura oscilla tra 20.0 e 35.0 C */
    sim_regs[MB_REG_TEMP] = 200 + (seq % 150);

    /* Umidita' varia lentamente */
    sim_regs[MB_REG_HUMIDITY] = 120 + (seq % 40);

    /* Vibrazione aumenta leggermente col tempo */
    sim_regs[MB_REG_VIBRATION] = 64 + (seq % 128);

    /* Pressione stabile con piccole variazioni */
    uint32_t pres_raw = (uint32_t)((1013.0f - 300.0f) * 16.0f)
                        + (seq % 10);
    sim_regs[MB_REG_PRES_HIGH] = (pres_raw >> 16) & 0xFFFF;
    sim_regs[MB_REG_PRES_LOW]  = pres_raw & 0xFFFF;

    /* Cicli PLC — incrementa sempre */
    uint32_t cycles = 100000 + seq;
    sim_regs[MB_REG_CYCLES_HIGH] = (cycles >> 16) & 0xFFFF;
    sim_regs[MB_REG_CYCLES_LOW]  = cycles & 0xFFFF;

    /* Ore — incrementa ogni 60 poll (simula 1 ora = 60 x 35min) */
    sim_regs[MB_REG_HOURS] = 500 + (seq / 60);

    /* Status: running sempre, alarm se vibrazione alta */
    sim_regs[MB_REG_STATUS] = 0x01;
    if (sim_regs[MB_REG_VIBRATION] > 160) {
        sim_regs[MB_REG_STATUS] |= 0x02;   /* bit 1 = alarm */
    }
}

int modbus_sim_read_registers(uint16_t *regs, uint8_t count)
{
    if (count > MB_NUM_REGS) {
        return -1;
    }

    /*
     * Simula il comportamento di:
     *   TX: device_addr(1) + FC(0x03) + start_reg(2) + count(2) + CRC(2)
     *   RX: device_addr(1) + FC(0x03) + byte_count(1) + data(N*2) + CRC(2)
     *
     * Adesso: copia direttamente da sim_regs
     * Dopo:   UART write + read con timeout e CRC check
     */
    memcpy(regs, sim_regs, count * sizeof(uint16_t));
    return 0;
}