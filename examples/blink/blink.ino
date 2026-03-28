/**
 * Agent Bridge SDK — Blink Example
 *
 * The simplest agent-native device: one LED, two actions (turn_on, turn_off).
 * Any AI agent that speaks the bridge protocol can discover and control it.
 *
 * Wiring: LED on GPIO 2 (built-in on most ESP32 boards).
 */

#include <WiFi.h>
#include <agent_bridge.h>

const char* WIFI_SSID     = "your-wifi-ssid";
const char* WIFI_PASSWORD = "your-wifi-password";
const char* GATEWAY_URL   = "ws://192.168.1.100:18789";

const int LED_PIN = 2;
static bool led_on = false;

bool on_get(const char* resource_id, ab_state_t* out_state) {
    if (strcmp(resource_id, "led") != 0) return false;
    strncpy(out_state->value, led_on ? "on" : "off", AB_MAX_VALUE_LEN);
    return true;
}

bool on_set(const char* resource_id, const char* action, const char* input_json, ab_state_t* out_state) {
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
    strncpy(out_state->value, led_on ? "on" : "off", AB_MAX_VALUE_LEN);
    ab_push_state("led", led_on ? "on" : "off");
    return true;
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    Serial.printf("Connected: %s\n", WiFi.localIP().toString().c_str());

    static ab_config_t config = {};
    config.gateway_url = GATEWAY_URL;
    config.on_get = on_get;
    config.on_set = on_set;

    strncpy(config.manifest.label, "ESP32 Blink", AB_MAX_NAME_LEN);
    strncpy(config.manifest.firmware_version, "0.1.0", 32);
    config.manifest.resource_count = 1;

    ab_resource_t* led = &config.manifest.resources[0];
    strncpy(led->id, "led", AB_MAX_ID_LEN);
    strncpy(led->name, "Built-in LED", AB_MAX_NAME_LEN);
    strncpy(led->type, "light", AB_MAX_TYPE_LEN);
    strncpy(led->state.value, "off", AB_MAX_VALUE_LEN);
    led->action_count = 2;
    strncpy(led->actions[0].id, "turn_on", AB_MAX_ID_LEN);
    strncpy(led->actions[0].label, "Turn On", AB_MAX_NAME_LEN);
    strncpy(led->actions[1].id, "turn_off", AB_MAX_ID_LEN);
    strncpy(led->actions[1].label, "Turn Off", AB_MAX_NAME_LEN);

    ab_begin(&config);
}

void loop() {
    ab_loop();
}
