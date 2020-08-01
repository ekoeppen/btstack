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
#define ENABLE_BLE
// #define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_CENTRAL
// #define ENABLE_LOG_DEBUG
// #define ENABLE_LOG_ERROR
// #define ENABLE_LOG_INFO
// #define ENABLE_EHCILL
// #define ENABLE_SCO_OVER_HCI

// BTstack configuration. buffers, sizes, ...
#define HCI_ACL_PAYLOAD_SIZE 52

#endif

