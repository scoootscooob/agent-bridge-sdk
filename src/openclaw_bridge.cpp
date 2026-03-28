/**
 * OpenClaw Bridge SDK — implementation.
 *
 * Handles WebSocket connection to the OpenClaw gateway, protocol handshake,
 * invoke command dispatch, and state event push.
 */

#include "openclaw_bridge.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static WebSocketsClient _ws;
static const oc_bridge_config_t* _config = nullptr;
static bool _connected = false;
static bool _handshake_sent = false;
static uint32_t _last_reconnect = 0;
static uint32_t _reconnect_ms = 5000;
static uint32_t _msg_seq = 0;

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static void build_manifest_json(JsonObject& manifest) {
    manifest["version"] = 1;
    if (_config->manifest.label[0]) {
        manifest["label"] = _config->manifest.label;
    }
    if (_config->manifest.firmware_version[0]) {
        manifest["firmwareVersion"] = _config->manifest.firmware_version;
    }
    JsonArray resources = manifest["resources"].to<JsonArray>();
    for (uint8_t i = 0; i < _config->manifest.resource_count; i++) {
        const oc_resource_t* r = &_config->manifest.resources[i];
        JsonObject res = resources.add<JsonObject>();
        res["id"] = r->id;
        res["name"] = r->name;
        res["type"] = r->type;
        if (r->state.value[0]) {
            JsonObject state = res["state"].to<JsonObject>();
            state["value"] = r->state.value;
        }
        if (r->action_count > 0) {
            JsonArray actions = res["actions"].to<JsonArray>();
            for (uint8_t j = 0; j < r->action_count; j++) {
                JsonObject action = actions.add<JsonObject>();
                action["id"] = r->actions[j].id;
                if (r->actions[j].label[0]) {
                    action["label"] = r->actions[j].label;
                }
            }
        }
    }
}

static void send_connect_hello() {
    JsonDocument doc;
    JsonObject frame = doc.to<JsonObject>();
    frame["type"] = "req";
    frame["id"] = "hello";
    frame["method"] = "hello";

    JsonObject params = frame["params"].to<JsonObject>();
    params["minProtocol"] = 1;
    params["maxProtocol"] = 1;

    JsonObject client = params["client"].to<JsonObject>();
    client["id"] = _config->manifest.label[0] ? _config->manifest.label : "bridge";
    client["displayName"] = _config->manifest.label[0] ? _config->manifest.label : "Hardware Bridge";
    client["version"] = _config->manifest.firmware_version[0] ? _config->manifest.firmware_version : "0.0.0";
    client["platform"] = "esp32";
    client["mode"] = "node";

    // Caps tell the gateway this is a hardware bridge.
    JsonArray caps = params["caps"].to<JsonArray>();
    caps.add("hardware-bridge");

    // Inline manifest so the gateway can register immediately.
    JsonObject manifest = params["bridgeManifest"].to<JsonObject>();
    build_manifest_json(manifest);

    params["role"] = "node";

    if (_config->token) {
        JsonObject auth = params["auth"].to<JsonObject>();
        auth["token"] = _config->token;
    }

    String output;
    serializeJson(doc, output);
    _ws.sendTXT(output);
    _handshake_sent = true;
}

// ---------------------------------------------------------------------------
// Invoke handler — dispatches gateway commands to user callbacks
// ---------------------------------------------------------------------------

static void handle_invoke(JsonObject& payload) {
    const char* id = payload["id"] | "";
    const char* command = payload["command"] | "";
    const char* params_json_raw = nullptr;
    String params_str;

    if (payload.containsKey("paramsJSON") && !payload["paramsJSON"].isNull()) {
        params_str = payload["paramsJSON"].as<String>();
        params_json_raw = params_str.c_str();
    }

    // Parse params if present.
    JsonDocument params_doc;
    if (params_json_raw && params_json_raw[0]) {
        deserializeJson(params_doc, params_json_raw);
    }
    JsonObject params = params_doc.as<JsonObject>();

    // Build response.
    JsonDocument resp_doc;
    JsonObject resp = resp_doc.to<JsonObject>();
    resp["type"] = "event";
    resp["event"] = "node.invoke.result";

    JsonObject resp_payload = resp["payload"].to<JsonObject>();
    resp_payload["id"] = id;
    resp_payload["nodeId"] = payload["nodeId"] | "";

    if (strcmp(command, "hardware.get") == 0) {
        const char* resource_id = params["resourceId"] | "";
        oc_state_t state = {};
        if (_config->on_get && _config->on_get(resource_id, &state)) {
            resp_payload["ok"] = true;
            // Find the resource in the manifest to build a full response.
            JsonDocument result_doc;
            JsonObject result = result_doc.to<JsonObject>();
            for (uint8_t i = 0; i < _config->manifest.resource_count; i++) {
                if (strcmp(_config->manifest.resources[i].id, resource_id) == 0) {
                    const oc_resource_t* r = &_config->manifest.resources[i];
                    JsonObject resource = result["resource"].to<JsonObject>();
                    resource["id"] = r->id;
                    resource["name"] = r->name;
                    resource["type"] = r->type;
                    JsonObject s = resource["state"].to<JsonObject>();
                    s["value"] = state.value;
                    break;
                }
            }
            String result_str;
            serializeJson(result_doc, result_str);
            resp_payload["payloadJSON"] = result_str;
        } else {
            resp_payload["ok"] = false;
            JsonObject err = resp_payload["error"].to<JsonObject>();
            err["code"] = "NOT_FOUND";
            err["message"] = "resource not found";
        }
    } else if (strcmp(command, "hardware.set") == 0) {
        const char* resource_id = params["resourceId"] | "";
        const char* action = params["action"] | "";
        String input_str;
        if (params.containsKey("input")) {
            serializeJson(params["input"], input_str);
        }
        oc_state_t state = {};
        if (_config->on_set && _config->on_set(resource_id, action, input_str.c_str(), &state)) {
            resp_payload["ok"] = true;
            JsonDocument result_doc;
            JsonObject result = result_doc.to<JsonObject>();
            result["ok"] = true;
            String result_str;
            serializeJson(result_doc, result_str);
            resp_payload["payloadJSON"] = result_str;
        } else {
            resp_payload["ok"] = false;
            JsonObject err = resp_payload["error"].to<JsonObject>();
            err["code"] = "FAILED";
            err["message"] = "action failed";
        }
    } else if (strcmp(command, "hardware.manifest") == 0) {
        resp_payload["ok"] = true;
        JsonDocument result_doc;
        JsonObject result = result_doc.to<JsonObject>();
        build_manifest_json(result);
        String result_str;
        serializeJson(result_doc, result_str);
        resp_payload["payloadJSON"] = result_str;
    } else if (strcmp(command, "hardware.watch.start") == 0 ||
               strcmp(command, "hardware.watch.stop") == 0 ||
               strcmp(command, "hardware.discover") == 0) {
        // Acknowledge; watch events are pushed via oc_bridge_push_state.
        resp_payload["ok"] = true;
    } else {
        resp_payload["ok"] = false;
        JsonObject err = resp_payload["error"].to<JsonObject>();
        err["code"] = "UNKNOWN_COMMAND";
        err["message"] = command;
    }

    String output;
    serializeJson(resp_doc, output);
    _ws.sendTXT(output);
}

// ---------------------------------------------------------------------------
// WebSocket event handler
// ---------------------------------------------------------------------------

static void ws_event(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            _connected = true;
            _handshake_sent = false;
            send_connect_hello();
            break;

        case WStype_DISCONNECTED:
            _connected = false;
            _handshake_sent = false;
            break;

        case WStype_TEXT: {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err) break;

            JsonObject frame = doc.as<JsonObject>();
            const char* type_str = frame["type"] | "";

            // Handle hello-ok response.
            if (strcmp(type_str, "res") == 0) {
                // Handshake acknowledged. We're live.
                break;
            }

            // Handle invoke requests from the gateway.
            if (strcmp(type_str, "event") == 0) {
                const char* event = frame["event"] | "";
                if (strcmp(event, "node.invoke.request") == 0) {
                    JsonObject invoke_payload = frame["payload"];
                    handle_invoke(invoke_payload);
                }
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void oc_bridge_begin(const oc_bridge_config_t* config) {
    _config = config;
    _reconnect_ms = config->reconnect_ms > 0 ? config->reconnect_ms : 5000;

    // Parse URL to extract host, port, path.
    String url = String(config->gateway_url);
    // Strip protocol prefix.
    if (url.startsWith("ws://")) url = url.substring(5);
    else if (url.startsWith("wss://")) url = url.substring(6);

    int colon = url.indexOf(':');
    int slash = url.indexOf('/');
    String host;
    uint16_t port = 18789;
    String path = "/";

    if (colon >= 0) {
        host = url.substring(0, colon);
        if (slash >= 0) {
            port = url.substring(colon + 1, slash).toInt();
            path = url.substring(slash);
        } else {
            port = url.substring(colon + 1).toInt();
        }
    } else if (slash >= 0) {
        host = url.substring(0, slash);
        path = url.substring(slash);
    } else {
        host = url;
    }

    _ws.begin(host.c_str(), port, path.c_str());
    _ws.onEvent(ws_event);
    _ws.setReconnectInterval(_reconnect_ms);
}

void oc_bridge_loop(void) {
    _ws.loop();
}

void oc_bridge_push_state(const char* resource_id, const char* value) {
    if (!_connected) return;

    JsonDocument doc;
    JsonObject frame = doc.to<JsonObject>();
    frame["type"] = "event";
    frame["event"] = "hardware.bridge.event";

    JsonObject payload = frame["payload"].to<JsonObject>();
    payload["kind"] = "changed";
    payload["resourceId"] = resource_id;

    // Find resource in manifest and build full resource object.
    for (uint8_t i = 0; i < _config->manifest.resource_count; i++) {
        if (strcmp(_config->manifest.resources[i].id, resource_id) == 0) {
            const oc_resource_t* r = &_config->manifest.resources[i];
            JsonObject resource = payload["resource"].to<JsonObject>();
            resource["id"] = r->id;
            resource["name"] = r->name;
            resource["type"] = r->type;
            JsonObject state = resource["state"].to<JsonObject>();
            state["value"] = value;
            break;
        }
    }

    String output;
    serializeJson(doc, output);
    _ws.sendTXT(output);
}

void oc_bridge_push_event(const char* resource_id, const char* kind) {
    if (!_connected) return;

    JsonDocument doc;
    JsonObject frame = doc.to<JsonObject>();
    frame["type"] = "event";
    frame["event"] = "hardware.bridge.event";

    JsonObject payload = frame["payload"].to<JsonObject>();
    payload["kind"] = kind;
    payload["resourceId"] = resource_id;

    String output;
    serializeJson(doc, output);
    _ws.sendTXT(output);
}

bool oc_bridge_connected(void) {
    return _connected;
}

void oc_bridge_stop(void) {
    _ws.disconnect();
    _connected = false;
    _handshake_sent = false;
}
