// beacon_state.hpp — Explicit state machine for beacon operation
#ifndef BEACON_STATE_HPP
#define BEACON_STATE_HPP

#include <cstdint>

#include "accel_data.hpp"
#include "beacon_config.hpp"
#include "ihardware.hpp"

namespace beacon {

// ---- State enum ----
enum class State : uint8_t {
    Initializing,
    Broadcasting,
    SettingsMode,
    FirmwareUpload,
    ShuttingDown,
};

// ---- Timer state (replaces global tLast* variables) ----
struct Timers {
    uint32_t time_save = 0;
    uint32_t battery_check = 0;
    uint32_t settings_mode = 0;
    uint32_t max_power = 0;
    uint32_t end_max_power = 0;
    uint32_t airtag_switch = 0;
    uint32_t end_settings = 0;
    uint32_t accel_read = 0;
    uint32_t accel_reset = 0;
};

// ---- Constants (from myboards.h / lis2dw12.h) ----
inline constexpr int kBroadcastIntervalMin = 1500; // 938ms
inline constexpr int kBroadcastIntervalMax = 1590; // 994ms
inline constexpr int kIntervalSettings = 60;
inline constexpr int kSettingsWait = 2;
inline constexpr int kIntervalTimeSave = 3600;
inline constexpr int kIntervalBatteryCheck = 60;
inline constexpr int kIntervalMaxPower = 68;
inline constexpr int kIntervalAccelerometer = 20;
inline constexpr int kBatteryLowVoltage = 2800;
inline constexpr int kSettingsAdvInterval = 400;
inline constexpr int kShutdownNokeys = 300;
inline constexpr int kAccelResetMultiplier = 30;
inline constexpr int kChargeLockDuration = 3600;
inline constexpr int kUvloBadPowerThreshold = 5;

// ---- StateMachine ----
class StateMachine {
  public:
    StateMachine(IHardware& hw, SettingsManager& settings, MovementTracker& accel);

    /// Run the startup sequence (main() lines 677-776).
    void initialize();

    /// Run one iteration of the main loop (main() lines 778-1018).
    void tick();

    /// Current state.
    State state() const { return state_; }

    // Expose internals for testing
    const Timers& timers() const { return timers_; }
    int current_key() const { return current_key_; }
    int bad_power() const { return bad_power_; }
    int charge_lock_counter() const { return charge_lock_counter_; }
    bool broadcasting_airtag() const { return broadcasting_airtag_; }
    bool broadcasting_fmdn() const { return broadcasting_fmdn_; }
    bool broadcasting_settings() const { return broadcasting_settings_; }
    bool broadcasting_anything() const { return broadcasting_anything_; }
    int keys_changes() const { return keys_changes_; }
    int what_in_status() const { return what_in_status_; }
    int last_battery_voltage() const { return last_battery_voltage_; }

    // Allow tests to inject GATT connection/auth state
    void set_connected_gatt(bool v) { connected_gatt_ = v; }
    void set_authorized_gatt(bool v) { authorized_gatt_ = v; }
    void set_pause_upload(bool v) { pause_upload_ = v; }
    void set_needs_reset(bool v) { needs_reset_ = v; }

  private:
    IHardware& hw_;
    SettingsManager& settings_;
    MovementTracker& accel_;

    State state_ = State::Initializing;
    Timers timers_;

    // Broadcast state
    bool broadcasting_anything_ = false;
    bool broadcasting_airtag_ = false;
    bool broadcasting_fmdn_ = false;
    bool broadcasting_settings_ = false;

    // Key rotation
    int current_key_ = 0;
    int keys_changes_ = 0;
    int what_in_status_ = 2;

    // Battery / UVLO
    int last_battery_voltage_ = 0;
    int bad_power_ = 0;
    int charge_lock_counter_ = 0;

    // GATT state (set externally via callbacks in real firmware)
    bool connected_gatt_ = false;
    bool authorized_gatt_ = false;
    bool pause_upload_ = false;
    bool needs_reset_ = false;

    // MAC address derived from current key
    uint8_t ble_addr_[6] = {};

    // Helpers
    void update_battery();
    void update_status();
    void broadcast();
    void handle_settings_mode_exit();
    void handle_battery_check();
    void handle_key_rotation();
    void handle_settings_mode_entry();
    void handle_initial_broadcast();
    void handle_airtag_fmdn_alternation();
    void handle_accelerometer();
    void handle_max_power_burst();
    bool handle_button_longpress();
    bool handle_uvlo_shutdown();
    bool handle_nokeys_shutdown();
    void set_mac_from_current_key();
};

} // namespace beacon

#endif // BEACON_STATE_HPP
