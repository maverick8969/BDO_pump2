/**
 * @file webserver.c
 * @brief Web server implementation with embedded WebUI
 */

#include "webserver.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "system_state.h"
#include <string.h>

static const char *TAG = "WEBSERVER";
static httpd_handle_t server = NULL;

/* External reference to global state */
extern system_state_t g_system_state;

/* Forward declarations */
static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t api_status_handler(httpd_req_t *req);
static esp_err_t api_start_fill_handler(httpd_req_t *req);
static esp_err_t api_stop_fill_handler(httpd_req_t *req);
static esp_err_t api_set_target_handler(httpd_req_t *req);

/**
 * @brief Embedded HTML for WebUI
 */
static const char HTML_INDEX[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BDO Pump Controller</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:#fff;padding:20px}
.container{max-width:800px;margin:0 auto;background:rgba(0,0,0,0.3);border-radius:15px;padding:30px;box-shadow:0 8px 32px rgba(0,0,0,0.3)}
h1{text-align:center;margin-bottom:30px;font-size:2.5em;text-shadow:2px 2px 4px rgba(0,0,0,0.5)}
.status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:20px;margin-bottom:30px}
.status-card{background:rgba(255,255,255,0.1);border-radius:10px;padding:20px;backdrop-filter:blur(10px)}
.status-card h3{font-size:0.9em;opacity:0.8;margin-bottom:10px}
.status-card .value{font-size:2em;font-weight:bold}
.status-card .unit{font-size:0.9em;opacity:0.7}
.state-badge{display:inline-block;padding:8px 16px;border-radius:20px;font-size:0.9em;font-weight:bold;margin-bottom:20px}
.state-IDLE{background:#6c757d}
.state-FILLING{background:#28a745;animation:pulse 1.5s infinite}
.state-COMPLETED{background:#17a2b8}
.state-ERROR{background:#dc3545}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.5}}
.progress-bar{width:100%;height:40px;background:rgba(255,255,255,0.1);border-radius:20px;overflow:hidden;margin-bottom:20px}
.progress-fill{height:100%;background:linear-gradient(90deg,#28a745,#20c997);transition:width 0.5s;display:flex;align-items:center;justify-content:center;font-weight:bold}
.controls{display:flex;gap:15px;flex-wrap:wrap;margin-top:20px}
button{flex:1;min-width:150px;padding:15px;font-size:1.1em;border:none;border-radius:10px;cursor:pointer;font-weight:bold;transition:all 0.3s}
button:hover{transform:translateY(-2px);box-shadow:0 4px 8px rgba(0,0,0,0.3)}
.btn-start{background:#28a745;color:#fff}
.btn-start:hover{background:#218838}
.btn-stop{background:#dc3545;color:#fff}
.btn-stop:hover{background:#c82333}
.btn-disabled{background:#6c757d;cursor:not-allowed;opacity:0.5}
.target-input{display:flex;align-items:center;gap:10px;margin-bottom:20px}
.target-input input{flex:1;padding:12px;font-size:1.2em;border:none;border-radius:8px;background:rgba(255,255,255,0.9);color:#333}
.target-input button{flex:0 0 auto;min-width:60px}
.zone-indicator{text-align:center;padding:15px;background:rgba(255,255,255,0.1);border-radius:10px;margin-bottom:20px}
.offline{color:#dc3545;font-weight:bold}
</style>
</head>
<body>
<div class="container">
<h1>🏭 BDO Pump Controller</h1>
<div style="text-align:center">
<span class="state-badge" id="stateBadge">IDLE</span>
</div>
<div class="zone-indicator">
<span id="zoneText">Zone: IDLE</span> | <span id="pressureText">Pressure: 0%</span>
</div>
<div class="progress-bar">
<div class="progress-fill" id="progressBar" style="width:0%">0%</div>
</div>
<div class="status-grid">
<div class="status-card">
<h3>Current Weight</h3>
<div class="value" id="currentWeight">0.0</div>
<div class="unit">lbs</div>
</div>
<div class="status-card">
<h3>Target Weight</h3>
<div class="value" id="targetWeight">200.0</div>
<div class="unit">lbs</div>
</div>
<div class="status-card">
<h3>Fills Today</h3>
<div class="value" id="fillsToday">0</div>
<div class="unit">fills</div>
</div>
<div class="status-card">
<h3>Total Dispensed</h3>
<div class="value" id="totalLbs">0.0</div>
<div class="unit">lbs</div>
</div>
</div>
<div class="target-input">
<input type="number" id="targetInput" value="200" min="10" max="250" step="5">
<button onclick="setTarget()">Set Target</button>
</div>
<div class="controls">
<button class="btn-start" id="btnStart" onclick="startFill()">Start Fill</button>
<button class="btn-stop" id="btnStop" onclick="stopFill()">Stop Fill</button>
</div>
<div style="margin-top:20px;text-align:center;opacity:0.7;font-size:0.9em">
<span id="scaleStatus">Scale: <span class="offline">Offline</span></span> |
<span id="mqttStatus">MQTT: <span class="offline">Disconnected</span></span>
</div>
</div>
<script>
function updateStatus(){
fetch('/api/status').then(r=>r.json()).then(data=>{
document.getElementById('currentWeight').innerText=data.current_weight.toFixed(1);
document.getElementById('targetWeight').innerText=data.target_weight.toFixed(1);
document.getElementById('fillsToday').innerText=data.fills_today;
document.getElementById('totalLbs').innerText=data.total_lbs_today.toFixed(1);
const badge=document.getElementById('stateBadge');
badge.innerText=data.state;
badge.className='state-badge state-'+data.state;
document.getElementById('zoneText').innerText='Zone: '+data.zone;
document.getElementById('pressureText').innerText='Pressure: '+data.pressure_pct.toFixed(0)+'%';
const prog=Math.min(100,data.progress_pct);
const bar=document.getElementById('progressBar');
bar.style.width=prog+'%';
bar.innerText=prog.toFixed(1)+'%';
document.getElementById('scaleStatus').innerHTML='Scale: '+(data.scale_online?'<span style="color:#28a745">Online</span>':'<span class="offline">Offline</span>');
document.getElementById('mqttStatus').innerHTML='MQTT: '+(data.mqtt_connected?'<span style="color:#28a745">Connected</span>':'<span class="offline">Disconnected</span>');
const btnStart=document.getElementById('btnStart');
const btnStop=document.getElementById('btnStop');
if(data.state==='IDLE'){
btnStart.disabled=false;
btnStart.className='btn-start';
btnStop.disabled=true;
btnStop.className='btn-stop btn-disabled';
}else{
btnStart.disabled=true;
btnStart.className='btn-start btn-disabled';
btnStop.disabled=false;
btnStop.className='btn-stop';
}
}).catch(e=>console.error('Error:',e));
}
function startFill(){
fetch('/api/start',{method:'POST'}).then(r=>r.json()).then(data=>alert(data.message));
}
function stopFill(){
fetch('/api/stop',{method:'POST'}).then(r=>r.json()).then(data=>alert(data.message));
}
function setTarget(){
const val=document.getElementById('targetInput').value;
fetch('/api/set_target',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:parseFloat(val)})}).then(r=>r.json()).then(data=>alert(data.message));
}
setInterval(updateStatus,1000);
updateStatus();
</script>
</body>
</html>
)rawliteral";

/**
 * @brief Root page handler - serves WebUI
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_INDEX, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief API endpoint: Get system status (JSON)
 */
static esp_err_t api_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "state", state_to_string(g_system_state.state));
    cJSON_AddStringToObject(root, "zone", zone_to_string(g_system_state.active_zone));
    cJSON_AddNumberToObject(root, "current_weight", g_system_state.current_weight_lbs);
    cJSON_AddNumberToObject(root, "target_weight", g_system_state.target_weight_lbs);
    cJSON_AddNumberToObject(root, "pressure_pct", g_system_state.pressure_setpoint_pct);

    float progress = (g_system_state.current_weight_lbs / g_system_state.target_weight_lbs) * 100.0f;
    cJSON_AddNumberToObject(root, "progress_pct", progress);

    cJSON_AddNumberToObject(root, "fills_today", g_system_state.fills_today);
    cJSON_AddNumberToObject(root, "total_lbs_today", g_system_state.total_lbs_today);
    cJSON_AddBoolToObject(root, "scale_online", g_system_state.scale_online);
    cJSON_AddBoolToObject(root, "mqtt_connected", g_system_state.mqtt_connected);

    const char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free((void *)json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

/**
 * @brief API endpoint: Start fill operation
 */
static esp_err_t api_start_fill_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    if (g_system_state.state == STATE_IDLE) {
        g_system_state.state = STATE_SAFETY_CHECK;
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "message", "Fill started (safety checks required)");
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "System not idle");
    }

    const char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free((void *)json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

/**
 * @brief API endpoint: Stop/cancel fill operation
 */
static esp_err_t api_stop_fill_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    if (g_system_state.state != STATE_IDLE) {
        g_system_state.state = STATE_CANCELLED;
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "message", "Fill cancelled");
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "No active fill");
    }

    const char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free((void *)json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

/**
 * @brief API endpoint: Set target weight
 */
static esp_err_t api_set_target_handler(httpd_req_t *req)
{
    char content[100];
    int ret = httpd_req_recv(req, content, sizeof(content));

    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    content[ret] = '\0';

    cJSON *json = cJSON_Parse(content);
    cJSON *target = cJSON_GetObjectItem(json, "target");

    cJSON *root = cJSON_CreateObject();

    if (target && cJSON_IsNumber(target)) {
        float new_target = target->valuedouble;

        if (new_target >= 10.0f && new_target <= 250.0f) {
            g_system_state.target_weight_lbs = new_target;
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "message", "Target weight updated");
        } else {
            cJSON_AddStringToObject(root, "status", "error");
            cJSON_AddStringToObject(root, "message", "Target out of range (10-250 lbs)");
        }
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Invalid JSON");
    }

    const char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free((void *)json_str);
    cJSON_Delete(root);
    cJSON_Delete(json);

    return ESP_OK;
}

/**
 * @brief Initialize web server
 */
esp_err_t webserver_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting web server on port %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return ESP_FAIL;
    }

    // Register URI handlers
    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_root);

    httpd_uri_t uri_api_status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_status);

    httpd_uri_t uri_api_start = {
        .uri = "/api/start",
        .method = HTTP_POST,
        .handler = api_start_fill_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_start);

    httpd_uri_t uri_api_stop = {
        .uri = "/api/stop",
        .method = HTTP_POST,
        .handler = api_stop_fill_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_stop);

    httpd_uri_t uri_api_set_target = {
        .uri = "/api/set_target",
        .method = HTTP_POST,
        .handler = api_set_target_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_set_target);

    ESP_LOGI(TAG, "Web server started successfully");
    ESP_LOGI(TAG, "Access WebUI at http://<ESP32_IP>/");

    return ESP_OK;
}

/**
 * @brief Stop web server
 */
esp_err_t webserver_stop(void)
{
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "Web server stopped");
    }
    return ESP_OK;
}
