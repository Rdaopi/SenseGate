-- SenseGate — Supabase schema (PostgreSQL, no TimescaleDB extensions)
-- When migrating to a VPS with TimescaleDB, use schema.sql instead.
-- The Lambda handler code does not need to change.

-- ── Main readings table ───────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS sensor_readings (
    id                  BIGSERIAL       PRIMARY KEY,
    time                TIMESTAMPTZ     NOT NULL DEFAULT NOW(),
    device_id           SMALLINT        NOT NULL,
    sequence            INTEGER         NOT NULL,

    -- Machine state
    state               SMALLINT        NOT NULL,
    state_name          VARCHAR(32)     NOT NULL,

    -- Pallet production data
    pallet_id           INTEGER         NOT NULL,
    wrap_time_s         INTEGER         NOT NULL,   -- wrapping time in seconds
    wrap_transit_time_s INTEGER         NOT NULL,   -- wrapping + transit time in seconds
    pallet_rotations    INTEGER         NOT NULL,   -- rotations on pallet
    program_number      SMALLINT        NOT NULL,
    pallet_perimeter    INTEGER         NOT NULL,   -- unit TBD with Technowrapp

    -- Daily accumulated times
    running_seconds     INTEGER         NOT NULL,
    alarm_seconds       INTEGER         NOT NULL,

    -- Timestamp from machine clock
    machine_timestamp   BIGINT          NOT NULL,

    -- Metadata
    from_number         VARCHAR(32)
);

-- Index for per-device time-range queries
CREATE INDEX IF NOT EXISTS idx_readings_device_time
    ON sensor_readings (device_id, time DESC);

-- Index for state-based queries (e.g. filter by alarm/offline)
CREATE INDEX IF NOT EXISTS idx_readings_state
    ON sensor_readings (state, time DESC);

-- Index for pallet traceability queries
CREATE INDEX IF NOT EXISTS idx_readings_pallet
    ON sensor_readings (pallet_id, time DESC);

-- ── Sequence deduplication log ────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS sequence_log (
    device_id       SMALLINT        NOT NULL,
    sequence        INTEGER         NOT NULL,
    received_at     TIMESTAMPTZ     NOT NULL DEFAULT NOW(),
    PRIMARY KEY (device_id, sequence)
);

-- ── Hourly summary view (replaces TimescaleDB continuous aggregate) ───────────
CREATE OR REPLACE VIEW readings_hourly AS
SELECT
    date_trunc('hour', time)    AS bucket,
    device_id,
    MAX(state)                  AS last_state,
    COUNT(*)                    AS packets_received,
    MAX(running_seconds)        AS max_running_s,
    MAX(alarm_seconds)          AS max_alarm_s,
    COUNT(DISTINCT pallet_id)   AS pallets_processed
FROM sensor_readings
GROUP BY date_trunc('hour', time), device_id;