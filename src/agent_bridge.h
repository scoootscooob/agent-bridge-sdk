/**
 * Agent Hardware Protocol (AHP) SDK — connect hardware to AI agents.
 *
 * A transport-agnostic protocol for hardware devices to self-describe
 * their capabilities (properties, commands, events) so any AI agent can
 * discover, query, control, and react to them.
 *
 * Protocol spec: https://github.com/scoootscooob/agent-bridge-sdk/PROTOCOL.md
 *
 * Usage:
 *   1. Build a manifest with ahp_resource / ahp_property / ahp_command / ahp_event
 *   2. Set callbacks for property reads, writes, and command invocations
 *   3. Call ahp_begin() in setup() after WiFi connects
 *   4. Call ahp_loop() in loop()
 *   5. Call ahp_push_property() or ahp_push_event() when state changes
 */

#ifndef AGENT_BRIDGE_H
#define AGENT_BRIDGE_H

#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------

#define AHP_MAX_RESOURCES       32
#define AHP_MAX_PROPERTIES      16
#define AHP_MAX_COMMANDS        8
#define AHP_MAX_EVENTS          8
#define AHP_MAX_ID_LEN          64
#define AHP_MAX_LABEL_LEN       64
#define AHP_MAX_TYPE_LEN        32
#define AHP_MAX_UNIT_LEN        16
#define AHP_MAX_DESC_LEN        256
#define AHP_MAX_CAPS            8
#define AHP_MAX_VALUE_LEN       256

// ---------------------------------------------------------------------------
// Property types
// ---------------------------------------------------------------------------

typedef enum {
    AHP_TYPE_BOOLEAN = 0,
    AHP_TYPE_INTEGER,
    AHP_TYPE_NUMBER,
    AHP_TYPE_STRING,
} ahp_type_t;

// ---------------------------------------------------------------------------
// Event priorities
// ---------------------------------------------------------------------------

typedef enum {
    AHP_PRIORITY_INFO = 0,
    AHP_PRIORITY_NOTICE,
    AHP_PRIORITY_ALERT,
    AHP_PRIORITY_CRITICAL,
} ahp_priority_t;

// ---------------------------------------------------------------------------
// Property definition
// ---------------------------------------------------------------------------

typedef struct {
    char id[AHP_MAX_ID_LEN];
    ahp_type_t type;
    char label[AHP_MAX_LABEL_LEN];
    char description[AHP_MAX_DESC_LEN];
    char unit[AHP_MAX_UNIT_LEN];
    float minimum;
    float maximum;
    bool has_minimum;
    bool has_maximum;
    bool observable;
    bool writable;
} ahp_property_def_t;

// ---------------------------------------------------------------------------
// Command definition
// ---------------------------------------------------------------------------

typedef struct {
    char id[AHP_MAX_ID_LEN];
    char label[AHP_MAX_LABEL_LEN];
    char description[AHP_MAX_DESC_LEN];
    /** Input schema as JSON string (NULL if no input). */
    const char* input_schema_json;
    bool safe;
    bool idempotent;
    bool confirm_required;
} ahp_command_def_t;

// ---------------------------------------------------------------------------
// Event policy
// ---------------------------------------------------------------------------

typedef struct {
    uint32_t debounce_ms;
    bool wake;
    uint32_t batch_window_ms;
    uint16_t max_rate_per_min;
} ahp_event_policy_t;

// ---------------------------------------------------------------------------
// Event declaration
// ---------------------------------------------------------------------------

typedef struct {
    char id[AHP_MAX_ID_LEN];
    char label[AHP_MAX_LABEL_LEN];
    char description[AHP_MAX_DESC_LEN];
    ahp_priority_t priority;
    ahp_event_policy_t policy;
    /** Data schema as JSON string (NULL if no data). */
    const char* data_schema_json;
} ahp_event_def_t;

// ---------------------------------------------------------------------------
// Resource definition
// ---------------------------------------------------------------------------

typedef struct {
    char id[AHP_MAX_ID_LEN];
    char type[AHP_MAX_TYPE_LEN];
    char label[AHP_MAX_LABEL_LEN];
    char description[AHP_MAX_DESC_LEN];
    char semantic_type[AHP_MAX_TYPE_LEN];

    ahp_property_def_t properties[AHP_MAX_PROPERTIES];
    uint8_t property_count;

    ahp_command_def_t commands[AHP_MAX_COMMANDS];
    uint8_t command_count;

    ahp_event_def_t events[AHP_MAX_EVENTS];
    uint8_t event_count;
} ahp_resource_t;

// ---------------------------------------------------------------------------
// Manifest
// ---------------------------------------------------------------------------

typedef struct {
    ahp_resource_t resources[AHP_MAX_RESOURCES];
    uint8_t resource_count;
} ahp_manifest_t;

// ---------------------------------------------------------------------------
// Callbacks — your firmware implements these
// ---------------------------------------------------------------------------

/** Result of a property read. */
typedef struct {
    char value_json[AHP_MAX_VALUE_LEN];
    bool ok;
} ahp_read_result_t;

/** Result of a property write. */
typedef struct {
    char value_json[AHP_MAX_VALUE_LEN];
    bool ok;
} ahp_write_result_t;

/** Result of a command invocation. */
typedef struct {
    bool ok;
    char output_json[AHP_MAX_VALUE_LEN];
} ahp_invoke_result_t;

/**
 * Called when the host reads a property.
 * Populate out->value_json with the current value (JSON-encoded).
 */
typedef ahp_read_result_t (*ahp_on_read_t)(
    const char* resource_id,
    const char* property_id
);

/**
 * Called when the host writes a property.
 * Apply the new value_json and return the actual applied value.
 */
typedef ahp_write_result_t (*ahp_on_write_t)(
    const char* resource_id,
    const char* property_id,
    const char* value_json
);

/**
 * Called when the host invokes a command.
 * input_json may be NULL if the command has no input.
 */
typedef ahp_invoke_result_t (*ahp_on_invoke_t)(
    const char* resource_id,
    const char* command_id,
    const char* input_json
);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

typedef struct {
    /** Device identifier (must be stable across reboots). */
    const char* device_id;
    /** Human-readable device label. */
    const char* label;
    /** Firmware version string. */
    const char* firmware;

    /**
     * Gateway WebSocket URL, e.g. "ws://192.168.1.100:18789/ahp".
     * If NULL, the SDK discovers the gateway via mDNS (_openclaw._tcp).
     */
    const char* gateway_url;
    /** Auth token (NULL if not required). */
    const char* token;
    /**
     * mDNS service type to scan for when gateway_url is NULL.
     * Default: "_openclaw._tcp" if not set.
     */
    const char* mdns_service;

    /** Device manifest. */
    ahp_manifest_t manifest;

    /** Property read callback. */
    ahp_on_read_t on_read;
    /** Property write callback (NULL if no writable properties). */
    ahp_on_write_t on_write;
    /** Command invoke callback (NULL if no commands). */
    ahp_on_invoke_t on_invoke;

    /** Reconnect interval in ms (default: 5000). */
    uint32_t reconnect_ms;

    /**
     * WiFi provisioning mode. Default: AHP_WIFI_AUTO.
     * AHP_WIFI_AUTO — use stored credentials if available, otherwise start BLE provisioning.
     * AHP_WIFI_PROVISION — always start BLE provisioning (factory reset flow).
     * AHP_WIFI_MANUAL — caller handles WiFi (call WiFi.begin() yourself before ahp_begin).
     */
    uint8_t wifi_mode;

    /**
     * WiFi provisioning pop (proof of possession) string.
     * Printed on the device label / QR code. Prevents neighbors from provisioning your device.
     * Default: "abcdf1234" if not set.
     */
    const char* wifi_pop;
} ahp_config_t;

#define AHP_WIFI_AUTO      0
#define AHP_WIFI_PROVISION 1
#define AHP_WIFI_MANUAL    2

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/**
 * Initialize the AHP device.
 *
 * If wifi_mode is AHP_WIFI_AUTO (default):
 *   - Tries stored WiFi credentials first
 *   - If no stored credentials, starts BLE provisioning
 *   - User scans QR code with ESP provisioning app, sends WiFi credentials
 *   - Once connected, discovers gateway via mDNS and starts AHP session
 *
 * If wifi_mode is AHP_WIFI_MANUAL:
 *   - Caller must connect WiFi before calling ahp_begin()
 *   - SDK skips provisioning and goes straight to gateway discovery
 *
 * Call once in setup().
 */
void ahp_begin(const ahp_config_t* config);

/**
 * Generate a provisioning QR code payload string.
 * Encode this as a QR code on the device packaging.
 * Format: {"ver":"v1","name":"<device_id>","pop":"<pop>","transport":"ble"}
 */
const char* ahp_provisioning_qr_payload(const ahp_config_t* config);

/**
 * Process messages and maintain connection.
 * Call in every loop() iteration.
 */
void ahp_loop(void);

/**
 * Push a property change to the host.
 * Only effective for properties with observable: true.
 * value_json is a JSON-encoded value (e.g. "true", "42", "\"hello\"").
 */
void ahp_push_property(
    const char* resource_id,
    const char* property_id,
    const char* value_json
);

/**
 * Push an event to the host.
 * data_json may be NULL if the event has no data payload.
 * The SDK applies the event's declared policy (debounce, rate limit)
 * before sending — callers don't need to implement policy logic.
 */
void ahp_push_event(
    const char* resource_id,
    const char* event_id,
    ahp_priority_t priority,
    const char* data_json
);

/**
 * Re-announce the manifest (e.g. after discovering new sub-devices).
 * Sends ahp.manifest.update to the host.
 */
void ahp_update_manifest(const ahp_manifest_t* manifest);

/** Returns true if the connection to the host is active. */
bool ahp_connected(void);

/** Stop the session and disconnect. */
void ahp_stop(void);

#ifdef __cplusplus
}
#endif

#endif // AGENT_BRIDGE_H
