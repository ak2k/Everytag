// native_sim_hardware.cpp — IHardware for native_sim integration tests
//
// Real BLE: bt_enable, adv_start/stop, start/stop_settings_adv, prepare_airtag/fmdn
// Stubbed: set_mac (no controller), set_tx_power (no VS HCI), GPIO, ADC, WDT, accel, BQ

#include "native_sim_hardware.hpp"

#include <cstring>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "beacon_logic.hpp"
#include "beacon_state.hpp"
#include "ble_glue.h"
#include "gatt_glue.h"

// ---- Advertising lifecycle (mirrors zephyr_hardware.cpp) ----

namespace {

bool adv_is_settings = false;
bool adv_enabled = true;

K_THREAD_STACK_DEFINE(adv_wq_stack_area, 1024);
struct k_work_q adv_wq;
struct k_work adv_restart_work;
bool adv_wq_initialized = false;

void adv_restart_handler(struct k_work*) {
    if (!adv_enabled || !adv_is_settings) {
        return;
    }
    ::start_settings_adv();
}

void adv_wq_init_once() {
    if (adv_wq_initialized) {
        return;
    }
    k_work_queue_init(&adv_wq);
    k_work_queue_start(&adv_wq, adv_wq_stack_area, K_THREAD_STACK_SIZEOF(adv_wq_stack_area),
                       K_PRIO_PREEMPT(7), NULL);
    k_work_init(&adv_restart_work, adv_restart_handler);
    adv_wq_initialized = true;
}

} // anonymous namespace

// Called from gatt_glue.c's .recycled callback
extern "C" void beacon_adv_recycled_cb(void) {
    if (!adv_is_settings) {
        return;
    }
    if (!adv_wq_initialized) {
        adv_wq_init_once();
    }
    k_work_submit_to_queue(&adv_wq, &adv_restart_work);
}

extern "C" void beacon_adv_cancel_sync(void) {
    if (!adv_wq_initialized) {
        return;
    }
    struct k_work_sync sync;
    k_work_cancel_sync(&adv_restart_work, &sync);
}

extern "C" void beacon_adv_set_allowed(int allowed) {
    adv_enabled = (allowed != 0);
}

// ---- Globals needed by bridge functions (zephyr_hardware.cpp provides these
//      in production; we provide them here since we don't compile that file) ----
extern "C" {
int accelThreshold = 800;
}

namespace beacon {

NativeSimHardware::NativeSimHardware(SettingsManager& settings) : settings_(settings) {
    adv_wq_init_once();
}

// ---- Real BLE implementations ----

uint32_t NativeSimHardware::uptime_seconds() {
    return static_cast<uint32_t>(k_uptime_seconds());
}

int NativeSimHardware::bt_enable() {
    int err = ::bt_enable(nullptr);
    if (err == 0) {
        bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
        size_t count = CONFIG_BT_ID_MAX;
        ::bt_id_get(addrs, &count);
        if (count > 0) {
            printk("BT identity: %02X:%02X:%02X:%02X:%02X:%02X\n", addrs[0].a.val[5],
                   addrs[0].a.val[4], addrs[0].a.val[3], addrs[0].a.val[2], addrs[0].a.val[1],
                   addrs[0].a.val[0]);
        }
    }
    return err;
}

void NativeSimHardware::bt_disable() {
    ::bt_disable();
}

int NativeSimHardware::adv_start(bool connectable, int interval_min, int interval_max,
                                  bool use_fmdn) {
    if (connectable) {
        return -EINVAL;
    }
    adv_is_settings = false;
    uint32_t options = BT_LE_ADV_OPT_USE_IDENTITY;
    if (use_fmdn) {
        return ::bt_le_adv_start(BT_LE_ADV_PARAM(options, static_cast<uint32_t>(interval_min),
                                                  static_cast<uint32_t>(interval_max), NULL),
                                 adv_fmdn, ADV_FMDN_COUNT, NULL, 0);
    }
    return ::bt_le_adv_start(BT_LE_ADV_PARAM(options, static_cast<uint32_t>(interval_min),
                                              static_cast<uint32_t>(interval_max), NULL),
                             adv_airtag, ADV_AIRTAG_COUNT, NULL, 0);
}

int NativeSimHardware::adv_stop() {
    int err = ::bt_le_adv_stop();
    adv_is_settings = false;
    return err;
}

int NativeSimHardware::adv_update_airtag() {
    return ::bt_le_adv_update_data(adv_airtag, ADV_AIRTAG_COUNT, NULL, 0);
}

int NativeSimHardware::adv_update_fmdn() {
    return ::bt_le_adv_update_data(adv_fmdn, ADV_FMDN_COUNT, NULL, 0);
}

void NativeSimHardware::prepare_airtag(const uint8_t* key) {
    derive_mac_from_key(key, bleAddr);
    fill_adv_template(key, offline_finding_adv_template, OFFLINE_FINDING_ADV_TEMPLATE_SIZE);
    uint8_t status_save = airtag_data_store[6];
    std::memcpy(airtag_data_store, offline_finding_adv_template, OFFLINE_FINDING_ADV_TEMPLATE_SIZE);
    airtag_data_store[6] = status_save;
    adv_airtag[0].data = airtag_data_store + 2;
}

void NativeSimHardware::prepare_fmdn(const uint8_t* key) {
    uint8_t status_save = fmdn_data_store[23];
    std::memcpy(fmdn_data_store, adv_fmdn[1].data, adv_fmdn[1].data_len);
    std::memcpy(fmdn_data_store + 3, key, 20);
    adv_fmdn[1].data = fmdn_data_store;
    fmdn_data_store[23] = status_save;
}

void NativeSimHardware::start_settings_adv() {
    ::start_settings_adv();
    adv_is_settings = true;
}

void NativeSimHardware::stop_settings_adv() {
    ::stop_settings_adv();
    adv_is_settings = false;
}

void NativeSimHardware::broadcast_ibeacon(int batt_voltage) {
    printk("broadcast_ibeacon: starting (voltage=%d)\n", batt_voltage);
    std::memcpy(iBeacon_data, adv_ibeacon[1].data, adv_ibeacon[1].data_len);
    adv_ibeacon[1].data = iBeacon_data;
    iBeacon_data[24] = static_cast<uint8_t>((batt_voltage + 50) / 100);
    int err = ::bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY, 11200, 12800, NULL),
                                adv_ibeacon, ADV_IBEACON_COUNT, NULL, 0);
    printk("broadcast_ibeacon: bt_le_adv_start returned %d\n", err);
}

void NativeSimHardware::set_status_bytes(uint8_t airtag_status, uint8_t fmdn_status) {
    airtag_data_store[6] = airtag_status;
    fmdn_data_store[23] = fmdn_status;
}

// ---- Stubbed hardware (no real peripherals on native_sim) ----

void NativeSimHardware::set_mac(const uint8_t* addr) {
    // No controller on native_sim — just record the MAC for reference
    std::memcpy(bleAddr, addr, 6);
    printk("set_mac: %02X:%02X:%02X:%02X:%02X:%02X (stub)\n", addr[5], addr[4], addr[3], addr[2],
           addr[1], addr[0]);
}

void NativeSimHardware::set_tx_power(int level) {
    printk("set_tx_power: %d (stub)\n", level);
}

int NativeSimHardware::battery_voltage() {
    return 3700; // simulate normal battery
}

bool NativeSimHardware::is_charging() {
    return false;
}

void NativeSimHardware::wdt_feed() {}

void NativeSimHardware::blink_led(int count, bool fast) {
    (void)count;
    (void)fast;
}

void NativeSimHardware::power_off() {
    printk("power_off (stub — exiting)\n");
    k_sleep(K_FOREVER);
}

void NativeSimHardware::reboot() {
    printk("reboot (stub)\n");
    store_time();
    sys_reboot(SYS_REBOOT_COLD);
}

bool NativeSimHardware::button_pressed() {
    // Return true on first call to simulate startup button press.
    // Without this, turned_on=false (default) causes immediate power_off().
    static bool first_call = true;
    if (first_call) {
        first_call = false;
        return true;
    }
    return false;
}

void NativeSimHardware::sleep_ms(uint32_t ms) {
    k_sleep(K_MSEC(ms));
}

void NativeSimHardware::store_time() {
    settings_.save_time(static_cast<uint32_t>(k_uptime_seconds()));
}

void NativeSimHardware::update_turned_on(bool on) {
    settings_.set_turned_on(on);
    settings_.save_field(ID_turnedOn_NVS);
}

int NativeSimHardware::accel_read() {
    return -1;
}

int NativeSimHardware::accel_init() {
    return -1;
}

int NativeSimHardware::accel_powerdown() {
    return -1;
}

void NativeSimHardware::bq_reinit(bool force) {
    (void)force;
}

void NativeSimHardware::bq_shipmode() {}

} // namespace beacon

// ====================================================================
// GATT glue bridge functions (extern "C", called from gatt_glue.c)
// These are normally in zephyr_hardware.cpp but we don't compile that.
// ====================================================================

extern "C" {

void glue_sync_gatt_state(void* state_machine) {
    auto* sm = static_cast<beacon::StateMachine*>(state_machine);
    if (!sm)
        return;
    sm->set_connected_gatt(connectedGatt != 0);
    sm->set_authorized_gatt(authorizedGatt != 0);
    sm->set_pause_upload(pauseUpload != 0);
    sm->set_needs_reset(needsReset != 0);
}

int beacon_glue_handle_auth(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    const auto& cfg = settings->config();
    if (len != cfg.auth_code.size())
        return -1;
    auto result = beacon::validate_auth_code(buf, len, cfg.auth_code.data(), cfg.auth_code.size());
    if (result == beacon::GattResult::Ok)
        return 1;
    return 0;
}

int beacon_glue_handle_key(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    if (buf == NULL) {
        settings->save_field(beacon::ID_key_NVS);
        return 0;
    }
    if (len != 14)
        return -1;
    extern int keysReceived;
    if (keysReceived >= 2 * beacon::kMaxKeysInMemory)
        return -1;
    int key_idx = keysReceived / 2;
    int half = keysReceived % 2;
    size_t offset = static_cast<size_t>(14 * half);
    settings->set_key_chunk(key_idx, offset, buf, 14);
    if (half == 1)
        settings->set_num_keys(static_cast<uint8_t>(key_idx + 1));
    return 0;
}

int beacon_glue_handle_period(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    int32_t value = 0;
    static const int32_t allowed[] = {1, 2, 4, 8};
    beacon::GattFieldSpec spec = {sizeof(int32_t), 0, 0, allowed, 4};
    auto result = beacon::validate_field(buf, len, true, spec, value);
    if (result == beacon::GattResult::Ok) {
        settings->set_mult_period(static_cast<uint8_t>(value));
        settings->save_field(beacon::ID_period_NVS);
    }
    return static_cast<int>(len);
}

int beacon_glue_handle_fmdn(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    if (len != sizeof(int32_t))
        return static_cast<int>(len);
    int32_t value = 0;
    std::memcpy(&value, buf, sizeof(value));
    settings->set_flag_fmdn(value != 0);
    settings->save_field(beacon::ID_fmdn_NVS);
    return static_cast<int>(len);
}

int beacon_glue_handle_airtag(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    if (len != sizeof(int32_t))
        return static_cast<int>(len);
    int32_t value = 0;
    std::memcpy(&value, buf, sizeof(value));
    settings->set_flag_airtag(value != 0);
    settings->save_field(beacon::ID_airtag_NVS);
    return static_cast<int>(len);
}

int beacon_glue_handle_change_interval(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    int32_t value = 0;
    beacon::GattFieldSpec spec = {sizeof(int32_t), 30, 7200, nullptr, 0};
    auto result = beacon::validate_field(buf, len, true, spec, value);
    if (result == beacon::GattResult::Ok) {
        settings->set_change_interval(static_cast<uint16_t>(value));
        settings->save_field(beacon::ID_changeInterval_NVS);
    }
    return static_cast<int>(len);
}

int beacon_glue_handle_tx_power(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    int32_t value = 0;
    beacon::GattFieldSpec spec = {sizeof(int32_t), 0, 2, nullptr, 0};
    auto result = beacon::validate_field(buf, len, true, spec, value);
    if (result == beacon::GattResult::Ok) {
        settings->set_tx_power(static_cast<uint8_t>(value));
        settings->save_field(beacon::ID_power_NVS);
    }
    return static_cast<int>(len);
}

int beacon_glue_handle_fmdn_key(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    if (len != 20)
        return -1;
    settings->set_fmdn_key(buf, len);
    settings->save_field(beacon::ID_fmdnKey_NVS);
    return static_cast<int>(len);
}

int beacon_glue_handle_time_offset_write(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    if (len != sizeof(int64_t))
        return -1;
    int64_t new_time = 0;
    std::memcpy(&new_time, buf, sizeof(new_time));
    settings->update_time_offset(new_time, static_cast<uint32_t>(k_uptime_seconds()));
    settings->save_field(beacon::ID_timeOffset_NVS);
    return static_cast<int>(len);
}

int beacon_glue_handle_time_offset_read(void* sm, int64_t* out_time) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings || !out_time)
        return -1;
    *out_time = settings->get_time(static_cast<uint32_t>(k_uptime_seconds()));
    return 0;
}

int beacon_glue_handle_settings_mac(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    if (len != 6)
        return -1;
    settings->set_settings_mac(buf, len);
    settings->save_field(beacon::ID_settingsMAC_NVS);
    return static_cast<int>(len);
}

int beacon_glue_handle_status(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    if (len != sizeof(int32_t))
        return -1;
    int32_t value = 0;
    std::memcpy(&value, buf, sizeof(value));
    settings->set_status_flags(static_cast<uint32_t>(value));
    settings->save_field(beacon::ID_status_NVS);
    return static_cast<int>(len);
}

int beacon_glue_handle_accel(void* sm, const uint8_t* buf, uint16_t len) {
    auto* settings = static_cast<beacon::SettingsManager*>(sm);
    if (!settings)
        return -1;
    int32_t value = 0;
    beacon::GattFieldSpec spec = {sizeof(int32_t), 0, 16383, nullptr, 0};
    auto result = beacon::validate_field(buf, len, true, spec, value);
    if (result == beacon::GattResult::Ok) {
        settings->set_accel_threshold(static_cast<uint16_t>(value));
        accelThreshold = static_cast<int>(value);
        settings->save_field(beacon::ID_accel_NVS);
    }
    return static_cast<int>(len);
}

} // extern "C"
