# MQTT Data Sharing Setup Guide

## Overview

This peristaltic pump dosing system now includes MQTT data sharing capabilities for integration with Grafana dashboards via Telegraf and InfluxDB.

**Data Flow:** ESP32 → MQTT → Telegraf → InfluxDB → Grafana

## What Data is Published

The system publishes the following data types to MQTT:

### 1. Dosing/Consumption Data
Published when each pump completes a dosing operation.

**Topic:** `factory/dosing/consumption`

**Data includes:**
- Device ID
- Pump number (1-4)
- Chemical name (DMDEE, T-12, T-9, L25B)
- Target amount (grams)
- Actual amount dispensed (grams)
- Error (actual - target)
- Recipe name (CU-85, CU-65/75, FG-85/95)
- Mode (CATALYST or BDO)
- Duration (milliseconds)
- Timestamp

### 2. Batch Events
Published at the start and completion of each dosing batch.

**Topic:** `factory/batch/events`

**Data includes:**
- Device ID
- Event type (start/complete)
- Recipe name
- Number of pumps used
- Timestamp

### 3. Inventory Levels
Published after each pump dispenses material, showing remaining inventory.

**Topic:** `factory/inventory/levels`

**Data includes:**
- Device ID
- Chemical name
- Remaining amount (grams)
- Container capacity (grams)
- Percentage full

## Quick Start Configuration

### Step 1: Configure WiFi and MQTT Settings

Edit `config.h` and update these settings:

```cpp
// WiFi Configuration
#define WIFI_SSID       "YOUR_WIFI_SSID"        // Your WiFi network name
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"    // Your WiFi password

// MQTT Broker
#define MQTT_BROKER         "192.168.1.100"     // IP address of your MQTT broker
#define MQTT_PORT           1883                // MQTT port (usually 1883)
#define MQTT_CLIENT_ID      "dosing_system_01"  // Unique device identifier

// Optional: MQTT Authentication (uncomment if your broker requires it)
// #define MQTT_USE_AUTH
#define MQTT_USER           "username"          // MQTT username
#define MQTT_PASSWORD       "password"          // MQTT password

// NTP Time Server (for accurate timestamps)
#define NTP_SERVER          "pool.ntp.org"
#define GMT_OFFSET_SEC      -18000              // Your timezone offset (-5h for EST)
#define DAYLIGHT_OFFSET_SEC 3600                // DST offset (+1h)
```

### Step 2: Adjust Inventory Capacities

Update the inventory capacities to match your actual container sizes in `config.h`:

```cpp
// Default inventory capacities (grams)
#define INVENTORY_DMDEE_CAPACITY_G  20000.0f    // 20 kg
#define INVENTORY_T12_CAPACITY_G    15000.0f    // 15 kg
#define INVENTORY_T9_CAPACITY_G     10000.0f    // 10 kg
#define INVENTORY_L25B_CAPACITY_G   10000.0f    // 10 kg
```

### Step 3: Install Required Libraries

Make sure you have these Arduino libraries installed:
- **PubSubClient** (MQTT client library)
- **WiFi** (ESP32 WiFi - built-in)
- **ArduinoJson** (for JSON parsing)

### Step 4: Upload the Code

Compile and upload the modified code to your ESP32.

### Step 5: Monitor Serial Output

Open the Serial Monitor (115200 baud) to verify:
- WiFi connection successful
- NTP time synchronized
- MQTT broker connection established
- Data being published during dosing operations

## Backend Setup Options

You have **two options** for the time-series database backend:

### Option 1: TimescaleDB (Recommended - SQL-based)
**Best for:** SQL users, complex queries, joining with relational data

**Complete guide:** `MQTT_TIMESCALEDB_INTEGRATION_GUIDE.md`

Quick start:
```bash
# 1. Start the stack with Docker Compose
docker-compose -f docker-compose-timescaledb.yml up -d

# 2. Verify TimescaleDB is ready
docker exec -it dosing-timescaledb psql -U telegraf -d factory_metrics -c "\dt"

# 3. Check data is flowing
docker logs dosing-telegraf
```

**Advantages:**
- ✅ Familiar SQL query language
- ✅ PostgreSQL ecosystem and tools
- ✅ JOINs with relational data
- ✅ Strong ACID guarantees
- ✅ No vendor lock-in

### Option 2: InfluxDB (Time-series specialist)
**Best for:** Time-series specialists, InfluxQL/Flux users

**Complete guide:** `MQTT_INFLUXDB_INTEGRATION_GUIDE.md`

Quick start:
```bash
# Follow the guide to set up InfluxDB v2
```

**Advantages:**
- ✅ Purpose-built for time-series
- ✅ Flux query language (powerful for time-series)
- ✅ Built-in downsampling
- ✅ Good for metrics-focused workflows

### Quick Backend Checklist (TimescaleDB):

1. ✅ Copy `mosquitto.conf` to `mosquitto/config/`
2. ✅ Run `docker-compose -f docker-compose-timescaledb.yml up -d`
3. ✅ Verify tables created: `docker exec -it dosing-timescaledb psql -U telegraf -d factory_metrics -c "SELECT * FROM timescaledb_information.hypertables;"`
4. ✅ Update `telegraf-timescaledb.conf` with database password
5. ✅ Configure Grafana data source (PostgreSQL)
6. ✅ Create dashboards using SQL queries

## Testing the Integration

### Test 1: Verify MQTT Messages

Use MQTT.fx or mosquitto_sub to subscribe to all topics:

```bash
mosquitto_sub -h YOUR_MQTT_BROKER_IP -t "factory/#" -v
```

You should see messages when:
- System starts up
- Dosing batch starts
- Each pump completes
- Dosing batch completes
- Inventory updates

### Test 2: Check Telegraf

Verify Telegraf is receiving and parsing messages:

```bash
telegraf --config /etc/telegraf/telegraf.conf --test
```

### Test 3: Query InfluxDB

Check that data is being written to InfluxDB:

```bash
influx query 'from(bucket:"factory_metrics") |> range(start: -1h) |> filter(fn: (r) => r._measurement == "dosing_consumption")'
```

### Test 4: Grafana Dashboard

1. Open Grafana (http://localhost:3000)
2. Add InfluxDB as a data source
3. Create a simple query to verify data:
   ```flux
   from(bucket: "factory_metrics")
     |> range(start: -1h)
     |> filter(fn: (r) => r._measurement == "dosing_consumption")
     |> filter(fn: (r) => r._field == "actual_g")
   ```

## Grafana Dashboard Ideas

### Panel 1: Total Material Consumption (Today)
Shows total grams dispensed per chemical for the current day.

### Panel 2: Dosing Accuracy
Displays error distribution to monitor pump accuracy.

### Panel 3: Batch Timeline
Timeline view of all batches completed.

### Panel 4: Inventory Levels
Gauge showing current inventory percentage for each chemical with low-level alerts.

### Panel 5: Dosing Duration
Average time per pump to identify performance issues.

### Panel 6: Recipe Usage
Pie chart or bar graph showing which recipes are used most frequently.

## Troubleshooting

### WiFi Not Connecting
- Check SSID and password in config.h
- Ensure ESP32 is within WiFi range
- Check router settings (2.4GHz required, not 5GHz)

### MQTT Not Publishing
- Verify MQTT broker is running: `systemctl status mosquitto`
- Check broker IP address and port
- Test with mosquitto_pub: `mosquitto_pub -h BROKER_IP -t test -m "hello"`
- Enable debug output in mqtt.cpp

### No Data in Grafana
1. Check MQTT messages are being received
2. Verify Telegraf is running and configured correctly
3. Check InfluxDB bucket exists and has data
4. Verify Grafana data source configuration
5. Check time range in Grafana panels

### Inventory Not Tracking
- Inventory starts at full capacity on boot
- Inventory is stored in RAM (resets on power cycle)
- Future enhancement: save inventory to flash/EEPROM

### Timestamps Incorrect
- Verify NTP server is accessible
- Check GMT_OFFSET_SEC matches your timezone
- Update DAYLIGHT_OFFSET_SEC for DST
- Monitor Serial output for "Time synced" message

## System Architecture

```
┌──────────────┐
│    ESP32     │  Peristaltic Pump Dosing System
│  Controller  │  - 4 pumps (DMDEE, T-12, T-9, L25B)
│              │  - Weight-based dosing
│              │  - Recipe management
└──────┬───────┘
       │ WiFi
       ▼
┌──────────────────────────────────────────────┐
│         MQTT Broker (Mosquitto)              │
│  Topics:                                     │
│    - factory/dosing/consumption              │
│    - factory/batch/events                    │
│    - factory/inventory/levels                │
└──────┬───────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│         Telegraf (MQTT Consumer)             │
│  - Subscribes to MQTT topics                 │
│  - Parses JSON payloads                      │
│  - Writes to InfluxDB                        │
└──────┬───────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│         InfluxDB v2 (Time Series DB)         │
│  Bucket: factory_metrics                     │
│    - dosing_consumption                      │
│    - batch_events                            │
│    - inventory_levels                        │
└──────┬───────────────────────────────────────┘
       │
       ▼
┌──────────────────────────────────────────────┐
│         Grafana (Visualization)              │
│  - Real-time dashboards                      │
│  - Historical analysis                       │
│  - Alert notifications                       │
└──────────────────────────────────────────────┘
```

## Example MQTT Payloads

### Dosing Consumption Event
```json
{
  "device_id": "dosing_system_01",
  "pump": 2,
  "chemical": "T-12",
  "target_g": 40.00,
  "actual_g": 40.15,
  "error_g": 0.15,
  "recipe": "CU-85",
  "mode": "CATALYST",
  "duration_ms": 58432,
  "timestamp": 1698765432
}
```

### Batch Event
```json
{
  "device_id": "dosing_system_01",
  "event": "complete",
  "recipe": "CU-85",
  "pumps": 2,
  "timestamp": 1698765555
}
```

### Inventory Update
```json
{
  "device_id": "dosing_system_01",
  "chemical": "T-12",
  "remaining_g": 14959.85,
  "capacity_g": 15000.00,
  "percent_full": 99.7
}
```

## Files Modified/Added

### New Files:
- `mqtt.h` - MQTT function declarations
- `mqtt.cpp` - MQTT implementation
- `MQTT_SETUP_README.md` - This file

### Modified Files:
- `config.h` - Added WiFi, MQTT, NTP, and inventory settings
- `main.cpp` - Integrated MQTT publishing and inventory tracking

### Reference:
- `MQTT_INFLUXDB_INTEGRATION_GUIDE.md` - Complete integration guide

## Support

For issues or questions:
1. Check the Serial Monitor output for error messages
2. Review the troubleshooting section above
3. Consult the detailed integration guide
4. Test each component individually (WiFi, MQTT, Telegraf, InfluxDB)

## Future Enhancements

Potential improvements:
- [ ] Persistent inventory storage (save to EEPROM/Flash)
- [ ] Temperature sensor integration (motor temps)
- [ ] Vibration monitoring (pump health)
- [ ] OTA firmware updates via MQTT
- [ ] Web dashboard on ESP32
- [ ] Email/SMS alerts via Grafana
- [ ] Production/maintenance mode tracking
- [ ] Recipe download from cloud

## Version History

**v2.0** - October 30, 2025
- Added MQTT data sharing
- Integrated with Telegraf/InfluxDB/Grafana
- Added inventory tracking
- Added NTP time synchronization
- Enhanced logging and diagnostics

---

**Ready to track your chemical consumption like a pro!** 🎯📊
