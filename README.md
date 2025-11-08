# BDO Pneumatic Pump Controller
## ESP32-Based Control System with WebUI and MQTT Integration

![Version](https://img.shields.io/badge/version-1.0.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32-green)
![Framework](https://img.shields.io/badge/framework-ESP--IDF-orange)

**Complete pneumatic pump control system with real-time monitoring, web interface, and factory data integration.**

---

## 📋 Table of Contents

- [Overview](#overview)
- [Features](#features)
- [System Architecture](#system-architecture)
- [Hardware Requirements](#hardware-requirements)
- [Quick Start](#quick-start)
- [Configuration](#configuration)
- [WebUI](#webui)
- [MQTT Integration](#mqtt-integration)
- [Database Setup](#database-setup)
- [API Reference](#api-reference)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)

---

## 🎯 Overview

This project implements a production-ready pneumatic pump controller for industrial filling operations up to 250 lbs using the SMC ITV2030-33N2 electropneumatic regulator and PS-IN202 scale.

### Key Capabilities

✅ **Multi-Zone Speed Control** - 4 zones (Fast, Moderate, Slow, Fine) for optimal fill accuracy
✅ **4-Stage Safety Interlock** - Mandatory safety checks before each fill
✅ **Web-Based Interface** - Real-time monitoring and control from any device
✅ **MQTT Integration** - Seamless integration with Telegraf/TimescaleDB/Grafana
✅ **High Accuracy** - ±0.3-1.0 lbs fill accuracy depending on configuration
✅ **Local Display** - 1602 LCD with rotary encoder for standalone operation

### Performance

| Metric | Value |
|--------|-------|
| Fill Capacity | 10-250 lbs |
| Fill Time (200 lbs) | ~175-180 seconds |
| Accuracy | ±0.3-1.0 lbs |
| Control Loop | 100ms (10 Hz) |
| Data Publishing | 5s (filling), 30s (idle) |

---

## 🚀 Features

### Control System

- **Multi-Zone Control**: Automatically adjusts pressure based on fill progress
  - Fast Zone (0-40%): 100% pressure
  - Moderate Zone (40-70%): 70% pressure
  - Slow Zone (70-90%): 40% pressure
  - Fine Zone (90-98%): 20% pressure

- **Safety Interlocks**: 4-stage mandatory safety checklist (LCD-based)
  1. Air line connection verification - confirm on LCD
  2. Fill hose connection verification - confirm on LCD
  3. Tank/valve positioning verification - confirm on LCD
  4. Final start confirmation - confirm on LCD
  - All confirmations use rotary encoder button
  - No separate safety buttons required!

- **Real-Time Feedback**: PNP switch monitoring from ITV2030 for pressure verification

### User Interfaces

- **WebUI**: Responsive web interface with real-time updates
  - Live fill progress visualization
  - System status monitoring
  - Remote start/stop control
  - Target weight adjustment
  - Mobile-friendly design

- **LCD Display**: Local 1602 LCD with rotary encoder
  - Standby mode with target weight
  - Fill progress with percentage
  - Safety checklist status
  - Error messages and diagnostics

### Data Integration

- **MQTT Publishing**: Factory-grade telemetry
  - Fill completion events
  - System events (startup, errors, safety checks)
  - Real-time status updates

- **TimescaleDB Storage**: Optimized time-series database
  - Automatic data compression
  - Continuous aggregates for performance
  - 90-day raw data retention
  - 2-year aggregate retention

- **Grafana Dashboards**: Production monitoring
  - Total fills and pounds dispensed
  - Fill accuracy trending
  - System uptime and health
  - Custom alerts

---

## 🏗️ System Architecture

```
┌──────────────┐
│  ESP32-DevKit│  Main Controller
│  (ESP-IDF)   │  - FreeRTOS multitasking
│              │  - WiFi connectivity
│              │  - DAC output (0-10V)
└──────┬───────┘
       │
       ├──► ITV2030-33N2 (24V) ──► Pneumatic Pump
       │     Pressure Control
       │
       ├──► PS-IN202 Scale (RS232) ──► Weight Measurement
       │
       ├──► LCD 1602 (I2C) ──► Local Display
       │
       ├──► Rotary Encoder ──► User Input + Safety Confirmations
       │
       └──► WiFi ──► MQTT Broker ──► Telegraf ──► TimescaleDB ──► Grafana
                      │
                      └──► WebUI (HTTP Server)
```

### FreeRTOS Task Architecture

| Task | Priority | Core | Function |
|------|----------|------|----------|
| Scale Task | 5 | 0 | RS232 communication, weight reading |
| Control Task | 5 | 0 | Fill state machine, zone control |
| Display Task | 4 | 1 | LCD update, encoder handling |
| Web Server Task | 3 | 1 | HTTP server, WebUI API |
| MQTT Task | 3 | 1 | MQTT client, telemetry publishing |

---

## 🛠️ Hardware Requirements

### Core Components

| Component | Specification | Qty |
|-----------|---------------|-----|
| ESP32-DevKit | 38-pin development board | 1 |
| Power Supply | 24V DC, 3-5A | 1 |
| Buck Converter | LM2596 (24V→12V, 1A) | 1 |
| Buck Converter | LM2596 (24V→5V, 3A) | 1 |
| ITV2030-33N2 | SMC electropneumatic regulator | 1 |
| PS-IN202 | Scale with RS232 interface | 1 |

### Interface Components

| Component | Specification | Qty |
|-----------|---------------|-----|
| LCD Display | 1602 with I2C backpack | 1 |
| Rotary Encoder | KY-040 or similar (includes button for safety confirms) | 1 |
| MAX3232 | RS232 level shifter module | 1 |
| LM358 | Dual op-amp (DAC amplifier) | 1 |

### Power System

```
24V DC Input
├─── ITV2030: 24V @ 100mA
├─── Buck 24V→12V: LCD backlight, LM358 op-amp
└─── Buck 24V→5V: ESP32, peripherals
```

**Total Power Consumption**: ~1.5-2A @ 24V (2.5A if using optional WS2812 LEDs)

### Detailed Schematics

See [`docs/HARDWARE_SCHEMATIC_24V.md`](docs/HARDWARE_SCHEMATIC_24V.md) for:
- Complete wiring diagrams
- Pin assignments
- Circuit schematics (DAC amplifier, RS232, safety system)
- Bill of materials with part numbers
- Assembly instructions

---

## ⚡ Quick Start

### 1. Hardware Assembly

1. **Power System**
   - Connect 24V power supply
   - Install and configure both buck converters (12V and 5V)
   - Verify voltages with multimeter

2. **ESP32 Connections**
   - Wire I2C to LCD (GPIO21/22)
   - Wire UART to MAX3232 (GPIO16/17)
   - Wire DAC to LM358 op-amp (GPIO25)
   - Connect rotary encoder (GPIO32/33/34)
   - Safety checks use LCD + encoder button (no separate buttons needed!)

3. **External Connections**
   - Connect scale RS232 cable
   - Connect ITV2030 control signal (0-10V)
   - Connect ITV2030 power (24V)
   - Connect ITV2030 PNP feedback (GPIO26)

### 2. Software Setup

#### Install PlatformIO

```bash
# Using VS Code
# Install PlatformIO IDE extension

# Or using CLI
pip install platformio
```

#### Clone and Build

```bash
git clone https://github.com/yourusername/BDO_Pump.git
cd BDO_Pump

# Configure WiFi and MQTT
nano include/config.h
# Update WIFI_SSID, WIFI_PASSWORD, MQTT_BROKER_URI

# Build firmware
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output
pio device monitor
```

### 3. Backend Setup (Docker)

```bash
# Navigate to project directory
cd BDO_Pump

# Start MQTT/Telegraf/TimescaleDB/Grafana stack
docker-compose -f docker-compose-timescaledb.yml up -d

# Initialize database
docker exec -i dosing-timescaledb psql -U telegraf -d factory_metrics < database/init-bdo-pump.sql

# Verify services
docker-compose logs -f
```

### 4. Access Interfaces

- **WebUI**: `http://<ESP32_IP>/` (find IP in serial monitor)
- **Grafana**: `http://localhost:3000` (admin/admin)
- **MQTT Broker**: `mqtt://localhost:1883`

---

## ⚙️ Configuration

### WiFi and MQTT Settings

Edit `include/config.h`:

```c
// WiFi Configuration
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// MQTT Broker
#define MQTT_BROKER_URI "mqtt://192.168.1.100:1883"
#define MQTT_DEVICE_ID "bdo_pump_01"
```

### Fill Control Parameters

```c
// Zone thresholds (percentage of target weight)
#define ZONE_FAST_END 40.0f        // 0-40%
#define ZONE_MODERATE_END 70.0f    // 40-70%
#define ZONE_SLOW_END 90.0f        // 70-90%
#define ZONE_FINE_END 98.0f        // 90-98%

// Pressure setpoints (0-100%)
#define PRESSURE_FAST 100.0f
#define PRESSURE_MODERATE 70.0f
#define PRESSURE_SLOW 40.0f
#define PRESSURE_FINE 20.0f
```

### GPIO Pin Mapping

See `include/config.h` for complete pin definitions. Key pins:

| Function | GPIO | Notes |
|----------|------|-------|
| DAC Output | 25 | 0-3.3V → amplified to 0-10V |
| Scale TX | 17 | UART2 to MAX3232 |
| Scale RX | 16 | UART2 from MAX3232 |
| LCD SDA | 21 | I2C data |
| LCD SCL | 22 | I2C clock |
| Encoder CLK | 32 | Rotary encoder A |
| Encoder DT | 33 | Rotary encoder B |
| Encoder SW | 34 | Encoder button |
| ITV Feedback | 26 | PNP switch input |

---

## 🌐 WebUI

### Features

- **Real-Time Updates**: Auto-refresh every 1 second
- **Live Progress Bar**: Visual fill progress indication
- **System Status**: State, zone, pressure, weight
- **Fill Statistics**: Daily fills and total pounds dispensed
- **Remote Control**: Start/stop fills, adjust target weight
- **Mobile Responsive**: Works on phones, tablets, desktops

### API Endpoints

#### GET /api/status

Returns current system status (JSON)

**Response:**
```json
{
  "state": "FILLING",
  "zone": "MODERATE",
  "current_weight": 125.4,
  "target_weight": 200.0,
  "pressure_pct": 70.0,
  "progress_pct": 62.7,
  "fills_today": 12,
  "total_lbs_today": 2400.5,
  "scale_online": true,
  "mqtt_connected": true
}
```

#### POST /api/start

Start fill operation (requires idle state)

**Response:**
```json
{
  "status": "success",
  "message": "Fill started (safety checks required)"
}
```

#### POST /api/stop

Stop/cancel current fill operation

**Response:**
```json
{
  "status": "success",
  "message": "Fill cancelled"
}
```

#### POST /api/set_target

Set target weight (JSON body)

**Request:**
```json
{
  "target": 200.0
}
```

**Response:**
```json
{
  "status": "success",
  "message": "Target weight updated"
}
```

---

## 📡 MQTT Integration

### Topic Structure

Following factory infrastructure pattern:

| Topic | Purpose | QoS |
|-------|---------|-----|
| `factory/pump/fills` | Fill completion events | 1 |
| `factory/pump/events` | System events | 1 |
| `factory/pump/status` | Real-time status | 0 |

### Message Formats

#### Fill Completion (`factory/pump/fills`)

```json
{
  "device_id": "bdo_pump_01",
  "target_lbs": 200.0,
  "actual_lbs": 200.2,
  "error_lbs": 0.2,
  "fill_time_ms": 178450,
  "fill_number": 1523,
  "pressure_avg_pct": 45.3,
  "zone_transitions": 4,
  "completion_status": "success",
  "timestamp": 1699294832
}
```

#### System Event (`factory/pump/events`)

```json
{
  "device_id": "bdo_pump_01",
  "event": "safety_check_complete",
  "details": "All 4 safety checks passed",
  "timestamp": 1699294832
}
```

#### Status Update (`factory/pump/status`)

```json
{
  "device_id": "bdo_pump_01",
  "state": "FILLING",
  "current_weight_lbs": 125.4,
  "target_weight_lbs": 200.0,
  "progress_pct": 62.7,
  "current_pressure_pct": 70.0,
  "active_zone": "MODERATE",
  "fill_elapsed_ms": 102340,
  "fills_today": 12,
  "total_lbs_today": 2400.5,
  "uptime_seconds": 86420,
  "timestamp": 1699294832
}
```

---

## 🗄️ Database Setup

### TimescaleDB Schema

Initialize database with:

```bash
docker exec -i dosing-timescaledb psql -U telegraf -d factory_metrics < database/init-bdo-pump.sql
```

### Tables Created

1. **pump_fills** - Individual fill operations
2. **pump_events** - System events
3. **pump_status** - Real-time status snapshots

### Continuous Aggregates

- **pump_fills_hourly** - Hourly statistics
- **pump_fills_daily** - Daily summaries

### Data Retention

- **Raw data**: 90 days
- **Aggregates**: 2 years
- **Compression**: After 7 days (~90% space savings)

### Grafana Queries

**Total Fills Today:**
```sql
SELECT COUNT(*) FROM pump_fills
WHERE time >= CURRENT_DATE AND completion_status = 'success';
```

**Total Pounds Dispensed Today:**
```sql
SELECT SUM(actual_lbs) FROM pump_fills
WHERE time >= CURRENT_DATE AND completion_status = 'success';
```

**Fill Accuracy Trend:**
```sql
SELECT time_bucket('1 hour', time) AS time, AVG(error_lbs) as avg_error
FROM pump_fills
WHERE time >= NOW() - INTERVAL '24 hours' AND completion_status = 'success'
GROUP BY time_bucket('1 hour', time) ORDER BY time;
```

---

## 🔧 Troubleshooting

### ESP32 Won't Boot

- Check 5V power supply
- Verify buck converter output
- Press EN button to reset
- Check USB cable connection

### WiFi Not Connecting

- Verify SSID and password in `config.h`
- Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
- Check router firewall settings
- Monitor serial output for errors

### Scale Not Reading

- Check RS232 wiring (TX/RX may be swapped)
- Verify MAX3232 power (3.3V or 5V)
- Confirm baud rate (9600 default)
- Test scale with serial monitor

### ITV2030 Not Responding

- Measure DAC output (should be 0-10V)
- Check op-amp power supply (12V)
- Verify resistor values (10kΩ and 20kΩ)
- Confirm ITV2030 has 24V power

### No Data in Grafana

1. Check MQTT messages: `mosquitto_sub -h localhost -t factory/#`
2. Verify Telegraf logs: `docker logs dosing-telegraf`
3. Query database: `psql -U telegraf -d factory_metrics -c "SELECT COUNT(*) FROM pump_fills;"`
4. Check Grafana data source connection

### WebUI Not Accessible

- Find ESP32 IP address in serial monitor
- Verify same WiFi network as client device
- Check firewall settings
- Try http://esp32_ip/ in browser

---

## 📚 Documentation

- **[Hardware Schematic](docs/HARDWARE_SCHEMATIC_24V.md)** - Complete wiring diagrams and BOM
- **[MQTT Topic Design](MQTT_TOPIC_DESIGN.md)** - Topic structure and payload formats
- **[TimescaleDB Integration](MQTT_TIMESCALEDB_INTEGRATION_GUIDE.md)** - Database setup guide
- **[Project Summary](PROJECT_COMPLETE_SUMMARY.md)** - Original design specifications

---

## 🛡️ Safety

⚠️ **IMPORTANT SAFETY INFORMATION**

- Always follow the 4-stage safety checklist before fills
- Verify all pneumatic connections are secure
- Ensure proper pressure relief valves are installed
- Do not exceed ITV2030 rated pressure (72.5 PSI)
- Keep hands and loose clothing away from moving parts
- Use properly rated electrical components
- Follow all local electrical and pneumatic safety codes

---

## 📊 Performance Metrics

### Typical Fill Performance (200 lbs)

| Metric | Value |
|--------|-------|
| Total Time | 175-180 seconds |
| Fast Zone Time | 60-70 seconds |
| Moderate Zone Time | 40-50 seconds |
| Slow Zone Time | 40-50 seconds |
| Fine Zone Time | 30-40 seconds |
| Typical Accuracy | ±0.3-0.7 lbs |

### System Resources

| Resource | Usage |
|----------|-------|
| Flash | ~600 KB |
| RAM | ~150 KB |
| CPU (avg) | 15-25% |
| WiFi Power | ~150 mA |

---

## 🔮 Future Enhancements

- [ ] PID control for smoother fills
- [ ] Recipe management (multiple target weights)
- [ ] Data logging to SD card
- [ ] OTA firmware updates
- [ ] Email/SMS alerts via Grafana
- [ ] Barcode scanning for product tracking
- [ ] Multi-language support
- [ ] Voice feedback

---

## 📝 License

This project is licensed under the MIT License - see LICENSE file for details.

---

## 🙏 Acknowledgments

- Based on original pneumatic pump controller design
- ESP-IDF framework by Espressif Systems
- TimescaleDB for time-series optimization
- SMC Corporation for ITV2030 documentation
- Community contributions and testing

---

## 📞 Support

For issues, questions, or contributions:

- **Issues**: [GitHub Issues](https://github.com/yourusername/BDO_Pump/issues)
- **Documentation**: See `docs/` folder
- **Hardware Questions**: See `HARDWARE_SCHEMATIC_24V.md`

---

## 🎉 Ready to Build!

**Start with:**
1. Review [`docs/HARDWARE_SCHEMATIC_24V.md`](docs/HARDWARE_SCHEMATIC_24V.md)
2. Order components from BOM
3. Assemble hardware following wiring diagrams
4. Flash firmware with PlatformIO
5. Configure WiFi/MQTT in `config.h`
6. Deploy backend with Docker Compose
7. Access WebUI and start filling!

**Happy Filling! 🏭**
