/**
 * Agent Hardware Protocol — Blink Example
 *
 * The simplest agent-native device: one LED with typed properties and commands.
 * Any AI agent that speaks AHP can discover and control it.
 *
 * The LED has:
 *   - Property "on" (boolean, observable, writable) — read/write power state
 *   - Command "turn_on" (idempotent) — turn the LED on
 *   - Command "turn_off" (idempotent) — turn the LED off
 *   - Command "toggle" — flip the current state
 *
 * Wiring: LED on GPIO 2 (built-in on most ESP32 boards).
 */

#include <agent_bridge.h>
// No WiFi.h needed — SDK handles WiFi provisioning and connection.
// No gateway URL needed — discovered automatically via mDNS.

const int LED_PIN = 2;
static bool led_on = false;

// --- Property read callback ---
ahp_read_result_t on_read(const char* resource_id, const char* property_id) {
    ahp_read_result_t result = { .ok = false };
    if (strcmp(resource_id, "led") != 0) return result;
    if (strcmp(property_id, "on") == 0) {
        strncpy(result.value_json, led_on ? "true" : "false", AHP_MAX_VALUE_LEN);
        result.ok = true;
    }
    return result;
}

// --- Property write callback ---
ahp_write_result_t on_write(const char* resource_id, const char* property_id, const char* value_json) {
    ahp_write_result_t result = { .ok = false };
    if (strcmp(resource_id, "led") != 0) return result;
    if (strcmp(property_id, "on") == 0) {
        led_on = (strcmp(value_json, "true") == 0);
        digitalWrite(LED_PIN, led_on ? HIGH : LOW);
        strncpy(result.value_json, led_on ? "true" : "false", AHP_MAX_VALUE_LEN);
        result.ok = true;
        // Push the state change to the host
        ahp_push_property("led", "on", led_on ? "true" : "false");
    }
    return result;
}

// --- Command invoke callback ---
ahp_invoke_result_t on_invoke(const char* resource_id, const char* command_id, const char* input_json) {
    ahp_invoke_result_t result = { .ok = false };
    if (strcmp(resource_id, "led") != 0) return result;

    if (strcmp(command_id, "turn_on") == 0) {
        led_on = true;
    } else if (strcmp(command_id, "turn_off") == 0) {
        led_on = false;
    } else if (strcmp(command_id, "toggle") == 0) {
        led_on = !led_on;
    } else {
        return result;
    }

    digitalWrite(LED_PIN, led_on ? HIGH : LOW);
    result.ok = true;
    // Push the state change
    ahp_push_property("led", "on", led_on ? "true" : "false");
    return result;
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // --- Build the manifest ---
    // No WiFi setup needed — SDK handles provisioning via BLE.
    // First boot: user scans QR code, sends WiFi creds from phone.
    // Subsequent boots: stored credentials used automatically.
    static ahp_config_t config = {};
    config.device_id = "esp32-blink-01";
    config.label = "ESP32 Blink";
    config.firmware = "1.0.0";
    config.gateway_url = NULL;   // Auto-discover gateway via mDNS
    config.wifi_mode = AHP_WIFI_AUTO;  // BLE provisioning if no stored creds
    config.on_read = on_read;
    config.on_write = on_write;
    config.on_invoke = on_invoke;

    // One resource: the LED
    ahp_resource_t* led = &config.manifest.resources[0];
    config.manifest.resource_count = 1;
    strncpy(led->id, "led", AHP_MAX_ID_LEN);
    strncpy(led->type, "light", AHP_MAX_TYPE_LEN);
    strncpy(led->label, "Built-in LED", AHP_MAX_LABEL_LEN);
    strncpy(led->description, "On-board LED, can be toggled on and off", AHP_MAX_DESC_LEN);

    // One property: "on" (boolean, observable, writable)
    led->property_count = 1;
    ahp_property_def_t* prop = &led->properties[0];
    strncpy(prop->id, "on", AHP_MAX_ID_LEN);
    prop->type = AHP_TYPE_BOOLEAN;
    strncpy(prop->label, "Power", AHP_MAX_LABEL_LEN);
    prop->observable = true;
    prop->writable = true;

    // Three commands
    led->command_count = 3;
    ahp_command_def_t* c;

    c = &led->commands[0];
    strncpy(c->id, "turn_on", AHP_MAX_ID_LEN);
    strncpy(c->label, "Turn on", AHP_MAX_LABEL_LEN);
    c->idempotent = true;

    c = &led->commands[1];
    strncpy(c->id, "turn_off", AHP_MAX_ID_LEN);
    strncpy(c->label, "Turn off", AHP_MAX_LABEL_LEN);
    c->idempotent = true;

    c = &led->commands[2];
    strncpy(c->id, "toggle", AHP_MAX_ID_LEN);
    strncpy(c->label, "Toggle", AHP_MAX_LABEL_LEN);
    strncpy(c->description, "Flip the LED state", AHP_MAX_DESC_LEN);

    ahp_begin(&config);
}

void loop() {
    ahp_loop();
}
