#pragma once
#include "hal.h"

typedef enum {
    ROLE_UNSET  = -1,
    ROLE_SLAVE  =  0,
    ROLE_MASTER =  1
} node_role_t;

node_role_t  role_election_run(void);
node_role_t  role_election_get(void);
const char  *role_to_string(node_role_t role);
