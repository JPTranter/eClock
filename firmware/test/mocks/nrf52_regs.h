#pragma once
// nrf52_regs.h — fake nRF52 register blocks for host tests.
//
// main.cpp reads NRF_POWER->USBREGSTATUS (getBatteryPercent) and, when
// ECLOCK_CORE_MBED && NRF_TWIM0 are both defined, calls eclock_release_twim_pins()
// from setup(), which WRITES NRF_TWIM0/1->ENABLE and NRF_TWI0/1->ENABLE.
//
// These are real, writable static structs (NOT dangling addresses), so the
// write in eclock_release_twim_pins() is a safe no-op on host. Defining
// NRF_TWIM0 also makes main.cpp's `#if defined(NRF_TWIM0)` branch compile and
// run, mirroring the real mbed core where these symbols exist.

#include <cstdint>

struct NRF_POWER_Type {
    uint32_t USBREGSTATUS;
    uint32_t DCDCEN;
};

struct NRF_TWIM_Type {
    uint32_t ENABLE;
    uint32_t PSELSCL;
    uint32_t PSELSDA;
};

struct NRF_TWI_Type {
    uint32_t ENABLE;
    uint32_t PSELSCL;
    uint32_t PSELSDA;
};

struct NRF_GPIO_Type {
    uint32_t PIN_CNF[48];
};

static NRF_POWER_Type nrf_power_instance = {0};
static NRF_TWIM_Type  nrf_twim0_instance = {0};
static NRF_TWIM_Type  nrf_twim1_instance = {0};
static NRF_TWI_Type   nrf_twi0_instance  = {0};
static NRF_TWI_Type   nrf_twi1_instance  = {0};
static NRF_GPIO_Type  nrf_gpio_instance  = {{0}};

#define NRF_POWER (&nrf_power_instance)
#define NRF_TWIM0 (&nrf_twim0_instance)
#define NRF_TWIM1 (&nrf_twim1_instance)
#define NRF_TWI0  (&nrf_twi0_instance)
#define NRF_TWI1  (&nrf_twi1_instance)
#define NRF_GPIO  (&nrf_gpio_instance)

#define POWER_USBREGSTATUS_VBUSDETECT_Msk (1UL << 0)
