// gatt_glue.h -- GATT service and connection callbacks (C interface)
#ifndef GATT_GLUE_H
#define GATT_GLUE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the glue layer with pointers to C++ objects.
// settings_manager: pointer to beacon::SettingsManager
// state_machine:    pointer to beacon::StateMachine
void glue_init(void* settings_manager, void* state_machine);

// Start settings advertisement (BLE connectable mode)
void start_settings_adv(void);

// Stop settings advertisement, disconnect all connections
void stop_settings_adv(void);

// ---- GATT connection state (readable from C++) ----
extern int connectedGatt;
extern int authorizedGatt;
extern int allowedChange;
extern int needsReset;
extern int pauseUpload;

// Synchronize GATT state into the StateMachine.
// Called by the state machine before settings mode checks.
void glue_sync_gatt_state(void* state_machine);

// ---- Bridge functions (implemented in C++, called from GATT callbacks) ----

// Returns the number of bytes accepted, or negative for GATT error.
int beacon_glue_handle_auth(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_key(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_period(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_fmdn(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_airtag(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_change_interval(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_tx_power(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_fmdn_key(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_time_offset_write(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_time_offset_read(void* sm, int64_t* out_time);
int beacon_glue_handle_settings_mac(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_status(void* sm, const uint8_t* buf, uint16_t len);
int beacon_glue_handle_accel(void* sm, const uint8_t* buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // GATT_GLUE_H
