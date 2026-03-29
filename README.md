# Agent Hardware Protocol (AHP) SDK

An open protocol and firmware SDK for connecting hardware devices to AI agents.

Devices self-describe with typed properties, commands, and events — including semantic annotations and device-side event policies — so any AI agent can discover, query, control, and react to them without platform-specific integrations.

## What makes this different

| | MQTT / ESPHome / Tasmota | AHP SDK |
|---|---|---|
| **Self-description** | Raw topics, human writes the mapping | Device publishes a semantic manifest an LLM can reason about directly |
| **State model** | Key-value telemetry | Typed properties with units, ranges, observability |
| **Actions** | Publish to command topic | Typed commands with JSON Schema input, safe/idempotent hints |
| **Events** | Firehose of state changes | Device-declared event policies — debounce, thresholds, rate limits, agent wake hints |
| **Integration** | Per-platform (HA integration, Alexa skill, etc.) | One protocol, any agent framework |

## Quick start

```cpp
#include <agent_bridge.h>

// 1. Define your callbacks
ahp_read_result_t on_read(const char* resource_id, const char* property_id) { ... }
ahp_invoke_result_t on_invoke(const char* resource_id, const char* command_id, const char* input_json) { ... }

// 2. Build a manifest describing your device
ahp_config_t config = {};
config.device_id = "my-device-01";
config.label = "My Device";
config.gateway_url = "ws://192.168.1.100:18789/ahp";
config.on_read = on_read;
config.on_invoke = on_invoke;
// ... add resources, properties, commands, events to config.manifest ...

// 3. Start
ahp_begin(&config);

// 4. In loop()
ahp_loop();

// 5. Push state changes
ahp_push_property("led", "on", "true");

// 6. Push events (SDK enforces your declared policies)
ahp_push_event("sensor", "high_temp", AHP_PRIORITY_ALERT, "{\"temperature\":31.5}");
```

## Three interaction primitives

**Properties** — live readable/writable state with types, units, and ranges
```json
{ "id": "brightness", "type": "integer", "minimum": 0, "maximum": 100, "unit": "percent", "observable": true, "writable": true }
```

**Commands** — typed invocable operations with input schemas
```json
{ "id": "set_brightness", "input": { "type": "object", "properties": { "brightness": { "type": "integer" } } }, "idempotent": true }
```

**Events** — device-declared push notifications with policies
```json
{ "id": "motion_detected", "priority": "alert", "policy": { "debounce_ms": 30000, "wake": true } }
```

## Event policies

Devices declare their own event emission rules. The SDK enforces them on-device — no firehose:

- `debounce_ms` — minimum interval between emissions
- `max_rate_per_min` — hard rate cap per minute
- `wake: true` — tells the host to wake the agent for this event
- `threshold` — only emit when a property crosses a value
- `batch_window_ms` — batch events within a window

## Transport agnostic

The protocol is JSON-RPC 2.0 over any transport:
- **WebSocket** — primary (implemented in this SDK)
- **MQTT** — `ahp/{deviceId}/to-host` / `ahp/{deviceId}/from-host`
- **HTTP** — POST + SSE
- **BLE / Serial** — future

## Examples

- [`examples/blink/`](examples/blink/) — LED with properties + commands
- [`examples/sensor/`](examples/sensor/) — temperature sensor with observable property + alert event with policy

## Protocol spec

See [`PROTOCOL.md`](PROTOCOL.md) for the full protocol specification.

## Dependencies

- [ArduinoJson](https://arduinojson.org/) v7+
- [WebSockets](https://github.com/Links2004/arduinoWebSockets) by Markus Sattler
- ESP32 Arduino core

## License

MIT
