/**
 * Agent Bridge SDK — connect hardware to AI agents.
 *
 * A generic protocol for hardware devices to announce resources, handle
 * commands, and push state changes to any compatible agent gateway.
 *
 * Include this header, define your resources, call ab_begin() in setup(),
 * and ab_loop() in loop().
 *
 * Protocol spec: https://github.com/scoootscooob/agent-bridge-sdk
 */

#ifndef AGENT_BRIDGE_H
#define AGENT_BRIDGE_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Resource types
// ---------------------------------------------------------------------------

#define AB_MAX_RESOURCES     32
#define AB_MAX_ACTIONS       8
#define AB_MAX_ID_LEN        64
#define AB_MAX_NAME_LEN      64
#define AB_MAX_TYPE_LEN      32
#define AB_MAX_VALUE_LEN     128

typedef struct {
    char id[AB_MAX_ID_LEN];
    char label[AB_MAX_NAME_LEN];
} ab_action_t;

typedef struct {
    char value[AB_MAX_VALUE_LEN];
} ab_state_t;

typedef struct {
    char id[AB_MAX_ID_LEN];
    char name[AB_MAX_NAME_LEN];
    char type[AB_MAX_TYPE_LEN];
    ab_action_t actions[AB_MAX_ACTIONS];
    uint8_t action_count;
    ab_state_t state;
} ab_resource_t;

// ---------------------------------------------------------------------------
// Manifest — announced at connect time
// ---------------------------------------------------------------------------

typedef struct {
    char label[AB_MAX_NAME_LEN];
    char firmware_version[32];
    ab_resource_t resources[AB_MAX_RESOURCES];
    uint8_t resource_count;
} ab_manifest_t;

// ---------------------------------------------------------------------------
// Callbacks — your firmware implements these
// ---------------------------------------------------------------------------

/** Called when the gateway requests the current state of a resource. */
typedef bool (*ab_get_handler_t)(const char* resource_id, ab_state_t* out_state);

/**
 * Called when the gateway invokes an action on a resource.
 * Return true on success. Optionally update out_state with the new state.
 */
typedef bool (*ab_set_handler_t)(
    const char* resource_id,
    const char* action,
    const char* input_json,
    ab_state_t* out_state
);

// ---------------------------------------------------------------------------
// Bridge configuration
// ---------------------------------------------------------------------------

typedef struct {
    /** Gateway WebSocket URL, e.g. "ws://192.168.1.100:18789" */
    const char* gateway_url;
    /** Auth token (NULL if not required). */
    const char* token;
    /** Manifest describing this bridge's resources. */
    ab_manifest_t manifest;
    /** Callback for get commands. */
    ab_get_handler_t on_get;
    /** Callback for set/action commands. */
    ab_set_handler_t on_set;
    /** Reconnect interval in ms (default: 5000). */
    uint32_t reconnect_ms;
} ab_config_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/**
 * Initialize and start the bridge connection.
 * Call once in setup() after WiFi is connected.
 */
void ab_begin(const ab_config_t* config);

/**
 * Process WebSocket messages and maintain connection.
 * Call in every loop() iteration.
 */
void ab_loop(void);

/**
 * Push a state change event to the gateway.
 * Call whenever a sensor reading changes or an actuator state updates.
 */
void ab_push_state(const char* resource_id, const char* value);

/**
 * Push a resource discovered/removed event.
 */
void ab_push_event(const char* resource_id, const char* kind);

/** Returns true if the WebSocket connection to the gateway is active. */
bool ab_connected(void);

/** Stop the bridge and disconnect. */
void ab_stop(void);

#ifdef __cplusplus
}
#endif

#endif // AGENT_BRIDGE_H
