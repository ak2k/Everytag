// main.cpp — native_sim entry point for GATT integration tests
//
// Simplified startup: init BLE, register GATT service, start connectable
// advertising immediately. Skips the full StateMachine lifecycle (iBeacon,
// key rotation, settings mode timer) — we just need the GATT service running
// so Bumble can connect and exercise the real write handlers.

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

    // Wire up GATT glue (sets g_settings_manager and g_state_machine pointers
    // used by gatt_glue.c write handlers)
    glue_init(&settings, &sm);

    // Initialize BLE
    int err = hw.bt_enable();
    if (err) {
        printk("BLE init failed (err %d)\n", err);
        return 1;
    }

    // Start connectable advertising immediately (ADV_IND).
    // This is what the production firmware does in settings mode —
    // start_settings_adv() in gatt_glue.c calls bt_le_adv_start with
    // BT_LE_ADV_OPT_CONN, which Bumble's Controller supports.
    printk("Starting connectable settings advertising\n");
    start_settings_adv();

    printk("GATT service ready, entering main loop\n");

    // Service loop: just keep the Zephyr scheduler running so BLE events
    // (connections, GATT writes) are processed. No state machine needed.
    while (true) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
