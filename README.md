# Agent Bridge SDK

Connect hardware to AI agents. A generic firmware SDK for building agent-native devices that register directly with any compatible agent gateway over WebSocket.

## What this does

Your ESP32 connects to an agent gateway, announces its resources (sensors, lights, switches, relays), and becomes controllable by any AI agent through the standard hardware tool. No hub, no cloud, no app.

```
ESP32 ──WebSocket──> Agent Gateway ──> AI Agent
                                       "turn on the kitchen light"
ESP32 <──invoke────── Agent Gateway <── AI Agent
```

The protocol is gateway-agnostic. Any gateway that speaks the bridge protocol (`caps: ["hardware-bridge"]`) works.

## Quick start

1. Install [PlatformIO](https://platformio.org/install)
2. Clone this repo
3. Edit WiFi and gateway URL in `examples/blink/blink.ino`
4. Flash: `pio run -t upload`
5. The device appears in your gateway's hardware list

## Examples

| Example | What it does |
|---------|-------------|
| `examples/blink/` | One LED, two actions (turn_on, turn_off). The "Hello World". |
| `examples/sensor/` | Temperature sensor, read-only, pushes state every 10s. |

## API

```c
#include <agent_bridge.h>

ab_config_t config = { ... };
ab_begin(&config);    // In setup()
ab_loop();            // In loop()
ab_push_state("sensor-1", "23.5");  // When state changes
```

### Callbacks

```c
bool on_get(const char* resource_id, ab_state_t* out_state);
bool on_set(const char* resource_id, const char* action,
            const char* input_json, ab_state_t* out_state);
```

## Protocol

| Direction | Message | Purpose |
|-----------|---------|---------|
| Device → Gateway | `hello` with `caps: ["hardware-bridge"]` + manifest | Register resources |
| Gateway → Device | `node.invoke.request` with `hardware.get` | Read state |
| Gateway → Device | `node.invoke.request` with `hardware.set` | Execute action |
| Device → Gateway | `hardware.bridge.event` | Push state changes |

## Requirements

- ESP32 or ESP32-S3 (Arduino framework)
- WiFi network shared with the agent gateway
- [ArduinoJson](https://arduinojson.org/) v7+
- [WebSockets](https://github.com/Links2004/arduinoWebSockets)

## License

MIT
