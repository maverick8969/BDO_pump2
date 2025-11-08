# Material Consumption Tracking Integration Guide
## ESP32 → MQTT → Telegraf → TimescaleDB → Grafana

**System:** Chemical Dosing System with Multi-Sensor Monitoring
**Date:** October 30, 2025
**Version:** 2.0 - TimescaleDB Edition

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Architecture](#architecture)
3. [Why TimescaleDB?](#why-timescaledb)
4. [TimescaleDB Setup](#timescaledb-setup)
5. [Database Schema](#database-schema)
6. [Telegraf Configuration](#telegraf-configuration)
7. [Grafana Dashboards](#grafana-dashboards)
8. [Docker Compose Setup](#docker-compose-setup)
9. [Best Practices](#best-practices)
10. [Troubleshooting](#troubleshooting)

---

## System Overview

### Components

- **Nodes:** ESP32 devices publish sensor data to MQTT
- **Broker:** Eclipse Mosquitto (MQTT broker)
- **Collector:** Telegraf subscribes to MQTT and writes to TimescaleDB
- **Database:** TimescaleDB (PostgreSQL extension for time-series data)
- **Visualization:** Grafana (dashboards, alerts, annotations)
- **Time Sync:** NTP on all devices

### Data Types

- **Dosing/Consumption Data:** Material usage, pump cycles, batch tracking
- **Inventory Data:** Material levels, low-stock alerts
- **Batch Events:** Production start/complete events

---

## Architecture

```
┌─────────────┐
│   ESP32     │ ──► Peristaltic Pumps (4x)
│   Device    │ ──► Scale (weight measurement)
│             │ ──► LED status indicators
└──────┬──────┘
       │ WiFi
       ▼
┌─────────────────────────────────────────────────┐
│          MQTT Broker (Mosquitto)                │
│  Topics:                                        │
│    - factory/dosing/consumption                 │
│    - factory/batch/events                       │
│    - factory/inventory/levels                   │
└──────┬──────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────┐
│          Telegraf (MQTT Consumer)               │
│  - Subscribes to topics                         │
│  - Parses JSON payloads                         │
│  - Writes to PostgreSQL/TimescaleDB             │
└──────┬──────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────┐
│     TimescaleDB (PostgreSQL + Time Series)      │
│  Database: factory_metrics                      │
│  Hypertables:                                   │
│    - dosing_consumption                         │
│    - batch_events                               │
│    - inventory_levels                           │
└──────┬──────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────┐
│          Grafana (Visualization)                │
│  - Multi-panel dashboards                       │
│  - SQL queries for analysis                     │
│  - Alerts and notifications                     │
└─────────────────────────────────────────────────┘
```

---

## Why TimescaleDB?

### Advantages over InfluxDB:

1. **SQL Interface** - Familiar query language, easier to learn
2. **PostgreSQL Ecosystem** - Access to all PostgreSQL features and tools
3. **Strong ACID Guarantees** - Better data consistency
4. **JOINs Support** - Easily join time-series with relational data
5. **Cost Effective** - Open source with no limitations
6. **Better for Mixed Workloads** - Time-series + relational data in one DB
7. **Continuous Aggregates** - Automatic materialized views for performance
8. **Compression** - Automatic data compression for long-term storage

### When to Use TimescaleDB:

✅ You know SQL
✅ Need to join time-series with relational data
✅ Want full PostgreSQL compatibility
✅ Need complex queries and analytics
✅ Want on-premise deployment with no vendor lock-in

---

## TimescaleDB Setup

### Option 1: Docker (Recommended)

See [Docker Compose Setup](#docker-compose-setup) section below.

### Option 2: Manual Installation

#### Install PostgreSQL (if not already installed)

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install postgresql postgresql-contrib

# Start PostgreSQL
sudo systemctl start postgresql
sudo systemctl enable postgresql
```

#### Install TimescaleDB Extension

```bash
# Add TimescaleDB repository
sudo sh -c "echo 'deb https://packagecloud.io/timescale/timescaledb/ubuntu/ $(lsb_release -c -s) main' > /etc/apt/sources.list.d/timescaledb.list"
wget --quiet -O - https://packagecloud.io/timescale/timescaledb/gpgkey | sudo apt-key add -

# Install TimescaleDB
sudo apt update
sudo apt install timescaledb-2-postgresql-14

# Configure PostgreSQL for TimescaleDB
sudo timescaledb-tune

# Restart PostgreSQL
sudo systemctl restart postgresql
```

#### Create Database and Enable Extension

```bash
# Switch to postgres user
sudo -u postgres psql

# In psql:
CREATE DATABASE factory_metrics;
\c factory_metrics
CREATE EXTENSION IF NOT EXISTS timescaledb;

# Create user for Telegraf
CREATE USER telegraf WITH PASSWORD 'your_secure_password';
GRANT ALL PRIVILEGES ON DATABASE factory_metrics TO telegraf;

# Exit
\q
```

---

## Database Schema

### Create Tables and Hypertables

Connect to the database:

```bash
psql -U telegraf -d factory_metrics
```

#### 1. Dosing Consumption Table

```sql
-- Create table for dosing consumption data
CREATE TABLE dosing_consumption (
    time TIMESTAMPTZ NOT NULL,
    device_id TEXT NOT NULL,
    pump INTEGER NOT NULL,
    chemical TEXT NOT NULL,
    target_g DOUBLE PRECISION,
    actual_g DOUBLE PRECISION,
    error_g DOUBLE PRECISION,
    recipe TEXT,
    mode TEXT,
    duration_ms BIGINT
);

-- Convert to hypertable (enables time-series optimizations)
SELECT create_hypertable('dosing_consumption', 'time');

-- Create indexes for common queries
CREATE INDEX idx_dosing_device_time ON dosing_consumption (device_id, time DESC);
CREATE INDEX idx_dosing_chemical_time ON dosing_consumption (chemical, time DESC);
CREATE INDEX idx_dosing_recipe_time ON dosing_consumption (recipe, time DESC);
CREATE INDEX idx_dosing_pump ON dosing_consumption (pump, time DESC);

-- Create continuous aggregate for hourly statistics
CREATE MATERIALIZED VIEW dosing_consumption_hourly
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 hour', time) AS bucket,
    device_id,
    chemical,
    recipe,
    mode,
    COUNT(*) as dose_count,
    SUM(actual_g) as total_grams,
    AVG(actual_g) as avg_grams,
    AVG(error_g) as avg_error,
    AVG(duration_ms) as avg_duration_ms
FROM dosing_consumption
GROUP BY bucket, device_id, chemical, recipe, mode
WITH NO DATA;

-- Refresh policy (update every hour, looking back 2 hours)
SELECT add_continuous_aggregate_policy('dosing_consumption_hourly',
    start_offset => INTERVAL '2 hours',
    end_offset => INTERVAL '1 hour',
    schedule_interval => INTERVAL '1 hour');
```

#### 2. Batch Events Table

```sql
-- Create table for batch events
CREATE TABLE batch_events (
    time TIMESTAMPTZ NOT NULL,
    device_id TEXT NOT NULL,
    event TEXT NOT NULL,
    recipe TEXT,
    pumps INTEGER
);

-- Convert to hypertable
SELECT create_hypertable('batch_events', 'time');

-- Create indexes
CREATE INDEX idx_batch_device_time ON batch_events (device_id, time DESC);
CREATE INDEX idx_batch_event_time ON batch_events (event, time DESC);
CREATE INDEX idx_batch_recipe_time ON batch_events (recipe, time DESC);
```

#### 3. Inventory Levels Table

```sql
-- Create table for inventory tracking
CREATE TABLE inventory_levels (
    time TIMESTAMPTZ NOT NULL,
    device_id TEXT NOT NULL,
    chemical TEXT NOT NULL,
    remaining_g DOUBLE PRECISION,
    capacity_g DOUBLE PRECISION,
    percent_full DOUBLE PRECISION
);

-- Convert to hypertable
SELECT create_hypertable('inventory_levels', 'time');

-- Create indexes
CREATE INDEX idx_inventory_device_time ON inventory_levels (device_id, time DESC);
CREATE INDEX idx_inventory_chemical_time ON inventory_levels (chemical, time DESC);

-- Create view for latest inventory levels
CREATE VIEW latest_inventory AS
SELECT DISTINCT ON (device_id, chemical)
    time,
    device_id,
    chemical,
    remaining_g,
    capacity_g,
    percent_full
FROM inventory_levels
ORDER BY device_id, chemical, time DESC;
```

#### 4. Data Retention Policies

```sql
-- Keep raw data for 90 days
SELECT add_retention_policy('dosing_consumption', INTERVAL '90 days');
SELECT add_retention_policy('batch_events', INTERVAL '90 days');
SELECT add_retention_policy('inventory_levels', INTERVAL '90 days');

-- Continuous aggregates are kept longer (2 years)
SELECT add_retention_policy('dosing_consumption_hourly', INTERVAL '2 years');
```

#### 5. Enable Compression (Optional)

```sql
-- Enable compression for older data (saves ~90% disk space)
ALTER TABLE dosing_consumption SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id, chemical'
);

-- Compress data older than 7 days
SELECT add_compression_policy('dosing_consumption', INTERVAL '7 days');

-- Enable compression for other tables
ALTER TABLE batch_events SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id'
);
SELECT add_compression_policy('batch_events', INTERVAL '7 days');

ALTER TABLE inventory_levels SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id, chemical'
);
SELECT add_compression_policy('inventory_levels', INTERVAL '7 days');
```

---

## Telegraf Configuration

### /etc/telegraf/telegraf.d/dosing_system.conf

```toml
# =============================================================================
# DOSING SYSTEM MQTT → TIMESCALEDB
# =============================================================================

# Global tags (applied to all measurements)
[global_tags]
  facility = "main_factory"
  system = "chemical_dosing"

# -----------------------------------------------------------------------------
# MQTT Input - Dosing Consumption
# -----------------------------------------------------------------------------
[[inputs.mqtt_consumer]]
  servers = ["tcp://mosquitto:1883"]
  topics = ["factory/dosing/consumption"]

  # Authentication (if enabled in Mosquitto)
  # username = "telegraf"
  # password = "your_password"

  data_format = "json"
  json_time_key = "timestamp"
  json_time_format = "unix"

  # Name override
  name_override = "dosing_consumption"

# -----------------------------------------------------------------------------
# MQTT Input - Batch Events
# -----------------------------------------------------------------------------
[[inputs.mqtt_consumer]]
  servers = ["tcp://mosquitto:1883"]
  topics = ["factory/batch/events"]

  data_format = "json"
  json_time_key = "timestamp"
  json_time_format = "unix"

  name_override = "batch_events"

# -----------------------------------------------------------------------------
# MQTT Input - Inventory Levels
# -----------------------------------------------------------------------------
[[inputs.mqtt_consumer]]
  servers = ["tcp://mosquitto:1883"]
  topics = ["factory/inventory/levels"]

  data_format = "json"

  name_override = "inventory_levels"

# =============================================================================
# OUTPUT TO TIMESCALEDB (PostgreSQL)
# =============================================================================

[[outputs.postgresql]]
  # Connection string
  connection = "host=timescaledb port=5432 user=telegraf password=your_secure_password dbname=factory_metrics sslmode=disable"

  # Create tables automatically (useful for development)
  create_templates = [
    '''CREATE TABLE IF NOT EXISTS {{ .table }} ({{ .columns }})''',
  ]

  # Table mapping
  tables = [
    # Map input measurements to database tables
    {measurement = "dosing_consumption", table = "dosing_consumption", tags = ["device_id", "chemical", "recipe", "mode"], fields = ["pump", "target_g", "actual_g", "error_g", "duration_ms"]},
    {measurement = "batch_events", table = "batch_events", tags = ["device_id", "event", "recipe"], fields = ["pumps"]},
    {measurement = "inventory_levels", table = "inventory_levels", tags = ["device_id", "chemical"], fields = ["remaining_g", "capacity_g", "percent_full"]},
  ]

  # Timestamp column
  timestamp_column = "time"

  # Timeout
  timeout = "5s"
```

### Testing Telegraf Configuration

```bash
# Test configuration syntax
telegraf --config /etc/telegraf/telegraf.d/dosing_system.conf --test

# Run telegraf with debug output
telegraf --config /etc/telegraf/telegraf.d/dosing_system.conf --debug

# Check Telegraf is running
sudo systemctl status telegraf

# Restart Telegraf
sudo systemctl restart telegraf
```

---

## Grafana Dashboards

### Add TimescaleDB Data Source

1. Navigate to **Configuration → Data Sources → Add data source**
2. Select **PostgreSQL**
3. Configure:
   - **Name:** TimescaleDB Factory Metrics
   - **Host:** `timescaledb:5432` (Docker) or `localhost:5432`
   - **Database:** `factory_metrics`
   - **User:** `telegraf`
   - **Password:** Your secure password
   - **SSL Mode:** `disable` (or configure SSL)
   - **TimescaleDB:** ✅ Enable (important!)
   - **Version:** 14+ (or your PostgreSQL version)
4. Click **Save & Test**

### Dashboard Panel Examples

#### Panel 1: Total Consumption by Chemical (Today)

```sql
SELECT
  chemical,
  SUM(actual_g) as total_grams
FROM dosing_consumption
WHERE time >= CURRENT_DATE
  AND time < CURRENT_DATE + INTERVAL '1 day'
GROUP BY chemical
ORDER BY total_grams DESC;
```

**Visualization:** Bar Chart or Stat Panel

#### Panel 2: Consumption Over Time (Time Series)

```sql
SELECT
  time_bucket('1 hour', time) AS time,
  chemical,
  SUM(actual_g) as total_grams
FROM dosing_consumption
WHERE time >= $__timeFrom() AND time <= $__timeTo()
GROUP BY time_bucket('1 hour', time), chemical
ORDER BY time;
```

**Visualization:** Time Series (Line Chart)

**Note:** `$__timeFrom()` and `$__timeTo()` are Grafana variables

#### Panel 3: Dosing Accuracy Distribution (Histogram)

```sql
SELECT
  width_bucket(error_g, -2.0, 2.0, 20) as bucket,
  COUNT(*) as count,
  (width_bucket(error_g, -2.0, 2.0, 20) - 10) * 0.2 as error_range
FROM dosing_consumption
WHERE time >= $__timeFrom() AND time <= $__timeTo()
GROUP BY bucket
ORDER BY bucket;
```

**Visualization:** Bar Chart (Histogram)

#### Panel 4: Current Inventory Levels

```sql
SELECT
  chemical,
  percent_full
FROM latest_inventory
ORDER BY percent_full ASC;
```

**Visualization:** Gauge Panel (with thresholds: <20% red, 20-50% yellow, >50% green)

#### Panel 5: Batch Count by Recipe (Today)

```sql
SELECT
  recipe,
  COUNT(*) as batch_count
FROM batch_events
WHERE event = 'complete'
  AND time >= CURRENT_DATE
  AND time < CURRENT_DATE + INTERVAL '1 day'
GROUP BY recipe
ORDER BY batch_count DESC;
```

**Visualization:** Stat or Bar Chart

#### Panel 6: Average Dosing Duration by Chemical

```sql
SELECT
  time_bucket('1 hour', time) AS time,
  chemical,
  AVG(duration_ms) / 1000.0 as avg_duration_seconds
FROM dosing_consumption
WHERE time >= $__timeFrom() AND time <= $__timeTo()
GROUP BY time_bucket('1 hour', time), chemical
ORDER BY time;
```

**Visualization:** Time Series

#### Panel 7: Dosing Error Rate (Outside Tolerance)

```sql
SELECT
  time_bucket('1 hour', time) AS time,
  COUNT(*) FILTER (WHERE ABS(error_g) > 0.5) * 100.0 / COUNT(*) as error_rate_percent
FROM dosing_consumption
WHERE time >= $__timeFrom() AND time <= $__timeTo()
GROUP BY time_bucket('1 hour', time)
ORDER BY time;
```

**Visualization:** Time Series with threshold line at 5%

#### Panel 8: Daily Production Summary Table

```sql
SELECT
  DATE(time) as date,
  recipe,
  mode,
  COUNT(*) as batches,
  SUM(actual_g) as total_grams,
  AVG(error_g) as avg_error,
  AVG(duration_ms) / 1000.0 as avg_duration_sec
FROM dosing_consumption
WHERE time >= CURRENT_DATE - INTERVAL '7 days'
GROUP BY DATE(time), recipe, mode
ORDER BY date DESC, recipe;
```

**Visualization:** Table

### Alert Rules

#### Low Inventory Alert

```sql
SELECT
  chemical,
  percent_full
FROM latest_inventory
WHERE percent_full < 20.0;
```

**Alert Condition:** When query returns data
**Notification:** "⚠️ Low inventory: {chemical} at {percent_full}%"

#### High Error Rate Alert

```sql
SELECT
  AVG(ABS(error_g)) as avg_error
FROM dosing_consumption
WHERE time >= NOW() - INTERVAL '1 hour'
HAVING AVG(ABS(error_g)) > 1.0;
```

**Alert Condition:** When query returns data
**Notification:** "⚠️ High dosing error rate: {avg_error}g average"

#### System Offline Alert

```sql
SELECT
  MAX(time) as last_seen,
  NOW() - MAX(time) as time_since_last_message
FROM dosing_consumption
HAVING NOW() - MAX(time) > INTERVAL '10 minutes';
```

**Alert Condition:** When query returns data
**Notification:** "🔴 Dosing system offline for {time_since_last_message}"

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

  # TimescaleDB (PostgreSQL + Time Series Extension)
  timescaledb:
    image: timescale/timescaledb:latest-pg14
    container_name: timescaledb
    ports:
      - "5432:5432"
    environment:
      - POSTGRES_DB=factory_metrics
      - POSTGRES_USER=telegraf
      - POSTGRES_PASSWORD=your_secure_password
      - TIMESCALEDB_TELEMETRY=off
    volumes:
      - timescaledb-data:/var/lib/postgresql/data
      - ./init-timescaledb.sql:/docker-entrypoint-initdb.d/init.sql
    restart: unless-stopped
    command: postgres -c shared_preload_libraries=timescaledb

  # Telegraf Data Collector
  telegraf:
    image: telegraf:latest
    container_name: telegraf
    volumes:
      - ./telegraf/telegraf.conf:/etc/telegraf/telegraf.conf:ro
    depends_on:
      - timescaledb
      - mosquitto
    restart: unless-stopped

  # Grafana Visualization
  grafana:
    image: grafana/grafana:latest
    container_name: grafana
    ports:
      - "3000:3000"
    environment:
      - GF_SECURITY_ADMIN_USER=admin
      - GF_SECURITY_ADMIN_PASSWORD=admin
      - GF_INSTALL_PLUGINS=
    volumes:
      - grafana-data:/var/lib/grafana
      - ./grafana/provisioning:/etc/grafana/provisioning
    depends_on:
      - timescaledb
    restart: unless-stopped

volumes:
  timescaledb-data:
  grafana-data:

networks:
  default:
    name: iot-network
```

### init-timescaledb.sql

Create `init-timescaledb.sql` with the schema from the [Database Schema](#database-schema) section.

### Start the Stack

```bash
# Create directory structure
mkdir -p mosquitto/config mosquitto/data mosquitto/log
mkdir -p telegraf
mkdir -p grafana/provisioning

# Copy mosquitto.conf to mosquitto/config/
# Copy telegraf.conf to telegraf/
# Copy init-timescaledb.sql to project root

# Start services
docker-compose up -d

# Check logs
docker-compose logs -f

# Check TimescaleDB is ready
docker exec -it timescaledb psql -U telegraf -d factory_metrics -c "SELECT * FROM timescaledb_information.hypertables;"

# Stop services
docker-compose down
```

### Access Services

- **Mosquitto MQTT:** `mqtt://localhost:1883`
- **TimescaleDB:** `postgresql://localhost:5432/factory_metrics`
- **Grafana:** `http://localhost:3000` (admin/admin)

---

## Best Practices

### Database Design

✅ **DO:**
- Use hypertables for all time-series data
- Create indexes on commonly filtered columns
- Use continuous aggregates for expensive queries
- Enable compression for data older than 7 days
- Set retention policies based on business needs
- Use meaningful column names and types

❌ **DON'T:**
- Store non-time-series data in hypertables
- Create too many indexes (impacts write performance)
- Query raw data for long time ranges (use continuous aggregates)
- Forget to vacuum and analyze regularly
- Use TEXT when VARCHAR(n) is sufficient

### Query Optimization

1. **Use time_bucket() for aggregations**
   ```sql
   SELECT time_bucket('1 hour', time), AVG(actual_g)
   FROM dosing_consumption
   GROUP BY time_bucket('1 hour', time);
   ```

2. **Filter by time first** (takes advantage of hypertable partitioning)
   ```sql
   WHERE time >= NOW() - INTERVAL '7 days'
   ```

3. **Use continuous aggregates for dashboards**
   - Pre-computed results
   - Much faster than querying raw data
   - Automatically updated

4. **Limit result sets**
   ```sql
   LIMIT 1000
   ```

### Maintenance

```sql
-- Check database size
SELECT pg_size_pretty(pg_database_size('factory_metrics'));

-- Check table sizes
SELECT
    hypertable_name,
    pg_size_pretty(total_bytes) AS total_size,
    pg_size_pretty(index_bytes) AS index_size
FROM timescaledb_information.hypertable_detailed_size('dosing_consumption');

-- Check compression stats
SELECT
    hypertable_name,
    compression_status,
    uncompressed_total_bytes,
    compressed_total_bytes,
    pg_size_pretty(uncompressed_total_bytes) AS uncompressed_size,
    pg_size_pretty(compressed_total_bytes) AS compressed_size,
    ROUND((1 - compressed_total_bytes::numeric / uncompressed_total_bytes::numeric) * 100, 2) AS compression_ratio
FROM timescaledb_information.compressed_hypertable_stats;

-- Vacuum and analyze (run weekly)
VACUUM ANALYZE dosing_consumption;
VACUUM ANALYZE batch_events;
VACUUM ANALYZE inventory_levels;

-- Refresh continuous aggregates manually (if needed)
CALL refresh_continuous_aggregate('dosing_consumption_hourly', NULL, NULL);
```

---

## Troubleshooting

### No Data in TimescaleDB

1. **Check Telegraf is running and connected:**
   ```bash
   docker logs telegraf
   # Look for connection errors
   ```

2. **Check MQTT messages are being received:**
   ```bash
   mosquitto_sub -h localhost -t factory/#
   ```

3. **Test database connection:**
   ```bash
   psql -U telegraf -d factory_metrics -c "SELECT COUNT(*) FROM dosing_consumption;"
   ```

4. **Check Telegraf config syntax:**
   ```bash
   telegraf --config telegraf.conf --test
   ```

### Grafana Shows No Data

1. **Test data source connection** in Grafana
2. **Run query directly in psql** to verify data exists
3. **Check time range** in Grafana panel
4. **Verify column names** match schema

### Slow Queries

1. **Check query execution plan:**
   ```sql
   EXPLAIN ANALYZE
   SELECT * FROM dosing_consumption WHERE time >= NOW() - INTERVAL '1 day';
   ```

2. **Add missing indexes**
3. **Use continuous aggregates** for pre-computed results
4. **Enable compression** for older data
5. **Partition large result sets**

### High Disk Usage

1. **Check table sizes** (see Maintenance section)
2. **Enable compression** for older data
3. **Adjust retention policies** to drop old data
4. **Vacuum** to reclaim space

---

## Comparison: TimescaleDB vs InfluxDB

| Feature | TimescaleDB | InfluxDB |
|---------|-------------|----------|
| Query Language | SQL (PostgreSQL) | InfluxQL / Flux |
| Learning Curve | Low (if you know SQL) | Medium-High |
| JOINs | ✅ Full support | ⚠️ Limited |
| ACID Compliance | ✅ Yes | ❌ No |
| Compression | ~90% | ~85% |
| Ecosystem | PostgreSQL tools | InfluxDB-specific |
| Scaling | Vertical + horizontal | Horizontal (clustered) |
| Cost | Open source | Open source + paid |
| Best For | SQL users, complex queries | Time-series specialists |

---

## Quick Start Summary

1. **Deploy backend:** `docker-compose up -d`
2. **ESP32 code:** Already configured (publishes to MQTT)
3. **Configure Telegraf:** Edit `telegraf.conf` with database credentials
4. **Create schema:** Run `init-timescaledb.sql`
5. **Add Grafana data source:** PostgreSQL/TimescaleDB
6. **Create dashboards:** Use SQL queries above
7. **Monitor:** Watch data flow in real-time

---

## Additional Resources

### Documentation
- **TimescaleDB Docs:** https://docs.timescale.com/
- **PostgreSQL Docs:** https://www.postgresql.org/docs/
- **Telegraf PostgreSQL Output:** https://github.com/influxdata/telegraf/tree/master/plugins/outputs/postgresql
- **Grafana PostgreSQL:** https://grafana.com/docs/grafana/latest/datasources/postgres/

### Tutorials
- **TimescaleDB Quick Start:** https://docs.timescale.com/quick-start/latest/
- **Continuous Aggregates:** https://docs.timescale.com/use-timescale/latest/continuous-aggregates/
- **Compression:** https://docs.timescale.com/use-timescale/latest/compression/

### Tools
- **pgAdmin** - PostgreSQL GUI
- **DBeaver** - Universal database tool
- **psql** - PostgreSQL command-line client

---

**Document Version:** 2.0
**Last Updated:** October 30, 2025
**Status:** Production Ready (TimescaleDB Edition)

---

**You now have a production-grade time-series database with the power of SQL!** 🎯📊
