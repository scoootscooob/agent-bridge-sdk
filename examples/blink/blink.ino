/**
 * OpenClaw Bridge SDK — Blink Example
 *
 * The simplest agent-native device: one LED, two actions (turn_on, turn_off).
 * The AI agent discovers this device and can control it.
 *
 * Wiring: LED on GPIO 2 (built-in on most ESP32 boards).
 */

#include <WiFi.h>
#include <openclaw_bridge.h>

// -- Config ------------------------------------------------------------------

const char* WIFI_SSID     = "your-wifi-ssid";
const char* WIFI_PASSWORD = "your-wifi-password";
const char* GATEWAY_URL   = "ws://192.168.1.100:18789";

const int LED_PIN = 2;
static bool led_on = false;

// -- Handlers ----------------------------------------------------------------

bool on_get(const char* resource_id, oc_state_t* out_state) {
    if (strcmp(resource_id, "led") != 0) return false;
    strncpy(out_state->value, led_on ? "on" : "off", OC_MAX_VALUE_LEN);
    return true;
}

bool on_set(const char* resource_id, const char* action, const char* input_json, oc_state_t* out_state) {
    if (strcmp(resource_id, "led") != 0) return false;

    if (strcmp(action, "turn_on") == 0) {
        led_on = true;
        digitalWrite(LED_PIN, HIGH);
    } else if (strcmp(action, "turn_off") == 0) {
        led_on = false;
        digitalWrite(LED_PIN, LOW);
    } else {
        return false;
    }

    strncpy(out_state->value, led_on ? "on" : "off", OC_MAX_VALUE_LEN);
    // Push the state change so the agent's watch sees it immediately.
    oc_bridge_push_state("led", led_on ? "on" : "off");
    return true;
}

// -- Setup & Loop ------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Connect WiFi.
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());

    // Configure the bridge.
    static oc_bridge_config_t config = {};
    config.gateway_url = GATEWAY_URL;
    config.on_get = on_get;
    config.on_set = on_set;

    // Define the manifest.
    strncpy(config.manifest.label, "ESP32 Blink", OC_MAX_NAME_LEN);
    strncpy(config.manifest.firmware_version, "0.1.0", 32);
    config.manifest.resource_count = 1;

    oc_resource_t* led = &config.manifest.resources[0];
    strncpy(led->id, "led", OC_MAX_ID_LEN);
    strncpy(led->name, "Built-in LED", OC_MAX_NAME_LEN);
    strncpy(led->type, "light", OC_MAX_TYPE_LEN);
    strncpy(led->state.value, "off", OC_MAX_VALUE_LEN);
    led->action_count = 2;
    strncpy(led->actions[0].id, "turn_on", OC_MAX_ID_LEN);
    strncpy(led->actions[0].label, "Turn On", OC_MAX_NAME_LEN);
    strncpy(led->actions[1].id, "turn_off", OC_MAX_ID_LEN);
    strncpy(led->actions[1].label, "Turn Off", OC_MAX_NAME_LEN);

    oc_bridge_begin(&config);
    Serial.println("Bridge started.");
}

void loop() {
    oc_bridge_loop();
}
