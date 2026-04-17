// main.cpp — native_sim entry point for GATT integration tests
//
// Mirrors production main.cpp but uses NativeSimHardware (no GPIO/ADC/WDT)
// and skips hardware-specific init. The real GATT service (gatt_glue.c),
// SettingsManager, and StateMachine run exactly as in production.

#include "accel_data.hpp"
#include "beacon_config.hpp"
#include "beacon_state.hpp"
#include "native_sim_hardware.hpp"
#include "zephyr_nvs.hpp"

extern "C" {
#include "gatt_glue.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
}

int main(void) {
    static beacon::ZephyrNvsStorage nvs;
    static beacon::SettingsManager settings(nvs);
    static beacon::MovementTracker accel;
    static beacon::NativeSimHardware hw(settings);
    static beacon::StateMachine sm(hw, settings, accel);

    printk("Native GATT test firmware starting\n");

    if (nvs.init() != 0) {
        printk("Warning: NVS init failed, using defaults\n");
    }
    if (settings.load() != 0) {
        printk("Warning: settings load failed, using defaults\n");
    }

    glue_init(&settings, &sm);
    sm.initialize();

    while (true) {
        glue_sync_gatt_state(&sm);
        sm.tick();
    }

    return 0;
}
