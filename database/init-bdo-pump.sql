-- ============================================================================
-- TimescaleDB Initialization Script - BDO Pneumatic Pump
-- Factory Metrics Database - Pump Fills Tracking
-- ============================================================================

-- Enable TimescaleDB extension (if not already enabled)
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- ============================================================================
-- TABLE 1: PUMP FILLS
-- Tracks individual fill operations
-- ============================================================================

CREATE TABLE IF NOT EXISTS pump_fills (
    time TIMESTAMPTZ NOT NULL,
    device_id TEXT NOT NULL,
    target_lbs DOUBLE PRECISION,
    actual_lbs DOUBLE PRECISION,
    error_lbs DOUBLE PRECISION,
    fill_time_ms BIGINT,
    fill_number INTEGER,
    pressure_avg_pct DOUBLE PRECISION,
    zone_transitions INTEGER,
    completion_status TEXT
);

-- Convert to hypertable (time-series optimized)
SELECT create_hypertable('pump_fills', 'time', if_not_exists => TRUE);

-- Create indexes for common queries
CREATE INDEX IF NOT EXISTS idx_pump_fills_device_time
    ON pump_fills (device_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_pump_fills_status
    ON pump_fills (completion_status, time DESC);

CREATE INDEX IF NOT EXISTS idx_pump_fills_device_status
    ON pump_fills (device_id, completion_status, time DESC);

-- ============================================================================
-- TABLE 2: PUMP EVENTS
-- System events (startup, safety checks, errors, etc.)
-- ============================================================================

CREATE TABLE IF NOT EXISTS pump_events (
    time TIMESTAMPTZ NOT NULL,
    device_id TEXT NOT NULL,
    event TEXT NOT NULL,
    details TEXT
);

-- Convert to hypertable
SELECT create_hypertable('pump_events', 'time', if_not_exists => TRUE);

-- Create indexes
CREATE INDEX IF NOT EXISTS idx_pump_events_device_time
    ON pump_events (device_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_pump_events_type
    ON pump_events (event, time DESC);

-- ============================================================================
-- TABLE 3: PUMP STATUS
-- Real-time status updates
-- ============================================================================

CREATE TABLE IF NOT EXISTS pump_status (
    time TIMESTAMPTZ NOT NULL,
    device_id TEXT NOT NULL,
    state TEXT NOT NULL,
    current_weight_lbs DOUBLE PRECISION,
    target_weight_lbs DOUBLE PRECISION,
    progress_pct DOUBLE PRECISION,
    current_pressure_pct DOUBLE PRECISION,
    active_zone TEXT,
    fill_elapsed_ms BIGINT,
    fills_today INTEGER,
    total_lbs_today DOUBLE PRECISION,
    uptime_seconds BIGINT
);

-- Convert to hypertable
SELECT create_hypertable('pump_status', 'time', if_not_exists => TRUE);

-- Create indexes
CREATE INDEX IF NOT EXISTS idx_pump_status_device_time
    ON pump_status (device_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_pump_status_state
    ON pump_status (state, time DESC);

-- ============================================================================
-- VIEWS
-- ============================================================================

-- Latest status for each pump device
CREATE OR REPLACE VIEW latest_pump_status AS
SELECT DISTINCT ON (device_id)
    time,
    device_id,
    state,
    current_weight_lbs,
    target_weight_lbs,
    progress_pct,
    current_pressure_pct,
    active_zone,
    fills_today,
    total_lbs_today,
    uptime_seconds
FROM pump_status
ORDER BY device_id, time DESC;

-- Daily pump summary
CREATE OR REPLACE VIEW daily_pump_summary AS
SELECT
    DATE(time) as date,
    device_id,
    COUNT(*) FILTER (WHERE completion_status = 'success') as successful_fills,
    COUNT(*) FILTER (WHERE completion_status = 'error') as error_fills,
    COUNT(*) FILTER (WHERE completion_status = 'cancelled') as cancelled_fills,
    SUM(actual_lbs) FILTER (WHERE completion_status = 'success') as total_lbs_dispensed,
    AVG(actual_lbs) FILTER (WHERE completion_status = 'success') as avg_fill_lbs,
    AVG(error_lbs) FILTER (WHERE completion_status = 'success') as avg_error_lbs,
    STDDEV(error_lbs) FILTER (WHERE completion_status = 'success') as stddev_error_lbs,
    AVG(fill_time_ms) FILTER (WHERE completion_status = 'success') / 1000.0 as avg_fill_time_sec,
    MIN(error_lbs) as min_error,
    MAX(error_lbs) as max_error
FROM pump_fills
GROUP BY DATE(time), device_id
ORDER BY date DESC;

-- ============================================================================
-- CONTINUOUS AGGREGATES (Pre-computed materialized views)
-- ============================================================================

-- Hourly fill statistics
CREATE MATERIALIZED VIEW IF NOT EXISTS pump_fills_hourly
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 hour', time) AS bucket,
    device_id,
    completion_status,
    COUNT(*) as fill_count,
    SUM(actual_lbs) as total_lbs,
    AVG(actual_lbs) as avg_lbs,
    STDDEV(actual_lbs) as stddev_lbs,
    AVG(error_lbs) as avg_error_lbs,
    STDDEV(error_lbs) as stddev_error_lbs,
    AVG(fill_time_ms) / 1000.0 as avg_fill_time_sec,
    AVG(pressure_avg_pct) as avg_pressure_pct,
    MIN(actual_lbs) as min_lbs,
    MAX(actual_lbs) as max_lbs
FROM pump_fills
GROUP BY bucket, device_id, completion_status
WITH NO DATA;

-- Refresh policy: update every hour, looking back 2 hours
SELECT add_continuous_aggregate_policy('pump_fills_hourly',
    start_offset => INTERVAL '2 hours',
    end_offset => INTERVAL '1 hour',
    schedule_interval => INTERVAL '1 hour',
    if_not_exists => TRUE);

-- Daily fill statistics
CREATE MATERIALIZED VIEW IF NOT EXISTS pump_fills_daily
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 day', time) AS bucket,
    device_id,
    COUNT(*) as total_fills,
    COUNT(*) FILTER (WHERE completion_status = 'success') as successful_fills,
    COUNT(*) FILTER (WHERE completion_status = 'error') as error_fills,
    COUNT(*) FILTER (WHERE completion_status = 'cancelled') as cancelled_fills,
    SUM(actual_lbs) FILTER (WHERE completion_status = 'success') as total_lbs_dispensed,
    AVG(actual_lbs) FILTER (WHERE completion_status = 'success') as avg_fill_lbs,
    AVG(error_lbs) FILTER (WHERE completion_status = 'success') as avg_error_lbs,
    AVG(fill_time_ms) FILTER (WHERE completion_status = 'success') / 1000.0 as avg_fill_time_sec
FROM pump_fills
GROUP BY bucket, device_id
WITH NO DATA;

-- Refresh policy: update daily at midnight
SELECT add_continuous_aggregate_policy('pump_fills_daily',
    start_offset => INTERVAL '3 days',
    end_offset => INTERVAL '1 day',
    schedule_interval => INTERVAL '1 day',
    if_not_exists => TRUE);

-- ============================================================================
-- DATA RETENTION POLICIES
-- ============================================================================

-- Keep raw data for 90 days (3 months)
SELECT add_retention_policy('pump_fills', INTERVAL '90 days', if_not_exists => TRUE);
SELECT add_retention_policy('pump_events', INTERVAL '90 days', if_not_exists => TRUE);
SELECT add_retention_policy('pump_status', INTERVAL '30 days', if_not_exists => TRUE);

-- Keep aggregated data for 2 years
SELECT add_retention_policy('pump_fills_hourly', INTERVAL '2 years', if_not_exists => TRUE);
SELECT add_retention_policy('pump_fills_daily', INTERVAL '2 years', if_not_exists => TRUE);

-- ============================================================================
-- COMPRESSION POLICIES (Optional - saves ~90% disk space)
-- ============================================================================

-- Enable compression
ALTER TABLE pump_fills SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id, completion_status'
);

ALTER TABLE pump_events SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id, event'
);

ALTER TABLE pump_status SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id, state'
);

-- Compress data older than 7 days
SELECT add_compression_policy('pump_fills', INTERVAL '7 days', if_not_exists => TRUE);
SELECT add_compression_policy('pump_events', INTERVAL '7 days', if_not_exists => TRUE);
SELECT add_compression_policy('pump_status', INTERVAL '3 days', if_not_exists => TRUE);

-- ============================================================================
-- PERMISSIONS
-- ============================================================================

-- Grant permissions to telegraf user
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO telegraf;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO telegraf;

-- ============================================================================
-- USEFUL QUERIES (for Grafana dashboards)
-- ============================================================================

-- Total fills today
-- SELECT COUNT(*) FROM pump_fills WHERE time >= CURRENT_DATE AND completion_status = 'success';

-- Total pounds dispensed today
-- SELECT SUM(actual_lbs) FROM pump_fills WHERE time >= CURRENT_DATE AND completion_status = 'success';

-- Fill accuracy trend (last 24 hours)
-- SELECT time_bucket('1 hour', time) AS time, AVG(error_lbs) as avg_error
-- FROM pump_fills
-- WHERE time >= NOW() - INTERVAL '24 hours' AND completion_status = 'success'
-- GROUP BY time_bucket('1 hour', time) ORDER BY time;

-- Current system status
-- SELECT * FROM latest_pump_status;

-- Fills per hour (last 7 days)
-- SELECT bucket, fill_count FROM pump_fills_hourly WHERE bucket >= NOW() - INTERVAL '7 days';

-- ============================================================================
-- DONE!
-- ============================================================================

\echo '========================================='
\echo 'BDO Pump TimescaleDB initialization complete!'
\echo '========================================='
\echo ''
\echo 'Created tables:'
\echo '  - pump_fills (hypertable)'
\echo '  - pump_events (hypertable)'
\echo '  - pump_status (hypertable)'
\echo ''
\echo 'Created views:'
\echo '  - latest_pump_status'
\echo '  - daily_pump_summary'
\echo ''
\echo 'Created continuous aggregates:'
\echo '  - pump_fills_hourly'
\echo '  - pump_fills_daily'
\echo ''
\echo 'Configured:'
\echo '  - Retention policies (90 days raw, 2 years aggregated)'
\echo '  - Compression policies (compress after 7 days)'
\echo '  - Indexes for fast queries'
\echo ''
\echo 'Ready to receive data from BDO Pump!'
\echo '========================================='
