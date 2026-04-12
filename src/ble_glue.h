// ble_glue.h -- BLE advertisement data arrays shared between C and C++
#ifndef BLE_GLUE_H
#define BLE_GLUE_H

#include <zephyr/bluetooth/bluetooth.h>

#ifdef __cplusplus
extern "C" {
#endif

// Advertisement data arrays (defined in ble_glue.c)
extern struct bt_data adv_airtag[];
extern struct bt_data adv_fmdn[];
extern struct bt_data adv_ibeacon[];

// Sizes for data arrays
#define OFFLINE_FINDING_ADV_TEMPLATE_SIZE 31
#define ADV_AIRTAG_COUNT 1
#define ADV_FMDN_COUNT 2
#define ADV_IBEACON_COUNT 2

// Raw data stores (writable from C++)
extern uint8_t airtag_data_store[OFFLINE_FINDING_ADV_TEMPLATE_SIZE];
extern uint8_t fmdn_data_store[25];
extern uint8_t iBeacon_data[32];
extern uint8_t bleAddr[6];

// AirTag advertisement template
extern uint8_t offline_finding_adv_template[OFFLINE_FINDING_ADV_TEMPLATE_SIZE];

#ifdef __cplusplus
}
#endif

#endif // BLE_GLUE_H
