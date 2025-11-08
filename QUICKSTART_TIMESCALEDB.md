# Quick Start: TimescaleDB Setup

## 5-Minute Setup Guide

This guide will get your MQTT → TimescaleDB → Grafana stack running in 5 minutes.

---

## Prerequisites

- Docker and Docker Compose installed
- ESP32 with MQTT code uploaded (WiFi and MQTT broker configured)
- Port 1883, 5432, and 3000 available

---

## Step 1: Prepare Configuration Files

### 1.1 Create directory structure

```bash
cd /path/to/peristaltic_pump

# Create Mosquitto directories
mkdir -p mosquitto/config mosquitto/data mosquitto/log

# Copy Mosquitto config
cp mosquitto.conf mosquitto/config/
```

### 1.2 Update Database Password

Edit `telegraf-timescaledb.conf` line 87:

```toml
connection = "host=timescaledb port=5432 user=telegraf password=YOUR_SECURE_PASSWORD dbname=factory_metrics sslmode=disable"
```

Edit `docker-compose-timescaledb.yml` line 33:

```yaml
- POSTGRES_PASSWORD=YOUR_SECURE_PASSWORD
```

**Important:** Use the **same password** in both files!

---

## Step 2: Start the Stack

```bash
# Start all services
docker-compose -f docker-compose-timescaledb.yml up -d

# Check all containers are running
docker-compose -f docker-compose-timescaledb.yml ps

# Expected output:
# NAME                    STATUS
# dosing-mosquitto        Up
# dosing-timescaledb      Up (healthy)
# dosing-telegraf         Up
# dosing-grafana          Up
```

---

## Step 3: Verify Database Setup

```bash
# Connect to TimescaleDB
docker exec -it dosing-timescaledb psql -U telegraf -d factory_metrics

# Inside psql, run:
\dt                    # List tables
\d dosing_consumption  # Show table structure
SELECT COUNT(*) FROM dosing_consumption;  # Check if data is flowing
\q                     # Exit
```

Expected tables:
- `dosing_consumption`
- `batch_events`
- `inventory_levels`

---

## Step 4: Test MQTT Data Flow

### 4.1 Subscribe to MQTT topics

In a terminal:
```bash
# Install mosquitto clients (if not already)
sudo apt install mosquitto-clients

# Subscribe to all topics
mosquitto_sub -h localhost -t "factory/#" -v
```

### 4.2 Trigger a dosing event on your ESP32

You should see JSON messages like:
```
factory/dosing/consumption {"device_id":"dosing_system_01","pump":3,"chemical":"T-9",...}
```

### 4.3 Check Telegraf is processing

```bash
# View Telegraf logs
docker logs dosing-telegraf --tail 50

# Look for lines like:
# "dosing_consumption" written successfully
```

### 4.4 Verify data in database

```bash
# Query recent data
docker exec -it dosing-timescaledb psql -U telegraf -d factory_metrics -c \
  "SELECT time, device_id, chemical, actual_g FROM dosing_consumption ORDER BY time DESC LIMIT 5;"
```

---

## Step 5: Configure Grafana

### 5.1 Access Grafana

Open browser: http://localhost:3000

- **Username:** `admin`
- **Password:** `admin`
- (Change password when prompted)

### 5.2 Add TimescaleDB Data Source

1. Click **⚙️ Configuration** → **Data sources**
2. Click **Add data source**
3. Select **PostgreSQL**
4. Configure:
   - **Name:** `Factory Metrics`
   - **Host:** `timescaledb:5432`
   - **Database:** `factory_metrics`
   - **User:** `telegraf`
   - **Password:** `YOUR_SECURE_PASSWORD`
   - **SSL Mode:** `disable`
   - **TimescaleDB:** ✅ **Enable** (important!)
5. Click **Save & Test** (should show green checkmark)

### 5.3 Create Your First Dashboard

1. Click **➕ Create** → **Dashboard**
2. Click **Add visualization**
3. Select **Factory Metrics** data source
4. Switch to **Code** mode (top right)
5. Paste this query:

```sql
SELECT
  time_bucket('5 minutes', time) AS time,
  chemical,
  SUM(actual_g) as total_grams
FROM dosing_consumption
WHERE time >= NOW() - INTERVAL '1 hour'
GROUP BY time_bucket('5 minutes', time), chemical
ORDER BY time;
```

6. Set visualization to **Time series**
7. Click **Apply**
8. Save dashboard (💾 icon)

---

## Step 6: Test End-to-End

### 6.1 Run a dosing cycle on ESP32

1. Power on ESP32
2. Select a recipe (e.g., CU-85)
3. Run a dosing cycle

### 6.2 Watch data flow

**Terminal 1 - MQTT messages:**
```bash
mosquitto_sub -h localhost -t "factory/#" -v
```

**Terminal 2 - Telegraf logs:**
```bash
docker logs -f dosing-telegraf
```

**Terminal 3 - Database queries:**
```bash
watch -n 5 'docker exec -it dosing-timescaledb psql -U telegraf -d factory_metrics -c "SELECT COUNT(*) FROM dosing_consumption;"'
```

**Browser - Grafana:**
- Refresh your dashboard
- You should see new data points appearing!

---

## Troubleshooting

### No MQTT messages

```bash
# Check ESP32 is connected to WiFi (Serial Monitor)
# Check MQTT broker IP in config.h matches your setup
# Test broker: mosquitto_pub -h localhost -t test -m "hello"
```

### Telegraf not writing to database

```bash
# Check Telegraf logs
docker logs dosing-telegraf | grep -i error

# Common issues:
# - Wrong database password
# - Database not ready (wait 30 seconds after starting)
# - Table doesn't exist (check init-timescaledb.sql ran)
```

### Grafana shows "No data"

1. Check data source connection (green checkmark)
2. Verify time range (top right in dashboard)
3. Run query directly in psql to verify data exists
4. Check query syntax (SQL not Flux/InfluxQL!)

### Container won't start

```bash
# Check logs
docker-compose -f docker-compose-timescaledb.yml logs

# Check ports aren't already in use
sudo netstat -tulpn | grep -E '1883|5432|3000'

# Restart everything
docker-compose -f docker-compose-timescaledb.yml down
docker-compose -f docker-compose-timescaledb.yml up -d
```

---

## Useful Commands

```bash
# View all logs
docker-compose -f docker-compose-timescaledb.yml logs -f

# Restart a specific service
docker-compose -f docker-compose-timescaledb.yml restart telegraf

# Stop everything
docker-compose -f docker-compose-timescaledb.yml down

# Stop and remove volumes (WARNING: deletes all data!)
docker-compose -f docker-compose-timescaledb.yml down -v

# Access database shell
docker exec -it dosing-timescaledb psql -U telegraf -d factory_metrics

# Check database size
docker exec -it dosing-timescaledb psql -U telegraf -d factory_metrics -c \
  "SELECT pg_size_pretty(pg_database_size('factory_metrics'));"

# Export dashboard (from Grafana UI)
# Settings → JSON Model → Copy to clipboard
```

---

## Next Steps

1. **Create more dashboards** - See `MQTT_TIMESCALEDB_INTEGRATION_GUIDE.md` for SQL query examples
2. **Set up alerts** - Configure Grafana alerts for low inventory, high error rates, etc.
3. **Enable authentication** - See `mosquitto.conf` for password setup
4. **Backup strategy** - Schedule regular PostgreSQL backups
5. **Monitor performance** - Check TimescaleDB compression and retention policies

---

## Production Checklist

Before deploying in production:

- [ ] Change all default passwords
- [ ] Enable MQTT authentication (`mosquitto.conf`)
- [ ] Configure SSL/TLS for MQTT
- [ ] Set up PostgreSQL backups
- [ ] Configure firewall rules
- [ ] Set up monitoring/alerting
- [ ] Document recovery procedures
- [ ] Test backup restoration
- [ ] Configure log rotation
- [ ] Review retention policies

---

**You're all set! Happy monitoring!** 📊🎯
