-- ---------------------------------------------------------------------------
-- CEREVIA — reference schema
--
-- The backend creates and migrates this automatically on start-up
-- (see backend/Database.cpp), so you never need to run this file by hand.
-- It is kept as documentation, and for anyone who wants to inspect or rebuild
-- the database with the sqlite3 CLI.
--
--   sqlite3 database/mental_health.db < database/schema.sql
-- ---------------------------------------------------------------------------

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- The single local user. CEREVIA is deliberately not multi-user: it is one
-- person's private journal on one machine.
CREATE TABLE IF NOT EXISTS users (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    -- Stored as "<saltHex>$<digestHex>", never in the clear.
    pin               TEXT    NOT NULL,
    security_question TEXT    NOT NULL,
    -- Also salted and hashed; compared case- and whitespace-insensitively.
    security_answer   TEXT    NOT NULL,
    emergency_contact TEXT    NOT NULL DEFAULT '112',
    display_name      TEXT    NOT NULL DEFAULT 'friend',
    created_at        TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- One row per check-in. Nothing here is ever deleted automatically.
CREATE TABLE IF NOT EXISTS moods (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    mood       TEXT    NOT NULL,           -- Happy | Calm | Neutral | Anxious | Angry | Sad
    level      INTEGER NOT NULL CHECK (level BETWEEN 1 AND 10),
    note       TEXT    NOT NULL DEFAULT '',
    tags       TEXT    NOT NULL DEFAULT '', -- comma separated
    date       TEXT    NOT NULL,            -- human-readable label
    created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- Journal bodies are encrypted before insertion, in the form
-- "v2:<nonceHex>:<cipherHex>". Reading this table directly shows ciphertext.
CREATE TABLE IF NOT EXISTS journals (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    text       TEXT    NOT NULL,
    mood       TEXT    NOT NULL DEFAULT '',
    prompt     TEXT    NOT NULL DEFAULT '',
    word_count INTEGER NOT NULL DEFAULT 0,
    date       TEXT    NOT NULL,
    created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS breathing_sessions (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    technique  TEXT    NOT NULL DEFAULT 'box',   -- box | 478 | coherent
    cycles     INTEGER NOT NULL DEFAULT 0,
    seconds    INTEGER NOT NULL DEFAULT 0,
    created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
);

-- Free-form preferences that do not deserve their own column.
CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- Every dashboard query filters or orders by time.
CREATE INDEX IF NOT EXISTS idx_moods_created     ON moods(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_journals_created  ON journals(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_breathing_created ON breathing_sessions(created_at DESC);

-- Seed the single user if the table is empty. The default PIN is '1234' and is
-- hashed in place the first time the backend starts.
INSERT INTO users (id, pin, security_question, security_answer, emergency_contact, display_name)
SELECT 1, '1234', 'What is your favourite colour?', 'blue', '112', 'friend'
WHERE NOT EXISTS (SELECT 1 FROM users WHERE id = 1);
