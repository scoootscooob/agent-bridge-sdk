/**
 * OpenClaw Bridge SDK — connect hardware to AI agents.
 *
 * This is the main header. Include it, define your resources, call
 * oc_bridge_begin() in setup(), and oc_bridge_loop() in loop().
 *
 * Protocol reference: openclaw/openclaw src/hardware/bridge-protocol.ts
 */

#ifndef OPENCLAW_BRIDGE_H
#define OPENCLAW_BRIDGE_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Resource types — mirrors BridgeResource / HardwareAdapterResource
// ---------------------------------------------------------------------------

#define OC_MAX_RESOURCES     32
#define OC_MAX_ACTIONS       8
#define OC_MAX_ID_LEN        64
#define OC_MAX_NAME_LEN      64
#define OC_MAX_TYPE_LEN      32
#define OC_MAX_VALUE_LEN     128

typedef struct {
    char id[OC_MAX_ID_LEN];
    char label[OC_MAX_NAME_LEN];
} oc_action_t;

typedef struct {
    char value[OC_MAX_VALUE_LEN];
} oc_state_t;

typedef struct {
    char id[OC_MAX_ID_LEN];
    char name[OC_MAX_NAME_LEN];
    char type[OC_MAX_TYPE_LEN];
    oc_action_t actions[OC_MAX_ACTIONS];
    uint8_t action_count;
    oc_state_t state;
} oc_resource_t;

// ---------------------------------------------------------------------------
// Manifest — announced at connect time
// ---------------------------------------------------------------------------

typedef struct {
    char label[OC_MAX_NAME_LEN];
    char firmware_version[32];
    oc_resource_t resources[OC_MAX_RESOURCES];
    uint8_t resource_count;
} oc_manifest_t;

// ---------------------------------------------------------------------------
// Callbacks — your firmware implements these
// ---------------------------------------------------------------------------

/** Called when the gateway requests the current state of a resource. */
typedef bool (*oc_get_handler_t)(const char* resource_id, oc_state_t* out_state);

/**
 * Called when the gateway invokes an action on a resource.
 * Return true on success. Optionally update out_state with the new state.
 */
typedef bool (*oc_set_handler_t)(
    const char* resource_id,
    const char* action,
    const char* input_json,
    oc_state_t* out_state
);

// ---------------------------------------------------------------------------
// Bridge configuration
// ---------------------------------------------------------------------------

typedef struct {
    /** Gateway WebSocket URL, e.g. "ws://192.168.1.100:18789" */
    const char* gateway_url;
    /** Auth token (NULL if shared-auth local). */
    const char* token;
    /** Manifest describing this bridge's resources. */
    oc_manifest_t manifest;
    /** Callback for hardware.get commands. */
    oc_get_handler_t on_get;
    /** Callback for hardware.set commands. */
    oc_set_handler_t on_set;
    /** Reconnect interval in ms (default: 5000). */
    uint32_t reconnect_ms;
} oc_bridge_config_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/**
 * Initialize and start the bridge connection.
 * Call once in setup() after WiFi is connected.
 */
void oc_bridge_begin(const oc_bridge_config_t* config);

/**
 * Process WebSocket messages and maintain connection.
 * Call in every loop() iteration.
 */
void oc_bridge_loop(void);

/**
 * Push a state change event to the gateway.
 * Call whenever a sensor reading changes or an actuator state updates.
 */
void oc_bridge_push_state(const char* resource_id, const char* value);

/**
 * Push a resource discovered/removed event.
 */
void oc_bridge_push_event(const char* resource_id, const char* kind);

/** Returns true if the WebSocket connection to the gateway is active. */
bool oc_bridge_connected(void);

/** Stop the bridge and disconnect. */
void oc_bridge_stop(void);

#ifdef __cplusplus
}
#endif

#endif // OPENCLAW_BRIDGE_H
