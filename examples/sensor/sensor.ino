/**
 * Agent Hardware Protocol — Sensor Example
 *
 * A temperature sensor with:
 *   - Property "temperature" (number, observable) — current reading in Celsius
 *   - Event "high_temp" (alert, wake: true) — fires when temp exceeds 30C
 *
 * The agent sees this as a self-describing sensor. It can read the current
 * temperature, subscribe to changes, and gets woken up when temp is high.
 *
 * The event policy is declared in the manifest — the SDK enforces debouncing
 * and rate limiting on-device before sending to the host.
 *
 * Wiring: analog temperature sensor on GPIO 34 (or any ADC pin).
 *         Simulated here with random values for demo purposes.
 */

#include <WiFi.h>
#include <agent_bridge.h>

const char* WIFI_SSID     = "your-wifi-ssid";
const char* WIFI_PASSWORD = "your-wifi-password";

static float current_temp = 22.0;
static unsigned long last_read_ms = 0;
const unsigned long READ_INTERVAL_MS = 10000;

// --- Property read callback ---
ahp_read_result_t on_read(const char* resource_id, const char* property_id) {
    ahp_read_result_t result = { .ok = false };
    if (strcmp(resource_id, "temp_sensor") != 0) return result;
    if (strcmp(property_id, "temperature") == 0) {
        snprintf(result.value_json, AHP_MAX_VALUE_LEN, "%.1f", current_temp);
        result.ok = true;
    }
    return result;
}

// --- Simulated sensor read (replace with real sensor code) ---
float read_temperature() {
    float delta = ((float)random(-10, 11)) / 10.0;
    current_temp += delta;
    if (current_temp < 15.0) current_temp = 15.0;
    if (current_temp > 35.0) current_temp = 35.0;
    return current_temp;
}

void setup() {
    Serial.begin(115200);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());

    // --- Build the manifest ---
    static ahp_config_t config = {};
    config.device_id = "esp32-sensor-01";
    config.label = "ESP32 Temperature Sensor";
    config.firmware = "1.0.0";
    config.gateway_url = NULL;  // Auto-discover gateway via mDNS
    config.on_read = on_read;

    ahp_resource_t* sensor = &config.manifest.resources[0];
    config.manifest.resource_count = 1;
    strncpy(sensor->id, "temp_sensor", AHP_MAX_ID_LEN);
    strncpy(sensor->type, "sensor", AHP_MAX_TYPE_LEN);
    strncpy(sensor->label, "Room Temperature", AHP_MAX_LABEL_LEN);
    strncpy(sensor->description, "Ambient temperature sensor, reads in Celsius", AHP_MAX_DESC_LEN);
    strncpy(sensor->semantic_type, "saref:TemperatureSensor", AHP_MAX_TYPE_LEN);

    // One property: temperature (number, observable, read-only)
    sensor->property_count = 1;
    ahp_property_def_t* prop = &sensor->properties[0];
    strncpy(prop->id, "temperature", AHP_MAX_ID_LEN);
    prop->type = AHP_TYPE_NUMBER;
    strncpy(prop->label, "Temperature", AHP_MAX_LABEL_LEN);
    strncpy(prop->description, "Current ambient temperature", AHP_MAX_DESC_LEN);
    strncpy(prop->unit, "celsius", AHP_MAX_UNIT_LEN);
    prop->minimum = -40.0;
    prop->maximum = 85.0;
    prop->has_minimum = true;
    prop->has_maximum = true;
    prop->observable = true;
    prop->writable = false;

    // One event: high_temp (alert priority, wakes agent, debounced 60s)
    sensor->event_count = 1;
    ahp_event_def_t* evt = &sensor->events[0];
    strncpy(evt->id, "high_temp", AHP_MAX_ID_LEN);
    strncpy(evt->label, "High temperature", AHP_MAX_LABEL_LEN);
    strncpy(evt->description, "Temperature exceeded 30 degrees Celsius", AHP_MAX_DESC_LEN);
    evt->priority = AHP_PRIORITY_ALERT;
    evt->policy.debounce_ms = 60000;  // Don't re-fire within 60 seconds
    evt->policy.wake = true;           // Wake the agent for this
    evt->policy.max_rate_per_min = 2;  // Max 2 alerts per minute

    ahp_begin(&config);
    Serial.println("AHP bridge started.");
}

void loop() {
    ahp_loop();

    unsigned long now = millis();
    if (now - last_read_ms >= READ_INTERVAL_MS) {
        last_read_ms = now;
        float temp = read_temperature();

        // Push property change (host gets ahp.property.changed)
        char value_json[16];
        snprintf(value_json, sizeof(value_json), "%.1f", temp);
        ahp_push_property("temp_sensor", "temperature", value_json);
        Serial.printf("Temperature: %s C\n", value_json);

        // Push event if threshold crossed (SDK enforces debounce + rate limit)
        if (temp > 30.0) {
            char data[64];
            snprintf(data, sizeof(data), "{\"temperature\":%.1f}", temp);
            ahp_push_event("temp_sensor", "high_temp", AHP_PRIORITY_ALERT, data);
            Serial.println("High temperature alert pushed.");
        }
    }
}
