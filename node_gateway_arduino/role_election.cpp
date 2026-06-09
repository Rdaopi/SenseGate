#include "role_election.h"
#include <Arduino.h>

/* RAK4631 on RAK19007: SIM_DETECT is on WisBlock IO pin */
#define SIM_DETECT_PIN  17   /* P0.17 = WisBlock IO1 on RAK19007 */

static node_role_t current_role = ROLE_UNSET;

node_role_t role_election_run(void)
{
    pinMode(SIM_DETECT_PIN, INPUT);
    int val = digitalRead(SIM_DETECT_PIN);

    current_role = (val == HIGH) ? ROLE_MASTER : ROLE_SLAVE;

    Serial.print("[ROLE] SIM_DETECT=");
    Serial.print(val);
    Serial.print(" -> ");
    Serial.println(role_to_string(current_role));

    if (current_role == ROLE_MASTER) {
        Serial.println("[ROLE] SIM present -> NB-IoT modem will be active");
    } else {
        Serial.println("[ROLE] No SIM -> LoRa relay only");
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
