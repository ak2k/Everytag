// ihardware.hpp — Hardware abstraction interface for StateMachine
#ifndef IHARDWARE_HPP
#define IHARDWARE_HPP

#include <cstdint>

namespace beacon {

class IHardware {
  public:
    virtual uint32_t uptime_seconds() = 0;
    virtual int bt_enable() = 0;
    virtual void bt_disable() = 0;
    virtual int adv_start(bool connectable, int interval_min, int interval_max) = 0;
    virtual int adv_stop() = 0;
    virtual int adv_update_airtag() = 0;
    virtual int adv_update_fmdn() = 0;
    virtual void set_mac(const uint8_t* addr) = 0;
    virtual void set_tx_power(int level) = 0;
    virtual int battery_voltage() = 0;
    virtual bool is_charging() = 0;
    virtual void wdt_feed() = 0;
    virtual void blink_led(int count, bool fast) = 0;
    virtual void power_off() = 0;
    virtual void reboot() = 0;
    virtual bool button_pressed() = 0;
    virtual void sleep_ms(uint32_t ms) = 0;
    virtual void store_time() = 0;
    virtual void prepare_airtag(const uint8_t* key) = 0;
    virtual void prepare_fmdn(const uint8_t* key) = 0;
    virtual void start_settings_adv() = 0;
    virtual void stop_settings_adv() = 0;
    virtual void broadcast_ibeacon(int battery_voltage) = 0;
    virtual int accel_read() = 0;
    virtual int accel_init() = 0;
    virtual int accel_powerdown() = 0;
    virtual void bq_reinit(bool force) = 0;
    virtual void bq_shipmode() = 0;
    virtual void update_turned_on(bool on) = 0;

  protected:
    ~IHardware() = default;
};

} // namespace beacon

#endif // IHARDWARE_HPP
