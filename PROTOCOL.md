# Agent Hardware Protocol (AHP)

Version: 0.1.0-draft

## Overview

AHP is an open protocol that enables physical devices to be discovered, queried, controlled, and observed by AI agents. It defines a semantic contract between a **device** (or bridge aggregating multiple devices) and a **host** (any agent runtime, gateway, or orchestrator).

The protocol is transport-agnostic and host-agnostic.

### Design principles

1. **Self-describing for AI.** Devices publish manifests with typed resources, natural language descriptions, action schemas, and semantic annotations that an LLM can reason about directly.
2. **Three interaction primitives.** Properties (live state), commands (typed operations), events (device-declared push notifications with policies).
3. **Capability negotiation.** Device and host declare what they support in a handshake.
4. **Transport independence.** JSON-RPC 2.0 messages over WebSocket, MQTT, HTTP, BLE, or serial.
5. **Event intelligence on the device.** Devices declare their own event policies (thresholds, debounce, rate limits, wake hints).

## 1. Session lifecycle

Three-message handshake:

```
Device -> Host:  ahp.hello      (manifest + caps)
Host   -> Device: ahp.hello.ok  (session ID + negotiated version)
Device -> Host:  ahp.ready
```

## 2. Manifest

Self-contained JSON document describing all device capabilities. Each resource has:
- **Properties** — typed state (boolean/integer/number/string) with unit, range, observable, writable flags
- **Commands** — typed operations with JSON Schema input, safe/idempotent/confirmRequired hints
- **Events** — declarations with priority levels and emission policies (debounce_ms, threshold, wake, batch_window_ms, max_rate_per_min)

## 3. Interactions (JSON-RPC 2.0)

| Method | Direction | Description |
|--------|-----------|-------------|
| ahp.property.read | host -> device | Read a property value |
| ahp.property.write | host -> device | Write a property value |
| ahp.command.invoke | host -> device | Invoke a command with input |
| ahp.subscribe | host -> device | Subscribe to property changes |
| ahp.unsubscribe | host -> device | Cancel subscription |
| ahp.batch | host -> device | Batch multiple operations |
| ahp.property.changed | device -> host | Property state change notification |
| ahp.event | device -> host | Event emission (with seq for ordering) |
| ahp.manifest.update | device -> host | Re-announce manifest |

## 4. Transport bindings

- **WebSocket** — ws(s)://host:port/ahp, JSON text frames
- **MQTT** — ahp/{deviceId}/to-host, ahp/{deviceId}/from-host
- **HTTP** — POST + SSE
- **BLE / Serial** — future

## 5. Error codes

-32001 ResourceNotFound, -32002 PropertyNotFound, -32003 CommandNotFound, -32004 NotWritable, -32005 ValidationFailed, -32006 DeviceBusy, -32007 ConfirmationRequired, -32008 CapabilityNotSupported

See the full specification with examples at the SDK source.
