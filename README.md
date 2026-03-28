# OpenClaw Bridge SDK

Connect hardware to AI agents. This is the firmware SDK for building agent-native devices that register directly with the [OpenClaw](https://github.com/openclaw/openclaw) gateway.

## What this does

Your ESP32 device connects to the OpenClaw gateway over WebSocket, announces its resources (sensors, lights, switches, etc.), and becomes controllable by any AI agent through the standard hardware tool. No Home Assistant, no cloud, no app.

```
ESP32 ──WebSocket──> OpenClaw Gateway ──> AI Agent
                                          "turn on the kitchen light"
ESP32 <──invoke────── OpenClaw Gateway <── AI Agent
```

## Quick start

1. Install [PlatformIO](https://platformio.org/install)
2. Clone this repo
3. Edit WiFi and gateway URL in `examples/blink/blink.ino`
4. Flash: `pio run -t upload`
5. The device appears in `openclaw hardware list`

## Examples

| Example | What it does |
|---------|-------------|
| `examples/blink/` | One LED, two actions (turn_on, turn_off). The "Hello World". |
| `examples/sensor/` | Temperature sensor, read-only, pushes state every 10s. |

## How it works

The SDK implements the OpenClaw bridge device protocol:

1. **Connect** — WebSocket to gateway with `caps: ["hardware-bridge"]`
2. **Announce** — send a manifest listing resources, actions, and current state
3. **Handle** — respond to `hardware.get` and `hardware.set` invoke commands
4. **Push** — send `hardware.bridge.event` when state changes

The gateway auto-registers the device as a hardware adapter. The AI agent discovers it via `hardware list_resources` and controls it via `hardware run_action`.

## API

```c
#include <openclaw_bridge.h>

// Define your manifest (resources, actions, state).
oc_bridge_config_t config = { ... };

// In setup():
oc_bridge_begin(&config);

// In loop():
oc_bridge_loop();

// When state changes:
oc_bridge_push_state("led", "on");
```

### Callbacks

```c
// Called when the agent reads a resource.
bool on_get(const char* resource_id, oc_state_t* out_state);

// Called when the agent invokes an action.
bool on_set(const char* resource_id, const char* action,
            const char* input_json, oc_state_t* out_state);
```

## Requirements

- ESP32 or ESP32-S3 (Arduino framework)
- WiFi network shared with the OpenClaw gateway
- OpenClaw gateway running with hardware adapter support

## Protocol reference

The TypeScript protocol spec lives in the OpenClaw repo at `src/hardware/bridge-protocol.ts`. This C SDK mirrors those types 1:1.

## License

MIT
