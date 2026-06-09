/*
 * hal_sim.cpp — simulation HAL
 * Active when HAL_USE_SIM is defined (see hal_select.h)
 */
#include "hal_select.h"
#ifdef HAL_USE_SIM

#include "hal.h"
#include "store_forward.h"
#include <Arduino.h>
#include <string.h>

static hal_role_t sim_role = HAL_ROLE_MASTER; /* demo: forced MASTER */
static uint16_t   sim_seq  = 0;

static const uint8_t SIM_PACKET[SF_SLOT_SIZE] = {
    0x5B,0xDE,0xB6,0x99,0x9B,0x33,0x97,0x1B,
    0x85,0xA4,0x59,0x2D,0xE3,0x35,0xCA,0x3E,
    0x44,0x75,0x8B,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00
};

int hal_init(void)
{
    sf_init();
    return SG_HAL_OK;
}

int hal_flash_write(const uint8_t *packet, size_t len)
{
    if (len != SF_SLOT_SIZE) return SG_HAL_ERROR;
    return (sf_write(packet) == SF_OK) ? SG_HAL_OK : SG_HAL_FULL;
}

int hal_flash_read(uint8_t *packet, size_t len)
{
    if (len != SF_SLOT_SIZE) return SG_HAL_ERROR;
    return (sf_read(packet) == SF_OK) ? SG_HAL_OK : SG_HAL_EMPTY;
}

int hal_flash_pending(void)
{
    return sf_pending();
}

int hal_radio_rx(uint8_t *packet, size_t len)
{
    if (len < SF_SLOT_SIZE) return SG_HAL_ERROR;
    memcpy(packet, SIM_PACKET, SF_SLOT_SIZE);
    packet[0] ^= (uint8_t)sim_seq;
    sim_seq++;
    return SG_HAL_OK;
}

hal_role_t hal_get_role(void) { return sim_role; }
void hal_sim_set_role(hal_role_t role) { sim_role = role; }

#endif /* HAL_USE_SIM */
