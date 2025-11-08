-- ============================================================================
-- TimescaleDB Initialization Script
-- Chemical Dosing System - Factory Metrics Database
-- ============================================================================

-- Enable TimescaleDB extension
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- ============================================================================
-- TABLE 1: DOSING CONSUMPTION
-- ============================================================================

CREATE TABLE IF NOT EXISTS dosing_consumption (
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

-- Convert to hypertable (time-series optimized)
SELECT create_hypertable('dosing_consumption', 'time', if_not_exists => TRUE);

-- Create indexes for common queries
CREATE INDEX IF NOT EXISTS idx_dosing_device_time
    ON dosing_consumption (device_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_dosing_chemical_time
    ON dosing_consumption (chemical, time DESC);

CREATE INDEX IF NOT EXISTS idx_dosing_recipe_time
    ON dosing_consumption (recipe, time DESC);

CREATE INDEX IF NOT EXISTS idx_dosing_pump
    ON dosing_consumption (pump, time DESC);

-- ============================================================================
-- TABLE 2: BATCH EVENTS
-- ============================================================================

CREATE TABLE IF NOT EXISTS batch_events (
    time TIMESTAMPTZ NOT NULL,
    device_id TEXT NOT NULL,
    event TEXT NOT NULL,
    recipe TEXT,
    pumps INTEGER
);

-- Convert to hypertable
SELECT create_hypertable('batch_events', 'time', if_not_exists => TRUE);

-- Create indexes
CREATE INDEX IF NOT EXISTS idx_batch_device_time
    ON batch_events (device_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_batch_event_time
    ON batch_events (event, time DESC);

CREATE INDEX IF NOT EXISTS idx_batch_recipe_time
    ON batch_events (recipe, time DESC);

-- ============================================================================
-- TABLE 3: INVENTORY LEVELS
-- ============================================================================

CREATE TABLE IF NOT EXISTS inventory_levels (
    time TIMESTAMPTZ NOT NULL,
    device_id TEXT NOT NULL,
    chemical TEXT NOT NULL,
    remaining_g DOUBLE PRECISION,
    capacity_g DOUBLE PRECISION,
    percent_full DOUBLE PRECISION
);

-- Convert to hypertable
SELECT create_hypertable('inventory_levels', 'time', if_not_exists => TRUE);

-- Create indexes
CREATE INDEX IF NOT EXISTS idx_inventory_device_time
    ON inventory_levels (device_id, time DESC);

CREATE INDEX IF NOT EXISTS idx_inventory_chemical_time
    ON inventory_levels (chemical, time DESC);

-- ============================================================================
-- VIEWS
-- ============================================================================

-- Latest inventory levels for each chemical
CREATE OR REPLACE VIEW latest_inventory AS
SELECT DISTINCT ON (device_id, chemical)
    time,
    device_id,
    chemical,
    remaining_g,
    capacity_g,
    percent_full
FROM inventory_levels
ORDER BY device_id, chemical, time DESC;

-- Daily production summary
CREATE OR REPLACE VIEW daily_production AS
SELECT
    DATE(time) as date,
    device_id,
    recipe,
    mode,
    COUNT(*) as dose_count,
    SUM(actual_g) as total_grams,
    AVG(actual_g) as avg_grams,
    AVG(error_g) as avg_error,
    MIN(error_g) as min_error,
    MAX(error_g) as max_error,
    AVG(duration_ms) / 1000.0 as avg_duration_seconds
FROM dosing_consumption
GROUP BY DATE(time), device_id, recipe, mode
ORDER BY date DESC;

-- ============================================================================
-- CONTINUOUS AGGREGATES (Pre-computed materialized views)
-- ============================================================================

-- Hourly dosing statistics
CREATE MATERIALIZED VIEW IF NOT EXISTS dosing_consumption_hourly
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
    STDDEV(actual_g) as stddev_grams,
    AVG(error_g) as avg_error,
    STDDEV(error_g) as stddev_error,
    AVG(duration_ms) as avg_duration_ms,
    MIN(actual_g) as min_actual_g,
    MAX(actual_g) as max_actual_g
FROM dosing_consumption
GROUP BY bucket, device_id, chemical, recipe, mode
WITH NO DATA;

-- Refresh policy: update every hour, looking back 2 hours
SELECT add_continuous_aggregate_policy('dosing_consumption_hourly',
    start_offset => INTERVAL '2 hours',
    end_offset => INTERVAL '1 hour',
    schedule_interval => INTERVAL '1 hour',
    if_not_exists => TRUE);

-- Daily dosing statistics
CREATE MATERIALIZED VIEW IF NOT EXISTS dosing_consumption_daily
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 day', time) AS bucket,
    device_id,
    chemical,
    recipe,
    mode,
    COUNT(*) as dose_count,
    SUM(actual_g) as total_grams,
    AVG(actual_g) as avg_grams,
    AVG(error_g) as avg_error,
    AVG(duration_ms) / 1000.0 as avg_duration_seconds
FROM dosing_consumption
GROUP BY bucket, device_id, chemical, recipe, mode
WITH NO DATA;

-- Refresh policy: update daily at midnight
SELECT add_continuous_aggregate_policy('dosing_consumption_daily',
    start_offset => INTERVAL '3 days',
    end_offset => INTERVAL '1 day',
    schedule_interval => INTERVAL '1 day',
    if_not_exists => TRUE);

-- ============================================================================
-- DATA RETENTION POLICIES
-- ============================================================================

-- Keep raw data for 90 days (3 months)
SELECT add_retention_policy('dosing_consumption', INTERVAL '90 days', if_not_exists => TRUE);
SELECT add_retention_policy('batch_events', INTERVAL '90 days', if_not_exists => TRUE);
SELECT add_retention_policy('inventory_levels', INTERVAL '90 days', if_not_exists => TRUE);

-- Keep aggregated data for 2 years
SELECT add_retention_policy('dosing_consumption_hourly', INTERVAL '2 years', if_not_exists => TRUE);
SELECT add_retention_policy('dosing_consumption_daily', INTERVAL '2 years', if_not_exists => TRUE);

-- ============================================================================
-- COMPRESSION POLICIES (Optional - saves ~90% disk space)
-- ============================================================================

-- Enable compression
ALTER TABLE dosing_consumption SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id, chemical'
);

ALTER TABLE batch_events SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id'
);

ALTER TABLE inventory_levels SET (
    timescaledb.compress,
    timescaledb.compress_segmentby = 'device_id, chemical'
);

-- Compress data older than 7 days
SELECT add_compression_policy('dosing_consumption', INTERVAL '7 days', if_not_exists => TRUE);
SELECT add_compression_policy('batch_events', INTERVAL '7 days', if_not_exists => TRUE);
SELECT add_compression_policy('inventory_levels', INTERVAL '7 days', if_not_exists => TRUE);

-- ============================================================================
-- PERMISSIONS
-- ============================================================================

-- Grant permissions to telegraf user
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO telegraf;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO telegraf;

-- ============================================================================
-- VERIFICATION QUERIES
-- ============================================================================

-- List all hypertables
SELECT * FROM timescaledb_information.hypertables;

-- Check compression stats (after some data is compressed)
-- SELECT * FROM timescaledb_information.compressed_hypertable_stats;

-- Show retention policies
SELECT * FROM timescaledb_information.jobs WHERE proc_name = 'policy_retention';

-- Show compression policies
SELECT * FROM timescaledb_information.jobs WHERE proc_name = 'policy_compression';

-- Show continuous aggregate policies
SELECT * FROM timescaledb_information.jobs WHERE proc_name = 'policy_refresh_continuous_aggregate';

-- ============================================================================
-- SAMPLE QUERIES (for testing after data ingestion)
-- ============================================================================

-- Count records in each table
-- SELECT 'dosing_consumption' as table_name, COUNT(*) as record_count FROM dosing_consumption
-- UNION ALL
-- SELECT 'batch_events', COUNT(*) FROM batch_events
-- UNION ALL
-- SELECT 'inventory_levels', COUNT(*) FROM inventory_levels;

-- Latest inventory
-- SELECT * FROM latest_inventory;

-- Recent dosing events
-- SELECT * FROM dosing_consumption ORDER BY time DESC LIMIT 10;

-- Hourly aggregates
-- SELECT * FROM dosing_consumption_hourly ORDER BY bucket DESC LIMIT 24;

-- ============================================================================
-- DONE!
-- ============================================================================

\echo '========================================='
\echo 'TimescaleDB initialization complete!'
\echo '========================================='
\echo ''
\echo 'Created tables:'
\echo '  - dosing_consumption (hypertable)'
\echo '  - batch_events (hypertable)'
\echo '  - inventory_levels (hypertable)'
\echo ''
\echo 'Created views:'
\echo '  - latest_inventory'
\echo '  - daily_production'
\echo ''
\echo 'Created continuous aggregates:'
\echo '  - dosing_consumption_hourly'
\echo '  - dosing_consumption_daily'
\echo ''
\echo 'Configured:'
\echo '  - Retention policies (90 days raw, 2 years aggregated)'
\echo '  - Compression policies (compress after 7 days)'
\echo '  - Indexes for fast queries'
\echo ''
\echo 'Ready to receive data from Telegraf!'
\echo '========================================='
