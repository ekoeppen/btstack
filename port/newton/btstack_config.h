//
// btstack_config.h for Newton port
//

#ifndef __BTSTACK_CONFIG
#define __BTSTACK_CONFIG

// Port related features
// #define HAVE_EMBEDDED_TICK
#define HAVE_MALLOC
#define HAVE_EMBEDDED_TIME_MS

// BTstack features that can be enabled
#define ENABLE_CLASSIC
// #define ENABLE_BLE
// #define ENABLE_LE_PERIPHERAL
// #define ENABLE_LE_CENTRAL
// #define ENABLE_LOG_DEBUG
// #define ENABLE_LOG_ERROR
// #define ENABLE_LOG_INFO
// #define ENABLE_EHCILL
// #define ENABLE_SCO_OVER_HCI

// BTstack configuration. buffers, sizes, ...
#define HCI_ACL_PAYLOAD_SIZE 52
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_HFP_CONNECTIONS 0
#define MAX_NR_WHITELIST_ENTRIES 1
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_LE_DEVICE_DB_ENTRIES 0

#endif

