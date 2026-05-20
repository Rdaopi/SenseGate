#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdint.h>
#include "hal.h"

#define MAX_SCENARIOS   8
#define MAX_NAME_LEN    32

typedef struct {
    uint32_t min;
    uint32_t max;
} range_u32_t;

typedef struct {
    uint16_t min;
    uint16_t max;
} range_u16_t;

typedef struct {
    char        name[MAX_NAME_LEN];
    uint32_t    interval_ms;   /* pausa tra TX in ms */
    uint32_t    loops;         /* numero TX per scenario */

    uint8_t     state;         /* fisso — stato macchina */
    range_u32_t pallet_id;     /* range ID pallet */
    range_u16_t wrap_time;     /* range secondi avvolimento */
    range_u16_t wrap_transit;  /* range secondi transito */
    range_u16_t rotations;     /* range rotazioni pallet */
    uint8_t     program_number;/* fisso */
    uint16_t    perimeter;     /* fisso — unità TBD */
    range_u32_t running_secs;  /* range secondi marcia */
    range_u32_t alarm_secs;    /* range secondi allarme */
} test_scenario_t;

typedef struct {
    test_scenario_t scenarios[MAX_SCENARIOS];
    int             count;
} test_config_t;

typedef void (*tx_callback_t)(const hal_sensor_data_t *data,
                               uint16_t seq,
                               const char *scenario_name);

/* SIM_TX_INTERVAL_MS: ms between packets (injected by CMake, default 5 min) */
#ifndef SIM_TX_INTERVAL_MS
#define SIM_TX_INTERVAL_MS 300000U
#endif

int  test_runner_load(test_config_t *cfg);
/* Runs scenarios once then loops forever, sleeping SIM_TX_INTERVAL_MS between packets */
void test_runner_run(const test_config_t *cfg, tx_callback_t on_tx);

uint32_t tr_rand_u32(uint32_t min, uint32_t max);
uint16_t tr_rand_u16(uint16_t min, uint16_t max);

#endif