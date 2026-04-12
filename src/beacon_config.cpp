// beacon_config.cpp — Settings, NVS persistence, GATT validation
#include "beacon_config.hpp"

namespace beacon {

// ---- SettingsManager ----

SettingsManager::SettingsManager(INvsStorage& nvs) : nvs_(nvs) {}

void SettingsManager::read_int(uint16_t id, int& dest) {
    int tmp = 0;
    if (nvs_.read(id, &tmp, sizeof(tmp)) > 0) {
        dest = tmp;
    }
}

void SettingsManager::read_bool(uint16_t id, bool& dest) {
    int tmp = 0;
    if (nvs_.read(id, &tmp, sizeof(tmp)) > 0) {
        dest = (tmp != 0);
    }
}

int SettingsManager::load() {
    // Reset to defaults first
    config_ = BeaconConfig{};

    // Read scalar settings from NVS (failure leaves default)
    read_bool(ID_fmdn_NVS, config_.flag_fmdn);
    read_bool(ID_airtag_NVS, config_.flag_airtag);
    read_int(ID_period_NVS, config_.mult_period);
    read_int(ID_power_NVS, config_.tx_power);
    read_int(ID_changeInterval_NVS, config_.change_interval);
    read_int(ID_status_NVS, config_.status_flags);
    read_int(ID_accel_NVS, config_.accel_threshold);
    read_bool(ID_turnedOn_NVS, config_.turned_on);

    // Time offset is int64_t
    {
        int64_t tmp = 0;
        if (nvs_.read(ID_timeOffset_NVS, &tmp, sizeof(tmp)) > 0) {
            config_.time_offset = tmp;
        }
    }

    // Settings MAC
    nvs_.read(ID_settingsMAC_NVS, config_.settings_mac.data(), config_.settings_mac.size());

    // Auth code
    nvs_.read(ID_auth_NVS, config_.auth_code.data(), config_.auth_code.size());

    // Change interval alignment: round down to multiple of 8
    config_.change_interval = config_.change_interval - (config_.change_interval % 8);

    // Load airtag keys from NVS
    if (nvs_.read(ID_key_NVS, config_.keys.data(), sizeof(config_.keys)) > 0) {
        // Count non-zero keys (BUG FIX: original compared against sizeof
        // which is 1120 bytes, not 40 elements)
        int count = 0;
        for (int i = 0; i < kMaxKeysInMemory; i++) {
            bool all_zero = true;
            for (int j = 0; j < kKeySize; j++) {
                if (config_.keys[static_cast<size_t>(i)][static_cast<size_t>(j)] != 0) {
                    all_zero = false;
                    break;
                }
            }
            if (all_zero) {
                break;
            }
            count++;
        }
        config_.num_keys = count;
    } else {
        config_.num_keys = 0;
    }

    // Load FMDN key (fall back to empty default)
    if (nvs_.read(ID_fmdnKey_NVS, config_.fmdn_key.data(), config_.fmdn_key.size()) <= 0) {
        config_.fmdn_key = {};
    }

    return 0;
}

int SettingsManager::save_field(uint16_t field_id) {
    switch (field_id) {
    case ID_fmdn_NVS: {
        int v = config_.flag_fmdn ? 1 : 0;
        return nvs_.write(field_id, &v, sizeof(v));
    }
    case ID_airtag_NVS: {
        int v = config_.flag_airtag ? 1 : 0;
        return nvs_.write(field_id, &v, sizeof(v));
    }
    case ID_period_NVS:
        return nvs_.write(field_id, &config_.mult_period, sizeof(config_.mult_period));
    case ID_changeInterval_NVS:
        return nvs_.write(field_id, &config_.change_interval, sizeof(config_.change_interval));
    case ID_key_NVS:
        return nvs_.write(field_id, config_.keys.data(), sizeof(config_.keys));
    case ID_auth_NVS:
        return nvs_.write(field_id, config_.auth_code.data(), config_.auth_code.size());
    case ID_power_NVS:
        return nvs_.write(field_id, &config_.tx_power, sizeof(config_.tx_power));
    case ID_fmdnKey_NVS:
        return nvs_.write(field_id, config_.fmdn_key.data(), config_.fmdn_key.size());
    case ID_timeOffset_NVS:
        return nvs_.write(field_id, &config_.time_offset, sizeof(config_.time_offset));
    case ID_settingsMAC_NVS:
        return nvs_.write(field_id, config_.settings_mac.data(), config_.settings_mac.size());
    case ID_status_NVS:
        return nvs_.write(field_id, &config_.status_flags, sizeof(config_.status_flags));
    case ID_accel_NVS:
        return nvs_.write(field_id, &config_.accel_threshold, sizeof(config_.accel_threshold));
    case ID_turnedOn_NVS: {
        int v = config_.turned_on ? 1 : 0;
        return nvs_.write(field_id, &v, sizeof(v));
    }
    default:
        return -1;
    }
}

const BeaconConfig& SettingsManager::config() const {
    return config_;
}

BeaconConfig& SettingsManager::config_mut() {
    return config_;
}

int64_t SettingsManager::get_time(uint32_t uptime_sec) const {
    return config_.time_offset + static_cast<int64_t>(uptime_sec);
}

void SettingsManager::update_time_offset(int64_t new_time, uint32_t uptime_sec) {
    config_.time_offset = new_time - static_cast<int64_t>(uptime_sec);
}

void SettingsManager::save_time(uint32_t uptime_sec) {
    int64_t current = get_time(uptime_sec);
    nvs_.write(ID_timeOffset_NVS, &current, sizeof(current));
}

// ---- GATT validators ----

GattResult validate_field(const uint8_t* buf, size_t len, bool authorized,
                          const GattFieldSpec& spec, int32_t& out) {
    if (len != spec.expected_len) {
        return GattResult::InvalidLength;
    }
    if (!authorized) {
        return GattResult::Unauthorized;
    }

    int32_t value = 0;
    std::memcpy(&value, buf, sizeof(value));

    if (spec.allowed_values != nullptr) {
        bool found = false;
        for (size_t i = 0; i < spec.allowed_count; i++) {
            if (value == spec.allowed_values[i]) {
                found = true;
                break;
            }
        }
        if (!found) {
            return GattResult::OutOfRange;
        }
    } else {
        if (value < spec.min_val || value > spec.max_val) {
            return GattResult::OutOfRange;
        }
    }

    out = value;
    return GattResult::Ok;
}

GattResult validate_auth_code(const uint8_t* buf, size_t len, const uint8_t* stored,
                              size_t code_len) {
    if (len != code_len) {
        return GattResult::InvalidLength;
    }

    // Constant-time comparison
    uint8_t diff = 0;
    for (size_t i = 0; i < code_len; i++) {
        diff |= static_cast<uint8_t>(buf[i] ^ stored[i]);
    }

    return (diff == 0) ? GattResult::Ok : GattResult::Unauthorized;
}

GattResult validate_key_chunk(const uint8_t* /*buf*/, size_t len, bool authorized,
                              int keys_received, int max_keys) {
    if (len != 14) {
        return GattResult::InvalidLength;
    }
    if (!authorized) {
        return GattResult::Unauthorized;
    }
    if (keys_received >= 2 * max_keys) {
        return GattResult::OutOfRange;
    }
    return GattResult::Ok;
}

} // namespace beacon
