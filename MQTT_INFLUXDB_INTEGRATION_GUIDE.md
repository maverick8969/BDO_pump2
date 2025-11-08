# Material Consumption Tracking Integration Guide
## ESP32 → MQTT → Telegraf → InfluxDB → Grafana

**System:** Chemical Dosing System with Multi-Sensor Monitoring  
**Date:** October 29, 2025  
**Version:** 1.0

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Architecture](#architecture)
3. [Data Organization Strategy](#data-organization-strategy)
4. [ESP32 Implementation](#esp32-implementation)
5. [MQTT Configuration](#mqtt-configuration)
6. [Telegraf Configuration](#telegraf-configuration)
7. [InfluxDB Setup](#influxdb-setup)
8. [Grafana Dashboards](#grafana-dashboards)
9. [Docker Compose Setup](#docker-compose-setup)
10. [Best Practices](#best-practices)
11. [Tutorials and Resources](#tutorials-and-resources)

---

## System Overview

### Components

- **Nodes:** ESP32 devices publish sensor data to MQTT
- **Broker:** Eclipse Mosquitto (MQTT broker)
- **Collector:** Telegraf subscribes to MQTT and writes to InfluxDB
- **Database:** InfluxDB v2 (12-24 month retention + downsampling)
- **Visualization:** Grafana (dashboards, alerts, annotations)
- **Time Sync:** NTP on all devices

### Data Types

This system handles multiple data types from industrial equipment:

- **Dosing/Consumption Data:** Material usage, pump cycles, batch tracking
- **Temperature Data:** Motor temperatures, ambient conditions
- **Vibration Data:** Equipment health monitoring
- **Inventory Data:** Material levels, low-stock alerts

---

## Architecture

```
┌─────────────┐
│   ESP32     │ ──► Temperature sensors
│   Device    │ ──► Vibration sensors
│             │ ──► Scale (weight/dosing)
└──────┬──────┘
       │ WiFi
       ▼
┌─────────────────────────────────────────────────┐
│          MQTT Broker (Mosquitto)                │
│  Topics:                                        │
│    - factory/dosing/consumption                 │
│    - factory/equipment/temperature              │
│    - factory/equipment/vibration                │
│    - factory/inventory/levels                   │
└──────┬──────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────┐
│          Telegraf (MQTT Consumer)               │
│  - Subscribes to topics                         │
│  - Parses JSON payloads                         │
│  - Routes to measurements                       │
└──────┬──────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────┐
│          InfluxDB v2 (Time Series DB)           │
│  Bucket: factory_metrics                        │
│  ├── dosing_consumption (measurement)           │
│  ├── equipment_temperature (measurement)        │
│  ├── equipment_vibration (measurement)          │
│  └── inventory_levels (measurement)             │
└──────┬──────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────┐
│          Grafana (Visualization)                │
│  - Multi-panel dashboards                       │
│  - Alerts and notifications                     │
│  - Historical analysis                          │
└─────────────────────────────────────────────────┘
```

---

## Data Organization Strategy

### Measurement Schema Design

**Key Principle:** Use separate measurements for different data types.

```
Bucket: factory_metrics
├── dosing_consumption      (pump usage, material dispensed)
├── equipment_temperature   (thermal monitoring)
├── equipment_vibration     (vibration/health monitoring)
├── inventory_levels        (material quantities)
└── batch_events           (production events)
```

### Schema Examples

#### 1. Dosing/Consumption Data

```
Measurement: dosing_consumption

Tags (metadata, indexed):
  - device_id: "dosing_system_01"
  - pump: "1", "2", "3", "4"
  - chemical: "DMDEE", "T-12", "T-9", "L25B"
  - recipe: "CU-85", "CU-65/75", "FG-85/95"
  - mode: "CATALYST", "BDO"

Fields (measured values):
  - target_g: 40.0 (float)
  - actual_g: 40.2 (float)
  - error_g: 0.2 (float)
  - duration_ms: 45000 (integer)
```

#### 2. Temperature Data

```
Measurement: equipment_temperature

Tags:
  - device_id: "dosing_system_01"
  - location: "pump_motor_1", "pump_motor_2", "control_box", "ambient"
  - sensor_type: "thermocouple_k", "PT100", "DHT22"

Fields:
  - temperature_c: 45.3 (float)
  - temperature_f: 113.5 (float)
  - humidity_percent: 45.0 (float) [if applicable]
```

#### 3. Vibration Data

```
Measurement: equipment_vibration

Tags:
  - device_id: "dosing_system_01"
  - pump: "1", "2", "3", "4"
  - axis: "x", "y", "z"

Fields:
  - acceleration_g: 0.15 (float)
  - velocity_mm_s: 2.3 (float)
  - frequency_hz: 120.0 (float)
  - rms: 0.08 (float)
```

#### 4. Inventory Levels

```
Measurement: inventory_levels

Tags:
  - device_id: "dosing_system_01"
  - chemical: "DMDEE", "T-12", "T-9", "L25B"
  - container_id: "tank_1", "tank_2"

Fields:
  - remaining_g: 15234.5 (float)
  - capacity_g: 20000.0 (float)
  - percent_full: 76.2 (float)
```

---

## ESP32 Implementation

### Required Libraries

```cpp
#include <WiFi.h>
#include <PubSubClient.h>  // MQTT client
#include <time.h>          // NTP time sync
```

### Configuration

```cpp
// config.h additions
#define MQTT_BROKER "192.168.1.100"  // Your Mosquitto broker IP
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "dosing_system_01"
#define MQTT_USER "username"         // Optional
#define MQTT_PASSWORD "password"     // Optional

// MQTT Topics
#define TOPIC_DOSING      "factory/dosing/consumption"
#define TOPIC_TEMPERATURE "factory/equipment/temperature"
#define TOPIC_VIBRATION   "factory/equipment/vibration"
#define TOPIC_INVENTORY   "factory/inventory/levels"
#define TOPIC_BATCH       "factory/batch/events"

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC -18000  // EST: -5 hours
#define DAYLIGHT_OFFSET_SEC 3600
```

### MQTT Connection Setup

```cpp
WiFiClient espClient;
PubSubClient mqtt(espClient);

void setup_mqtt() {
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(mqtt_callback);  // Optional: for receiving commands
}

void mqtt_connect() {
    while (!mqtt.connected()) {
        Serial.print("Connecting to MQTT...");
        
        if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
            Serial.println("connected!");
            // Subscribe to command topics if needed
            // mqtt.subscribe("factory/dosing/commands");
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqtt.state());
            Serial.println(" retrying in 5 seconds");
            delay(5000);
        }
    }
}

void loop() {
    if (!mqtt.connected()) {
        mqtt_connect();
    }
    mqtt.loop();
    // Your other code...
}
```

### Publishing Functions

#### Dosing/Consumption Data

```cpp
void publish_dose_complete(int pump_num, const char* chemical, 
                          float target_g, float actual_g, 
                          const char* recipe, const char* mode,
                          uint32_t duration_ms) {
    
    char payload[512];
    
    // Create JSON payload
    snprintf(payload, sizeof(payload),
        "{"
        "\"device_id\":\"%s\","
        "\"pump\":%d,"
        "\"chemical\":\"%s\","
        "\"target_g\":%.2f,"
        "\"actual_g\":%.2f,"
        "\"error_g\":%.2f,"
        "\"recipe\":\"%s\","
        "\"mode\":\"%s\","
        "\"duration_ms\":%lu,"
        "\"timestamp\":%lu"
        "}",
        MQTT_CLIENT_ID,
        pump_num,
        chemical,
        target_g,
        actual_g,
        actual_g - target_g,
        recipe,
        mode,
        duration_ms,
        millis()
    );
    
    bool success = mqtt.publish(TOPIC_DOSING, payload);
    
    if (success) {
        Serial.printf("Published dose: Pump %d, %s, %.2fg\n", 
                     pump_num, chemical, actual_g);
    } else {
        Serial.println("Failed to publish dose data");
    }
}

// Usage example - call after successful dose completion:
void on_dose_complete(int pump, float dispensed_g) {
    const char* chemical_names[] = {"DMDEE", "T-12", "T-9", "L25B"};
    
    publish_dose_complete(
        pump + 1,                    // Pump number (1-4)
        chemical_names[pump],        // Chemical name
        current_target_g[pump],      // Target amount
        dispensed_g,                 // Actual amount
        current_recipe_name,         // Recipe name
        current_mode,                // "CATALYST" or "BDO"
        dose_duration_ms[pump]       // How long it took
    );
}
```

#### Temperature Data

```cpp
void publish_temperature(const char* location, const char* sensor_type, 
                        float temp_c) {
    char payload[256];
    
    float temp_f = (temp_c * 9.0/5.0) + 32.0;
    
    snprintf(payload, sizeof(payload),
        "{"
        "\"device_id\":\"%s\","
        "\"location\":\"%s\","
        "\"sensor_type\":\"%s\","
        "\"temperature_c\":%.2f,"
        "\"temperature_f\":%.2f"
        "}",
        MQTT_CLIENT_ID,
        location,
        sensor_type,
        temp_c,
        temp_f
    );
    
    mqtt.publish(TOPIC_TEMPERATURE, payload);
}

// Usage - call periodically (e.g., every 10 seconds)
void monitor_temperatures() {
    float temp_motor1 = read_temperature_sensor(MOTOR_1_PIN);
    float temp_motor2 = read_temperature_sensor(MOTOR_2_PIN);
    float temp_ambient = read_temperature_sensor(AMBIENT_PIN);
    
    publish_temperature("pump_motor_1", "thermocouple_k", temp_motor1);
    publish_temperature("pump_motor_2", "thermocouple_k", temp_motor2);
    publish_temperature("ambient", "DHT22", temp_ambient);
}
```

#### Vibration Data

```cpp
void publish_vibration(int pump_num, const char* axis, 
                      float acceleration_g, float velocity_mm_s, 
                      float rms) {
    char payload[256];
    
    snprintf(payload, sizeof(payload),
        "{"
        "\"device_id\":\"%s\","
        "\"pump\":%d,"
        "\"axis\":\"%s\","
        "\"acceleration_g\":%.3f,"
        "\"velocity_mm_s\":%.2f,"
        "\"rms\":%.3f"
        "}",
        MQTT_CLIENT_ID,
        pump_num,
        axis,
        acceleration_g,
        velocity_mm_s,
        rms
    );
    
    mqtt.publish(TOPIC_VIBRATION, payload);
}

// Usage - call from vibration monitoring task
void monitor_vibration() {
    for (int pump = 1; pump <= 4; pump++) {
        float accel_x = read_accelerometer_x(pump);
        float accel_y = read_accelerometer_y(pump);
        float accel_z = read_accelerometer_z(pump);
        
        float rms = calculate_rms(accel_x, accel_y, accel_z);
        
        publish_vibration(pump, "x", accel_x, 0, rms);
        publish_vibration(pump, "y", accel_y, 0, rms);
        publish_vibration(pump, "z", accel_z, 0, rms);
    }
}
```

#### Inventory Tracking

```cpp
struct ChemicalInventory {
    const char* name;
    float container_capacity_g;
    float current_level_g;
};

ChemicalInventory inventory[4] = {
    {"DMDEE", 20000.0, 20000.0},
    {"T-12", 15000.0, 15000.0},
    {"T-9", 10000.0, 10000.0},
    {"L25B", 10000.0, 10000.0}
};

void update_inventory(int pump, float dispensed_g) {
    inventory[pump].current_level_g -= dispensed_g;
    
    char payload[256];
    float percent = (inventory[pump].current_level_g / 
                    inventory[pump].container_capacity_g) * 100.0;
    
    snprintf(payload, sizeof(payload),
        "{"
        "\"device_id\":\"%s\","
        "\"chemical\":\"%s\","
        "\"remaining_g\":%.2f,"
        "\"capacity_g\":%.2f,"
        "\"percent_full\":%.1f"
        "}",
        MQTT_CLIENT_ID,
        inventory[pump].name,
        inventory[pump].current_level_g,
        inventory[pump].container_capacity_g,
        percent
    );
    
    mqtt.publish(TOPIC_INVENTORY, payload);
    
    // Alert if low
    if (percent < 20.0) {
        Serial.printf("⚠️  LOW INVENTORY: %s at %.1f%%\n", 
                     inventory[pump].name, percent);
    }
}
```

#### Batch Events

```cpp
void publish_batch_event(const char* event, const char* recipe, 
                        int total_pumps) {
    char payload[256];
    
    snprintf(payload, sizeof(payload),
        "{"
        "\"device_id\":\"%s\","
        "\"event\":\"%s\","
        "\"recipe\":\"%s\","
        "\"pumps\":%d,"
        "\"timestamp\":%lu"
        "}",
        MQTT_CLIENT_ID,
        event,
        recipe,
        total_pumps,
        millis()
    );
    
    mqtt.publish(TOPIC_BATCH, payload);
}

// Usage:
// publish_batch_event("start", "CU-85", 2);
// ... dosing occurs ...
// publish_batch_event("complete", "CU-85", 2);
```

### NTP Time Synchronization

```cpp
void setup_time() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    Serial.print("Waiting for NTP time sync: ");
    
    time_t now = time(nullptr);
    while (now < 8 * 3600 * 2) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.println(" synchronized!");
    
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.printf("Current time: %s", asctime(&timeinfo));
}

// Call in setup():
// setup_time();
```

---

## MQTT Configuration

### Mosquitto Setup

#### mosquitto.conf

```conf
# /etc/mosquitto/mosquitto.conf

# Basic settings
listener 1883
protocol mqtt

# Allow anonymous connections (for testing)
# Remove this in production and use authentication
allow_anonymous true

# Persistence
persistence true
persistence_location /mosquitto/data/

# Logging
log_dest file /mosquitto/log/mosquitto.log
log_dest stdout
log_type all

# Authentication (uncomment for production)
# password_file /mosquitto/config/passwd
# allow_anonymous false
```

#### Creating Password File (Production)

```bash
# Create password file
mosquitto_passwd -c /etc/mosquitto/passwd username

# Add additional users
mosquitto_passwd /etc/mosquitto/passwd another_user

# Restart mosquitto
sudo systemctl restart mosquitto
```

---

## Telegraf Configuration

### /etc/telegraf/telegraf.d/dosing_system.conf

```toml
# =============================================================================
# DOSING SYSTEM MQTT CONSUMER
# =============================================================================

# Global tags (applied to all measurements)
[global_tags]
  facility = "main_factory"
  system = "chemical_dosing"

# -----------------------------------------------------------------------------
# Dosing Consumption Data
# -----------------------------------------------------------------------------
[[inputs.mqtt_consumer]]
  servers = ["tcp://localhost:1883"]
  topics = ["factory/dosing/consumption"]
  
  # Authentication (if enabled in Mosquitto)
  # username = "telegraf"
  # password = "your_password"
  
  data_format = "json"
  
  # Extract these fields as tags (indexed, for filtering)
  tag_keys = ["device_id", "pump", "chemical", "recipe", "mode"]
  
  # These should remain as string fields (not parsed as numbers)
  json_string_fields = ["chemical", "recipe", "mode", "device_id"]
  
  # Name of the measurement in InfluxDB
  name_override = "dosing_consumption"

# -----------------------------------------------------------------------------
# Equipment Temperature Data
# -----------------------------------------------------------------------------
[[inputs.mqtt_consumer]]
  servers = ["tcp://localhost:1883"]
  topics = ["factory/equipment/temperature"]
  
  data_format = "json"
  tag_keys = ["device_id", "location", "sensor_type"]
  json_string_fields = ["location", "sensor_type", "device_id"]
  name_override = "equipment_temperature"

# -----------------------------------------------------------------------------
# Equipment Vibration Data
# -----------------------------------------------------------------------------
[[inputs.mqtt_consumer]]
  servers = ["tcp://localhost:1883"]
  topics = ["factory/equipment/vibration"]
  
  data_format = "json"
  tag_keys = ["device_id", "pump", "axis"]
  json_string_fields = ["axis", "device_id"]
  name_override = "equipment_vibration"

# -----------------------------------------------------------------------------
# Inventory Levels
# -----------------------------------------------------------------------------
[[inputs.mqtt_consumer]]
  servers = ["tcp://localhost:1883"]
  topics = ["factory/inventory/levels"]
  
  data_format = "json"
  tag_keys = ["device_id", "chemical", "container_id"]
  json_string_fields = ["chemical", "container_id", "device_id"]
  name_override = "inventory_levels"

# -----------------------------------------------------------------------------
# Batch Events
# -----------------------------------------------------------------------------
[[inputs.mqtt_consumer]]
  servers = ["tcp://localhost:1883"]
  topics = ["factory/batch/events"]
  
  data_format = "json"
  tag_keys = ["device_id", "event", "recipe"]
  json_string_fields = ["event", "recipe", "device_id"]
  name_override = "batch_events"

# =============================================================================
# OUTPUT TO INFLUXDB v2
# =============================================================================

[[outputs.influxdb_v2]]
  urls = ["http://localhost:8086"]
  
  # API token from InfluxDB
  token = "your-influxdb-token-here"
  
  # Organization and bucket
  organization = "my_organization"
  bucket = "factory_metrics"
  
  # Timeout
  timeout = "5s"
```

### Testing Telegraf Configuration

```bash
# Test configuration syntax
telegraf --config /etc/telegraf/telegraf.d/dosing_system.conf --test

# Run telegraf with verbose output
telegraf --config /etc/telegraf/telegraf.d/dosing_system.conf --debug
```

---

## InfluxDB Setup

### Create Organization and Bucket

```bash
# Using InfluxDB CLI
influx setup \
  --username admin \
  --password your-password \
  --org my_organization \
  --bucket factory_metrics \
  --retention 17520h \
  --force

# Or create bucket separately
influx bucket create \
  --name factory_metrics \
  --org my_organization \
  --retention 17520h  # 24 months
```

### Create API Token for Telegraf

```bash
# Create token with write access to factory_metrics bucket
influx auth create \
  --org my_organization \
  --read-bucket factory_metrics \
  --write-bucket factory_metrics \
  --description "Telegraf write token"

# Token will be printed - save this for telegraf.conf
```

### Downsampling Task (Optional)

For long-term storage efficiency, create a downsampling task:

```javascript
// Create daily aggregation bucket
influx bucket create \
  --name factory_metrics_daily \
  --org my_organization \
  --retention 87600h  // 10 years

// Create downsampling task
option task = {name: "downsample_dosing_daily", every: 1h}

from(bucket: "factory_metrics")
  |> range(start: -2h)
  |> filter(fn: (r) => r._measurement == "dosing_consumption")
  |> aggregateWindow(every: 1d, fn: sum, createEmpty: false)
  |> to(bucket: "factory_metrics_daily")
```

---

## Grafana Dashboards

### Add InfluxDB Data Source

1. Navigate to **Configuration → Data Sources → Add data source**
2. Select **InfluxDB**
3. Configure:
   - **Query Language:** Flux
   - **URL:** `http://influxdb:8086` (Docker) or `http://localhost:8086`
   - **Organization:** `my_organization`
   - **Token:** Your InfluxDB API token
   - **Default Bucket:** `factory_metrics`
4. Click **Save & Test**

### Dashboard Panel Examples

#### Panel 1: Total Consumption by Chemical (Today)

```flux
from(bucket: "factory_metrics")
  |> range(start: today())
  |> filter(fn: (r) => r._measurement == "dosing_consumption")
  |> filter(fn: (r) => r._field == "actual_g")
  |> group(columns: ["chemical"])
  |> sum()
  |> yield(name: "total_consumption")
```

**Visualization:** Bar Chart or Stat Panel

#### Panel 2: Consumption Over Time (Time Series)

```flux
from(bucket: "factory_metrics")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "dosing_consumption")
  |> filter(fn: (r) => r._field == "actual_g")
  |> aggregateWindow(every: 1h, fn: sum, createEmpty: false)
  |> group(columns: ["chemical"])
```

**Visualization:** Time Series (Line Chart)

#### Panel 3: Dosing Accuracy Distribution

```flux
from(bucket: "factory_metrics")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "dosing_consumption")
  |> filter(fn: (r) => r._field == "error_g")
  |> histogram(bins: [-2.0, -1.0, -0.5, 0.5, 1.0, 2.0])
```

**Visualization:** Histogram

#### Panel 4: Pump Motor Temperatures

```flux
from(bucket: "factory_metrics")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "equipment_temperature")
  |> filter(fn: (r) => r.location =~ /pump_motor_/)
  |> filter(fn: (r) => r._field == "temperature_c")
```

**Visualization:** Time Series with threshold lines at 60°C (warning) and 80°C (critical)

#### Panel 5: Pump Vibration RMS

```flux
from(bucket: "factory_metrics")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r._measurement == "equipment_vibration")
  |> filter(fn: (r) => r._field == "rms")
  |> group(columns: ["pump"])
```

**Visualization:** Time Series

#### Panel 6: Current Inventory Levels

```flux
from(bucket: "factory_metrics")
  |> range(start: -5m)
  |> filter(fn: (r) => r._measurement == "inventory_levels")
  |> filter(fn: (r) => r._field == "percent_full")
  |> last()
  |> group(columns: ["chemical"])
```

**Visualization:** Gauge (with thresholds: <20% red, 20-50% yellow, >50% green)

#### Panel 7: Batch Count by Recipe (Today)

```flux
from(bucket: "factory_metrics")
  |> range(start: today())
  |> filter(fn: (r) => r._measurement == "batch_events")
  |> filter(fn: (r) => r.event == "complete")
  |> group(columns: ["recipe"])
  |> count()
```

**Visualization:** Stat or Bar Chart

#### Panel 8: Equipment Health Score (Multi-Measurement Join)

```flux
// Get latest temperature data
temp = from(bucket: "factory_metrics")
  |> range(start: -5m)
  |> filter(fn: (r) => r._measurement == "equipment_temperature")
  |> filter(fn: (r) => r.location =~ /pump_motor_/)
  |> filter(fn: (r) => r._field == "temperature_c")
  |> last()
  |> group(columns: ["pump"])

// Get latest vibration data
vib = from(bucket: "factory_metrics")
  |> range(start: -5m)
  |> filter(fn: (r) => r._measurement == "equipment_vibration")
  |> filter(fn: (r) => r._field == "rms")
  |> last()
  |> group(columns: ["pump"])

// Join and calculate health score
// Health = 100 - (temp_penalty + vibration_penalty)
join(tables: {temp: temp, vib: vib}, on: ["pump"])
  |> map(fn: (r) => ({
      pump: r.pump,
      health_score: 100.0 - 
        (if r._value_temp > 60.0 then (r._value_temp - 60.0) else 0.0) - 
        (if r._value_vib > 0.1 then (r._value_vib - 0.1) * 100.0 else 0.0)
    }))
```

**Visualization:** Gauge or Stat (per pump)

### Alert Rules

#### Low Inventory Alert

```flux
from(bucket: "factory_metrics")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "inventory_levels")
  |> filter(fn: (r) => r._field == "percent_full")
  |> last()
  |> filter(fn: (r) => r._value < 20.0)
```

**Alert Condition:** When query returns data  
**Notification:** Email/Slack: "⚠️ Low inventory: {chemical} at {percent_full}%"

#### High Temperature Alert

```flux
from(bucket: "factory_metrics")
  |> range(start: -5m)
  |> filter(fn: (r) => r._measurement == "equipment_temperature")
  |> filter(fn: (r) => r._field == "temperature_c")
  |> last()
  |> filter(fn: (r) => r._value > 80.0)
```

**Alert Condition:** When query returns data  
**Notification:** "🔥 High temperature alert: {location} at {temperature_c}°C"

#### High Vibration Alert

```flux
from(bucket: "factory_metrics")
  |> range(start: -1m)
  |> filter(fn: (r) => r._measurement == "equipment_vibration")
  |> filter(fn: (r) => r._field == "rms")
  |> mean()
  |> filter(fn: (r) => r._value > 0.15)
```

**Alert Condition:** When query returns data  
**Notification:** "⚠️ High vibration detected: Pump {pump}, RMS {rms}"

---

## Docker Compose Setup

### docker-compose.yml

```yaml
version: '3.8'

services:
  # MQTT Broker
  mosquitto:
    image: eclipse-mosquitto:2.0
    container_name: mosquitto
    hostname: mosquitto
    ports:
      - "1883:1883"
      - "9001:9001"
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
      - ./mosquitto/log:/mosquitto/log
    restart: unless-stopped

  # InfluxDB Time Series Database
  influxdb:
    image: influxdb:2.7
    container_name: influxdb
    ports:
      - "8086:8086"
    environment:
      - DOCKER_INFLUXDB_INIT_MODE=setup
      - DOCKER_INFLUXDB_INIT_USERNAME=admin
      - DOCKER_INFLUXDB_INIT_PASSWORD=changeme123
      - DOCKER_INFLUXDB_INIT_ORG=my_organization
      - DOCKER_INFLUXDB_INIT_BUCKET=factory_metrics
      - DOCKER_INFLUXDB_INIT_RETENTION=17520h  # 24 months
      - DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=my-super-secret-auth-token
    volumes:
      - influxdb-storage:/var/lib/influxdb2
      - influxdb-config:/etc/influxdb2
    restart: unless-stopped

  # Telegraf Data Collector
  telegraf:
    image: telegraf:1.28
    container_name: telegraf
    volumes:
      - ./telegraf/telegraf.conf:/etc/telegraf/telegraf.conf:ro
    depends_on:
      - influxdb
      - mosquitto
    restart: unless-stopped

  # Grafana Visualization
  grafana:
    image: grafana/grafana:10.2.0
    container_name: grafana
    ports:
      - "3000:3000"
    environment:
      - GF_SECURITY_ADMIN_USER=admin
      - GF_SECURITY_ADMIN_PASSWORD=admin
      - GF_INSTALL_PLUGINS=
    volumes:
      - grafana-storage:/var/lib/grafana
      - ./grafana/provisioning:/etc/grafana/provisioning
    depends_on:
      - influxdb
    restart: unless-stopped

volumes:
  influxdb-storage:
  influxdb-config:
  grafana-storage:

networks:
  default:
    name: iot-network
```

### Start the Stack

```bash
# Create directory structure
mkdir -p mosquitto/config mosquitto/data mosquitto/log
mkdir -p telegraf
mkdir -p grafana/provisioning

# Copy mosquitto.conf to mosquitto/config/
# Copy telegraf.conf to telegraf/

# Start services
docker-compose up -d

# Check logs
docker-compose logs -f

# Stop services
docker-compose down
```

### Access Services

- **Mosquitto MQTT:** `mqtt://localhost:1883`
- **InfluxDB:** `http://localhost:8086`
- **Grafana:** `http://localhost:3000` (admin/admin)

---

## Best Practices

### Data Organization

✅ **DO:**

- Use separate measurements for different data types
- Use consistent `device_id` tags across all measurements
- Store metadata in tags (indexed, strings only)
- Store measured values in fields (typed: int, float, bool, string)
- Use descriptive measurement names: `dosing_consumption`, not `data1`
- Keep field names consistent across all devices

❌ **DON'T:**

- Mix unrelated data in one measurement
- Embed data in measurement names (e.g., `pump1_temperature`)
- Use fields as tags or vice versa
- Create thousands of measurements (group logically)
- Use spaces or special characters in names

### Tag Strategy

**Tags are indexed and used for filtering:**

- Device identifiers: `device_id`, `pump`, `sensor_id`
- Location: `location`, `zone`, `building`
- Type: `chemical`, `recipe`, `mode`, `sensor_type`
- Status: `status`, `state`, `event`

**Fields store actual measurements:**

- Numeric values: `temperature_c`, `actual_g`, `rms`
- Calculated values: `error_g`, `percent_full`, `duration_ms`
- Status codes (as integers): `error_code`, `state_id`

### Performance Tips

1. **Batch writes** - Send multiple data points in one MQTT message when possible
2. **Appropriate sampling rates:**
   - Dosing events: On completion (event-driven)
   - Temperature: Every 10-30 seconds
   - Vibration: Every 1-5 seconds
   - Inventory: After each dose or on change
3. **Use downsampling** for long-term storage
4. **Set appropriate retention policies** per measurement type
5. **Index commonly filtered tags**

### Security

1. **Enable MQTT authentication** (username/password)
2. **Use TLS/SSL** for MQTT connections in production
3. **Restrict InfluxDB API tokens** (read-only vs read-write)
4. **Change default passwords** for Grafana
5. **Use firewall rules** to restrict access to ports
6. **Regular backups** of InfluxDB data

### Monitoring

1. **Monitor Telegraf metrics** - Check for dropped messages
2. **Set up alerts** for:
   - Low inventory levels
   - High temperatures
   - Abnormal vibration
   - Dosing errors (>2g deviation)
   - System offline (no data for X minutes)
3. **Dashboard health checks** - Create a system status panel
4. **Log rotation** - Ensure MQTT and Telegraf logs don't fill disk

---

## Tutorials and Resources

### Recommended Tutorials

1. **Gabriel Tanner - Complete MQTT→Telegraf→InfluxDB→Grafana**  
   https://gabrieltanner.org/blog/grafana-sensor-visualization/  
   - Best match for this exact stack
   - Includes Docker Compose setup
   - Step-by-step with examples

2. **Random Nerd Tutorials - ESP32 with InfluxDB**  
   https://randomnerdtutorials.com/esp32-influxdb/  
   - ESP32-specific implementation
   - Arduino library setup
   - Good for beginners

3. **Surviving with Android - Raspberry Pi IoT System**  
   https://www.survivingwithandroid.com/raspberry-pi-iot-sensors-influxdb-mqtt-grafana/  
   - Clear architecture diagrams
   - Docker-based setup
   - Good troubleshooting tips

4. **DIY IoT - MQTT Data with InfluxDB and Grafana**  
   https://diyi0t.com/visualize-mqtt-data-with-influxdb-and-grafana/  
   - Detailed Grafana dashboard creation
   - Python bridge examples
   - Topic structure best practices

### Official Documentation

- **InfluxDB:** https://docs.influxdata.com/
- **Telegraf:** https://docs.influxdata.com/telegraf/
- **Grafana:** https://grafana.com/docs/
- **Mosquitto:** https://mosquitto.org/documentation/
- **PubSubClient (Arduino MQTT):** https://pubsubclient.knolleary.net/

### GitHub Examples

- **ESP32 + MQTT + Telegraf + InfluxDB:**  
  https://github.com/vorby01/esp32-mqtt-telegraf-influxdb

### Tools

- **MQTT.fx** - MQTT client for testing (GUI)
- **mosquitto_pub/sub** - Command-line MQTT client
- **InfluxDB CLI** - Command-line database management
- **Grafana Explore** - Query builder and data exploration

---

## Implementation Checklist

### Phase 1: Backend Setup

- [ ] Install Docker and Docker Compose
- [ ] Create `docker-compose.yml` file
- [ ] Start Mosquitto, InfluxDB, Telegraf, Grafana
- [ ] Create InfluxDB organization and bucket
- [ ] Create InfluxDB API token for Telegraf
- [ ] Configure Telegraf MQTT consumer
- [ ] Test MQTT→Telegraf→InfluxDB pipeline with `mosquitto_pub`

### Phase 2: ESP32 Integration

- [ ] Add WiFi and PubSubClient libraries to project
- [ ] Configure MQTT broker settings
- [ ] Implement NTP time synchronization
- [ ] Add `publish_dose_complete()` function
- [ ] Integrate publishing into dosing state machine
- [ ] Test with real dosing cycle
- [ ] Add temperature monitoring (if sensors available)
- [ ] Add vibration monitoring (if sensors available)

### Phase 3: Grafana Dashboards

- [ ] Add InfluxDB as Grafana data source
- [ ] Create "Production Overview" dashboard
- [ ] Add consumption panels (daily, weekly, by chemical)
- [ ] Add temperature monitoring panel
- [ ] Add vibration monitoring panel
- [ ] Add inventory level gauges
- [ ] Create alert rules for low inventory
- [ ] Create alert rules for high temperature
- [ ] Test alerts with simulated conditions

### Phase 4: Production Deployment

- [ ] Enable MQTT authentication
- [ ] Configure retention policies
- [ ] Set up downsampling tasks
- [ ] Configure backup strategy
- [ ] Document dashboard usage for operators
- [ ] Train staff on alert response procedures
- [ ] Monitor system for one week
- [ ] Adjust sampling rates and thresholds as needed

---

## Troubleshooting

### No Data in InfluxDB

1. Check Telegraf logs: `docker logs telegraf`
2. Verify MQTT messages: `mosquitto_sub -h localhost -t factory/#`
3. Test InfluxDB connection: `influx ping`
4. Check Telegraf config syntax: `telegraf --config telegraf.conf --test`

### ESP32 Not Connecting to MQTT

1. Verify WiFi connection
2. Check broker IP address and port
3. Test broker with: `mosquitto_sub -h <broker_ip> -t test`
4. Check firewall rules
5. Enable debug output in ESP32 code

### Grafana Shows No Data

1. Verify InfluxDB data source configuration
2. Test query in InfluxDB UI first
3. Check time range in Grafana panel
4. Verify measurement and field names match
5. Check bucket name in query

### High Message Loss

1. Increase MQTT broker memory limits
2. Add QoS levels to MQTT publishes
3. Reduce ESP32 publish frequency
4. Check network bandwidth
5. Monitor Telegraf buffer metrics

---

## Support and Maintenance

### Regular Maintenance Tasks

- **Daily:** Check dashboard for anomalies
- **Weekly:** Review alert history and logs
- **Monthly:** Verify backup integrity, check disk usage
- **Quarterly:** Update Docker images, review retention policies
- **Annually:** Audit security settings, refresh credentials

### Backup Strategy

```bash
# Backup InfluxDB
docker exec influxdb influx backup /backup/influxdb-$(date +%Y%m%d)

# Backup Grafana dashboards
docker exec grafana tar czf /backup/grafana-$(date +%Y%m%d).tar.gz /var/lib/grafana

# Copy to external storage
rsync -av /backup/ remote-server:/backups/dosing-system/
```

---

**Document Version:** 1.0  
**Last Updated:** October 29, 2025  
**Author:** Chemical Dosing System Integration Team  
**Status:** Production Ready

---

## Quick Start Summary

1. **Deploy backend:** `docker-compose up -d`
2. **Add to ESP32:** WiFi + PubSubClient + publish functions
3. **Configure Telegraf:** Create `telegraf.conf` with MQTT consumers
4. **Create dashboards:** Build panels in Grafana
5. **Monitor:** Watch data flow in real-time
6. **Alert:** Set up notifications for critical conditions

**That's it!** You now have a production-grade material consumption tracking system integrated with your existing chemical dosing infrastructure.
