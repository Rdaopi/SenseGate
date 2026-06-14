-- SenseGate — Seed data from real QEMU firmware output (14 packets, device_id=1)
-- Timestamps are offset from base 2026-05-18 00:00:00 UTC

INSERT INTO sensor_readings (
    time, device_id, sequence,
    state, state_name,
    pallet_id, wrap_time_s, wrap_transit_time_s,
    pallet_rotations, program_number, pallet_perimeter,
    running_seconds, alarm_seconds,
    machine_timestamp, from_number
) VALUES
    ('2026-05-18 00:00:00+00', 1,  0, 30, 'Running',            159302,     668,     944, 14, 0, 3, 4000,   0, 1774138000, NULL),
    ('2026-05-18 00:01:00+00', 1,  1, 30, 'Running',            159291,    2717,    3200, 14, 0, 3, 4000,   0, 1774138060, NULL),
    ('2026-05-18 00:02:00+00', 1,  2, 30, 'Running',            159280,    4766,    5456, 14, 0, 3, 4000,   0, 1774138120, NULL),
    ('2026-05-18 00:03:00+00', 1,  3, 30, 'Running',            159269,    6815,    7712, 14, 0, 3, 4000,   0, 1774138180, NULL),
    ('2026-05-18 00:04:00+00', 1,  4, 30, 'Running',            159258,    8864,    9968, 14, 0, 3, 4000,   0, 1774138240, NULL),
    ('2026-05-18 00:05:00+00', 1,  5, 30, 'Running',            159247,   10913,   12224, 14, 0, 3, 4000,   0, 1774138300, NULL),
    ('2026-05-18 00:06:00+00', 1,  6, 30, 'Running',            159236,   12962,   14480, 14, 0, 3, 4000,   0, 1774138360, NULL),
    ('2026-05-18 00:07:00+00', 1,  7, 30, 'Running',            159225,   15011,   16736, 15, 0, 3, 4000,   0, 1774138420, NULL),
    ('2026-05-18 00:08:00+00', 1,  8, 30, 'Running',            159214,   17060,   18992, 15, 0, 3, 4000,   0, 1774138480, NULL),
    ('2026-05-18 00:09:00+00', 1,  9, 31, 'Waiting Materials',  159202,   19107,   21252, 16, 0, 3, 4000, 283, 1774138540, NULL),
    ('2026-05-18 00:10:00+00', 1, 10, 31, 'Waiting Materials',  159192,   19207,   21352, 16, 0, 3, 4000, 293, 1774138600, NULL),
    ('2026-05-18 00:11:00+00', 1, 11, 31, 'Waiting Materials',  159182,   19307,   21452, 16, 0, 3, 4000, 303, 1774138660, NULL),
    ('2026-05-18 00:12:00+00', 1, 12, 99, 'Offline',            158308,       0,       0,  0, 0, 3, 4000,   0, 1774138720, NULL),
    ('2026-05-18 00:13:00+00', 1, 13, 99, 'Offline',            158303,       0,       0,  0, 0, 3, 4000,   0, 1774138780, NULL)
ON CONFLICT DO NOTHING;

-- sequence_log is intentionally left empty so live packets are never blocked by seed data
