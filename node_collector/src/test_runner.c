#include "test_runner.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

/* ── LCG random ─────────────────────────────────────────── */
static uint32_t rng_state = 0xDEADBEEF;

static uint32_t lcg_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

uint32_t tr_rand_u32(uint32_t min, uint32_t max)
{
    if (min >= max) return min;
    return min + (lcg_next() % (max - min + 1));
}

uint16_t tr_rand_u16(uint16_t min, uint16_t max)
{
    return (uint16_t)tr_rand_u32(min, max);
}

/* ── Scenario definitions ───────────────────────────────── */
int test_runner_load(test_config_t *cfg)
{
    memset(cfg, 0, sizeof(test_config_t));
    test_scenario_t *s;

    /* ── Scenario 1: Normal production (valori tipici dal dataset) ── */
    s = &cfg->scenarios[0];
    strncpy(s->name, "Normal production", MAX_NAME_LEN);
    s->interval_ms    = 0;
    s->loops          = 5;
    s->state          = 30;
    s->pallet_id      = (range_u32_t){159290, 159307};
    s->wrap_time      = (range_u16_t){400, 780};
    s->wrap_transit   = (range_u16_t){650, 1035};
    s->rotations      = (range_u16_t){13, 25};
    s->program_number = 0;     /* sempre 0 dai dati reali */
    s->perimeter      = 3;     /* sempre 3 dai dati reali */
    s->running_secs   = (range_u32_t){4000, 4000}; /* fisso nei dati */
    s->alarm_secs     = (range_u32_t){0, 0};

    /* ── Scenario 2: Slow pallet (wrap_time alto ma normale) ── */
    s = &cfg->scenarios[1];
    strncpy(s->name, "Slow pallet", MAX_NAME_LEN);
    s->interval_ms    = 0;
    s->loops          = 4;
    s->state          = 30;
    s->pallet_id      = (range_u32_t){159270, 159290};
    s->wrap_time      = (range_u16_t){700, 780};
    s->wrap_transit   = (range_u16_t){950, 1035};
    s->rotations      = (range_u16_t){23, 25};
    s->program_number = 0;
    s->perimeter      = 3;
    s->running_secs   = (range_u32_t){4000, 4000};
    s->alarm_secs     = (range_u32_t){0, 0};

    /* ── Scenario 3: Anomaly (valori anomali reali dal dataset) ── */
    s = &cfg->scenarios[2];
    strncpy(s->name, "Anomaly - pallet stuck", MAX_NAME_LEN);
    s->interval_ms    = 0;
    s->loops          = 3;
    s->state          = 31;
    s->pallet_id      = (range_u32_t){159200, 159270};
    s->wrap_time      = (range_u16_t){18000, 24060}; /* valori anomali reali */
    s->wrap_transit   = (range_u16_t){18200, 24312};
    s->rotations      = (range_u16_t){11, 16};
    s->program_number = 0;
    s->perimeter      = 3;
    s->running_secs   = (range_u32_t){4000, 4000};
    s->alarm_secs     = (range_u32_t){100, 500};

    /* ── Scenario 4: Machine offline ── */
    s = &cfg->scenarios[3];
    strncpy(s->name, "Machine offline", MAX_NAME_LEN);
    s->interval_ms    = 0;
    s->loops          = 2;
    s->state          = 99;
    s->pallet_id      = (range_u32_t){158308, 158308};
    s->wrap_time      = (range_u16_t){0, 0};
    s->wrap_transit   = (range_u16_t){0, 0};
    s->rotations      = (range_u16_t){0, 0};
    s->program_number = 0;
    s->perimeter      = 3;
    s->running_secs   = (range_u32_t){4000, 4000};
    s->alarm_secs     = (range_u32_t){0, 0};

    cfg->count = 4;

    printk("[TEST RUNNER] Loaded %d scenarios\n", cfg->count);
    return 0;
}

/* ── Runner ─────────────────────────────────────────────── */
void test_runner_run(const test_config_t *cfg, tx_callback_t on_tx)
{
    printk("\n[TEST RUNNER] Starting — %d scenarios\n\n", cfg->count);

    uint16_t global_seq = 0;
    uint32_t base_ts    = 1774138000U;

    for (int s = 0; s < cfg->count; s++) {
        const test_scenario_t *sc = &cfg->scenarios[s];

        printk("==============================\n");
        printk("Scenario %d/%d: %s\n", s+1, cfg->count, sc->name);
        printk("  State: %u  Loops: %u  Interval: %ums\n",
               sc->state, sc->loops, sc->interval_ms);
        printk("  Wrap: %u-%us  Rotations: %u-%u\n",
               sc->wrap_time.min, sc->wrap_time.max,
               sc->rotations.min, sc->rotations.max);
        printk("==============================\n\n");

        for (uint32_t loop = 0; loop < sc->loops; loop++) {

            hal_sensor_data_t data;

            data.state              = sc->state;
            data.pallet_id          = tr_rand_u32(sc->pallet_id.min,
                                                   sc->pallet_id.max);
            data.wrap_time          = tr_rand_u16(sc->wrap_time.min,
                                                   sc->wrap_time.max);
            data.wrap_transit_time  = tr_rand_u16(sc->wrap_transit.min,
                                                   sc->wrap_transit.max);
            data.pallet_rotations   = tr_rand_u16(sc->rotations.min,
                                                   sc->rotations.max);
            data.program_number     = sc->program_number;
            data.pallet_perimeter   = sc->perimeter;
            data.running_seconds    = tr_rand_u32(sc->running_secs.min,
                                                   sc->running_secs.max);
            data.alarm_seconds      = tr_rand_u32(sc->alarm_secs.min,
                                                   sc->alarm_secs.max);
            data.machine_timestamp  = base_ts + (uint32_t)global_seq * 60;

            printk("[%s] TX #%u (%u/%u) state=%u pallet=%lu\n",
                   sc->name, global_seq,
                   loop+1, sc->loops,
                   data.state,
                   (unsigned long)data.pallet_id);

            on_tx(&data, global_seq, sc->name);
            global_seq++;

            if (sc->interval_ms > 0) {
                k_msleep(sc->interval_ms);
            }
        }

        printk("[TEST RUNNER] '%s' complete\n\n", sc->name);
    }

    printk("[TEST RUNNER] Done. Total TX: %u\n", global_seq);
}