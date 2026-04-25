#include "role_election.h"
#include <zephyr/sys/printk.h>

static node_role_t current_role = ROLE_UNSET;

node_role_t role_election_run(void)
{
    printk("[ROLE] Reading SIM_DETECT GPIO...\n");

    /*
     * Legge il ruolo tramite HAL.
     * Adesso: valore simulato impostato con hal_sim_set_role()
     * Dopo:   gpio_pin_get(sim_detect_dev, SIM_DETECT_PIN)
     */
    hal_role_t hal_role = hal_get_role();

    if (hal_role == HAL_ROLE_MASTER) {
        current_role = ROLE_MASTER;
    } else {
        current_role = ROLE_SLAVE;
    }

    printk("[ROLE] Elected: %s\n", role_to_string(current_role));

    if (current_role == ROLE_MASTER) {
        printk("[ROLE] SIM present -> NB-IoT modem will be active\n");
        printk("[ROLE] Responsible for SMS uplink to cloud\n");
    } else {
        printk("[ROLE] No SIM -> LoRa only\n");
        printk("[ROLE] Packets forwarded to master via LoRa\n");
    }

    return current_role;
}

node_role_t role_election_get(void)
{
    return current_role;
}

const char *role_to_string(node_role_t role)
{
    switch (role) {
        case ROLE_MASTER: return "MASTER";
        case ROLE_SLAVE:  return "SLAVE";
        default:          return "UNSET";
    }
}