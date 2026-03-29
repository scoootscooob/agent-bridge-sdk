/**
 * Agent Hardware Protocol (AHP) SDK — WebSocket transport implementation.
 *
 * Implements the AHP session lifecycle over WebSocket:
 *   ahp.hello → ahp.hello.ok → ahp.ready
 *
 * Dispatches incoming JSON-RPC methods to user callbacks:
 *   ahp.property.read  → on_read
 *   ahp.property.write → on_write
 *   ahp.command.invoke  → on_invoke
 *
 * Sends outbound notifications with device-side policy enforcement:
 *   ahp.property.changed — via ahp_push_property()
 *   ahp.event            — via ahp_push_event() (respects debounce/rate limits)
 *   ahp.manifest.update  — via ahp_update_manifest()
 */

#include "agent_bridge.h"
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static const ahp_config_t* _config = NULL;
static WebSocketsClient _ws;
static bool _connected = false;
static bool _handshake_done = false;
static uint32_t _msg_id = 1;
static uint32_t _event_seq = 0;
static char _session_id[64] = {0};

// Per-event policy tracking
typedef struct {
    char resource_id[AHP_MAX_ID_LEN];
    char event_id[AHP_MAX_ID_LEN];
    uint32_t last_emit_ms;
    uint16_t emit_count_this_min;
    uint32_t minute_start_ms;
} ahp_event_tracker_t;

#define AHP_MAX_EVENT_TRACKERS (AHP_MAX_RESOURCES * AHP_MAX_EVENTS)
static ahp_event_tracker_t _event_trackers[AHP_MAX_EVENT_TRACKERS];
static uint8_t _event_tracker_count = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* type_str(ahp_type_t t) {
    switch (t) {
        case AHP_TYPE_BOOLEAN: return "boolean";
        case AHP_TYPE_INTEGER: return "integer";
        case AHP_TYPE_NUMBER:  return "number";
        case AHP_TYPE_STRING:  return "string";
        default:               return "string";
    }
}

static const char* priority_str(ahp_priority_t p) {
    switch (p) {
        case AHP_PRIORITY_NOTICE:   return "notice";
        case AHP_PRIORITY_ALERT:    return "alert";
        case AHP_PRIORITY_CRITICAL: return "critical";
        default:                    return "info";
    }
}

// ---------------------------------------------------------------------------
// Manifest serialization
// ---------------------------------------------------------------------------

static void serialize_manifest(JsonObject out, const ahp_manifest_t* m) {
    JsonArray resources = out["resources"].to<JsonArray>();
    for (uint8_t r = 0; r < m->resource_count; r++) {
        const ahp_resource_t* res = &m->resources[r];
        JsonObject robj = resources.add<JsonObject>();
        robj["id"] = res->id;
        robj["type"] = res->type;
        robj["label"] = res->label;
        if (res->description[0]) robj["description"] = res->description;
        if (res->semantic_type[0]) robj["semanticType"] = res->semantic_type;

        if (res->property_count > 0) {
            JsonArray props = robj["properties"].to<JsonArray>();
            for (uint8_t p = 0; p < res->property_count; p++) {
                const ahp_property_def_t* prop = &res->properties[p];
                JsonObject pobj = props.add<JsonObject>();
                pobj["id"] = prop->id;
                pobj["type"] = type_str(prop->type);
                if (prop->label[0]) pobj["label"] = prop->label;
                if (prop->description[0]) pobj["description"] = prop->description;
                if (prop->unit[0]) pobj["unit"] = prop->unit;
                if (prop->has_minimum) pobj["minimum"] = prop->minimum;
                if (prop->has_maximum) pobj["maximum"] = prop->maximum;
                if (prop->observable) pobj["observable"] = true;
                if (prop->writable) pobj["writable"] = true;
            }
        }

        if (res->command_count > 0) {
            JsonArray cmds = robj["commands"].to<JsonArray>();
            for (uint8_t c = 0; c < res->command_count; c++) {
                const ahp_command_def_t* cmd = &res->commands[c];
                JsonObject cobj = cmds.add<JsonObject>();
                cobj["id"] = cmd->id;
                if (cmd->label[0]) cobj["label"] = cmd->label;
                if (cmd->description[0]) cobj["description"] = cmd->description;
                if (cmd->input_schema_json) {
                    JsonDocument schema_doc;
                    deserializeJson(schema_doc, cmd->input_schema_json);
                    cobj["input"] = schema_doc.as<JsonVariant>();
                }
                if (cmd->safe) cobj["safe"] = true;
                if (cmd->idempotent) cobj["idempotent"] = true;
                if (cmd->confirm_required) cobj["confirmRequired"] = true;
            }
        }

        if (res->event_count > 0) {
            JsonArray evts = robj["events"].to<JsonArray>();
            for (uint8_t e = 0; e < res->event_count; e++) {
                const ahp_event_def_t* evt = &res->events[e];
                JsonObject eobj = evts.add<JsonObject>();
                eobj["id"] = evt->id;
                if (evt->label[0]) eobj["label"] = evt->label;
                if (evt->description[0]) eobj["description"] = evt->description;
                if (evt->priority != AHP_PRIORITY_INFO) eobj["priority"] = priority_str(evt->priority);
                const ahp_event_policy_t* pol = &evt->policy;
                if (pol->debounce_ms || pol->wake || pol->batch_window_ms || pol->max_rate_per_min) {
                    JsonObject pobj = eobj["policy"].to<JsonObject>();
                    if (pol->debounce_ms) pobj["debounce_ms"] = pol->debounce_ms;
                    if (pol->wake) pobj["wake"] = true;
                    if (pol->batch_window_ms) pobj["batch_window_ms"] = pol->batch_window_ms;
                    if (pol->max_rate_per_min) pobj["max_rate_per_min"] = pol->max_rate_per_min;
                }
                if (evt->data_schema_json) {
                    JsonDocument data_doc;
                    deserializeJson(data_doc, evt->data_schema_json);
                    eobj["data"] = data_doc.as<JsonVariant>();
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Send helpers
// ---------------------------------------------------------------------------

static void send_json(JsonDocument& doc) {
    String output;
    serializeJson(doc, output);
    _ws.sendTXT(output);
}

static void send_hello(void) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "ahp.hello";
    JsonObject params = doc["params"].to<JsonObject>();
    params["protocolVersion"] = "0.1";
    params["deviceId"] = _config->device_id;
    if (_config->label) params["label"] = _config->label;
    if (_config->firmware) params["firmware"] = _config->firmware;

    JsonArray caps = params["caps"].to<JsonArray>();
    caps.add("properties");
    if (_config->on_invoke) caps.add("commands");
    caps.add("events");

    serialize_manifest(params["manifest"].to<JsonObject>(), &_config->manifest);
    send_json(doc);
}

static void send_ready(void) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "ahp.ready";
    doc["params"] = JsonObject();
    send_json(doc);
}

static void send_response(uint32_t id, JsonObject result) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;
    doc["result"] = result;
    send_json(doc);
}

static void send_error(uint32_t id, int code, const char* message) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;
    JsonObject error = doc["error"].to<JsonObject>();
    error["code"] = code;
    error["message"] = message;
    send_json(doc);
}

// ---------------------------------------------------------------------------
// Event policy enforcement
// ---------------------------------------------------------------------------

static ahp_event_tracker_t* find_or_create_tracker(const char* resource_id, const char* event_id) {
    for (uint8_t i = 0; i < _event_tracker_count; i++) {
        if (strcmp(_event_trackers[i].resource_id, resource_id) == 0 &&
            strcmp(_event_trackers[i].event_id, event_id) == 0) {
            return &_event_trackers[i];
        }
    }
    if (_event_tracker_count < AHP_MAX_EVENT_TRACKERS) {
        ahp_event_tracker_t* t = &_event_trackers[_event_tracker_count++];
        strncpy(t->resource_id, resource_id, AHP_MAX_ID_LEN - 1);
        strncpy(t->event_id, event_id, AHP_MAX_ID_LEN - 1);
        t->last_emit_ms = 0;
        t->emit_count_this_min = 0;
        t->minute_start_ms = millis();
        return t;
    }
    return NULL;
}

static const ahp_event_def_t* find_event_def(const char* resource_id, const char* event_id) {
    for (uint8_t r = 0; r < _config->manifest.resource_count; r++) {
        const ahp_resource_t* res = &_config->manifest.resources[r];
        if (strcmp(res->id, resource_id) != 0) continue;
        for (uint8_t e = 0; e < res->event_count; e++) {
            if (strcmp(res->events[e].id, event_id) == 0) return &res->events[e];
        }
    }
    return NULL;
}

static bool check_event_policy(const char* resource_id, const char* event_id) {
    const ahp_event_def_t* def = find_event_def(resource_id, event_id);
    if (!def) return true;

    const ahp_event_policy_t* pol = &def->policy;
    if (!pol->debounce_ms && !pol->max_rate_per_min) return true;

    ahp_event_tracker_t* t = find_or_create_tracker(resource_id, event_id);
    if (!t) return true;

    uint32_t now = millis();

    if (pol->debounce_ms > 0 && t->last_emit_ms > 0) {
        if ((now - t->last_emit_ms) < pol->debounce_ms) return false;
    }

    if (pol->max_rate_per_min > 0) {
        if ((now - t->minute_start_ms) >= 60000) {
            t->emit_count_this_min = 0;
            t->minute_start_ms = now;
        }
        if (t->emit_count_this_min >= pol->max_rate_per_min) return false;
    }

    t->last_emit_ms = now;
    t->emit_count_this_min++;
    return true;
}

// ---------------------------------------------------------------------------
// Incoming message dispatch
// ---------------------------------------------------------------------------

static void handle_hello_ok(JsonObject result) {
    const char* sid = result["sessionId"] | "";
    strncpy(_session_id, sid, sizeof(_session_id) - 1);
    _handshake_done = true;
    send_ready();
}

static void handle_property_read(uint32_t id, JsonObject params) {
    if (!_config->on_read) {
        send_error(id, -32601, "Property read not supported");
        return;
    }
    const char* rid = params["resourceId"] | "";
    const char* pid = params["propertyId"] | "";
    ahp_read_result_t result = _config->on_read(rid, pid);
    if (!result.ok) {
        send_error(id, -32001, "Resource or property not found");
        return;
    }
    JsonDocument doc;
    JsonObject res = doc.to<JsonObject>();
    JsonDocument val_doc;
    deserializeJson(val_doc, result.value_json);
    res["value"] = val_doc.as<JsonVariant>();
    send_response(id, res);
}

static void handle_property_write(uint32_t id, JsonObject params) {
    if (!_config->on_write) {
        send_error(id, -32004, "Property write not supported");
        return;
    }
    const char* rid = params["resourceId"] | "";
    const char* pid = params["propertyId"] | "";
    String value_str;
    serializeJson(params["value"], value_str);
    ahp_write_result_t result = _config->on_write(rid, pid, value_str.c_str());
    if (!result.ok) {
        send_error(id, -32004, "Write failed");
        return;
    }
    JsonDocument doc;
    JsonObject res = doc.to<JsonObject>();
    JsonDocument val_doc;
    deserializeJson(val_doc, result.value_json);
    res["value"] = val_doc.as<JsonVariant>();
    send_response(id, res);
}

static void handle_command_invoke(uint32_t id, JsonObject params) {
    if (!_config->on_invoke) {
        send_error(id, -32008, "Commands not supported");
        return;
    }
    const char* rid = params["resourceId"] | "";
    const char* cid = params["commandId"] | "";
    String input_str;
    if (params.containsKey("input")) {
        serializeJson(params["input"], input_str);
    }
    ahp_invoke_result_t result = _config->on_invoke(
        rid, cid, input_str.length() > 0 ? input_str.c_str() : NULL
    );
    JsonDocument doc;
    JsonObject res = doc.to<JsonObject>();
    res["ok"] = result.ok;
    if (result.output_json[0]) {
        JsonDocument out_doc;
        deserializeJson(out_doc, result.output_json);
        res["output"] = out_doc.as<JsonVariant>();
    }
    send_response(id, res);
}

static void handle_message(uint8_t* payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length)) return;

    if (doc.containsKey("result")) {
        handle_hello_ok(doc["result"].as<JsonObject>());
        return;
    }

    const char* method = doc["method"] | "";
    uint32_t id = doc["id"] | (uint32_t)0;

    if (strcmp(method, "ahp.property.read") == 0) {
        handle_property_read(id, doc["params"].as<JsonObject>());
    } else if (strcmp(method, "ahp.property.write") == 0) {
        handle_property_write(id, doc["params"].as<JsonObject>());
    } else if (strcmp(method, "ahp.command.invoke") == 0) {
        handle_command_invoke(id, doc["params"].as<JsonObject>());
    } else if (id > 0) {
        send_error(id, -32601, "Method not found");
    }
}

// ---------------------------------------------------------------------------
// WebSocket event handler
// ---------------------------------------------------------------------------

static void ws_event(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            _connected = true;
            _handshake_done = false;
            send_hello();
            break;
        case WStype_DISCONNECTED:
            _connected = false;
            _handshake_done = false;
            _session_id[0] = '\0';
            break;
        case WStype_TEXT:
            handle_message(payload, length);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ahp_begin(const ahp_config_t* config) {
    _config = config;
    _connected = false;
    _handshake_done = false;
    _msg_id = 1;
    _event_seq = 0;
    _event_tracker_count = 0;

    String url = String(config->gateway_url);
    bool use_ssl = url.startsWith("wss://");
    if (url.startsWith("ws://")) url = url.substring(5);
    else if (use_ssl) url = url.substring(6);

    int path_idx = url.indexOf('/');
    String path = "/ahp";
    String host_port = url;
    if (path_idx > 0) {
        path = url.substring(path_idx);
        host_port = url.substring(0, path_idx);
    }

    int port_idx = host_port.indexOf(':');
    String host = host_port;
    uint16_t port = use_ssl ? 443 : 80;
    if (port_idx > 0) {
        host = host_port.substring(0, port_idx);
        port = host_port.substring(port_idx + 1).toInt();
    }

    _ws.begin(host.c_str(), port, path.c_str());
    _ws.onEvent(ws_event);
    _ws.setReconnectInterval(config->reconnect_ms > 0 ? config->reconnect_ms : 5000);

    if (config->token) {
        String auth = String("Bearer ") + config->token;
        _ws.setExtraHeaders(("Authorization: " + auth).c_str());
    }
}

void ahp_loop(void) {
    _ws.loop();
}

void ahp_push_property(const char* resource_id, const char* property_id, const char* value_json) {
    if (!_connected || !_handshake_done) return;

    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "ahp.property.changed";
    JsonObject params = doc["params"].to<JsonObject>();
    params["resourceId"] = resource_id;
    JsonArray changes = params["changes"].to<JsonArray>();
    JsonObject change = changes.add<JsonObject>();
    change["propertyId"] = property_id;
    JsonDocument val_doc;
    deserializeJson(val_doc, value_json);
    change["value"] = val_doc.as<JsonVariant>();
    send_json(doc);
}

void ahp_push_event(const char* resource_id, const char* event_id, ahp_priority_t priority, const char* data_json) {
    if (!_connected || !_handshake_done) return;
    if (!check_event_policy(resource_id, event_id)) return;

    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "ahp.event";
    JsonObject params = doc["params"].to<JsonObject>();
    params["resourceId"] = resource_id;
    params["eventId"] = event_id;
    if (priority != AHP_PRIORITY_INFO) params["priority"] = priority_str(priority);
    if (data_json) {
        JsonDocument data_doc;
        deserializeJson(data_doc, data_json);
        params["data"] = data_doc.as<JsonVariant>();
    }
    params["seq"] = _event_seq++;
    send_json(doc);
}

void ahp_update_manifest(const ahp_manifest_t* manifest) {
    if (!_connected || !_handshake_done) return;

    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "ahp.manifest.update";
    JsonObject params = doc["params"].to<JsonObject>();
    serialize_manifest(params["manifest"].to<JsonObject>(), manifest);
    send_json(doc);
}

bool ahp_connected(void) {
    return _connected && _handshake_done;
}

void ahp_stop(void) {
    _ws.disconnect();
    _connected = false;
    _handshake_done = false;
}
