/**
 * OpenClaw Bridge SDK — Sensor Example
 *
 * A read-only temperature sensor that pushes state changes to the agent.
 * No actions — the agent observes but doesn't control.
 *
 * Wiring: analog temperature sensor on GPIO 34 (or any ADC pin).
 *         Simulated here with random values for demo purposes.
 */

#include <WiFi.h>
#include <openclaw_bridge.h>

// -- Config ------------------------------------------------------------------

const char* WIFI_SSID     = "your-wifi-ssid";
const char* WIFI_PASSWORD = "your-wifi-password";
const char* GATEWAY_URL   = "ws://192.168.1.100:18789";

static float last_temp = 0.0;
static unsigned long last_read_ms = 0;
const unsigned long READ_INTERVAL_MS = 10000; // Push every 10 seconds.

// -- Handlers ----------------------------------------------------------------

bool on_get(const char* resource_id, oc_state_t* out_state) {
    if (strcmp(resource_id, "temp") != 0) return false;
    snprintf(out_state->value, OC_MAX_VALUE_LEN, "%.1f", last_temp);
    return true;
}

// -- Sensor read (replace with real sensor code) -----------------------------

float read_temperature() {
    // Simulated: random walk around 22C.
    // Replace with actual analogRead() + conversion for your sensor.
    float delta = ((float)random(-10, 11)) / 10.0;
    last_temp += delta;
    if (last_temp < 15.0) last_temp = 15.0;
    if (last_temp > 35.0) last_temp = 35.0;
    return last_temp;
}

// -- Setup & Loop ------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    last_temp = 22.0;

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
    config.on_set = nullptr; // Read-only sensor, no actions.

    strncpy(config.manifest.label, "ESP32 Sensor", OC_MAX_NAME_LEN);
    strncpy(config.manifest.firmware_version, "0.1.0", 32);
    config.manifest.resource_count = 1;

    oc_resource_t* temp = &config.manifest.resources[0];
    strncpy(temp->id, "temp", OC_MAX_ID_LEN);
    strncpy(temp->name, "Room Temperature", OC_MAX_NAME_LEN);
    strncpy(temp->type, "sensor", OC_MAX_TYPE_LEN);
    snprintf(temp->state.value, OC_MAX_VALUE_LEN, "%.1f", last_temp);
    temp->action_count = 0;

    oc_bridge_begin(&config);
    Serial.println("Bridge started.");
}

void loop() {
    oc_bridge_loop();

    unsigned long now = millis();
    if (now - last_read_ms >= READ_INTERVAL_MS) {
        last_read_ms = now;
        float temp = read_temperature();
        char value[16];
        snprintf(value, sizeof(value), "%.1f", temp);

        Serial.printf("Temperature: %s C\n", value);

        // Push the new reading to the gateway.
        // If hardware.agentWake is on, this wakes the agent.
        oc_bridge_push_state("temp", value);
    }
}
