from __future__ import annotations

import sqlite3
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator


SCHEMA = """
PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;

CREATE TABLE IF NOT EXISTS projects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    area TEXT NOT NULL DEFAULT 'Personal',
    color TEXT NOT NULL DEFAULT '#38B6FF',
    archived INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS tasks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER REFERENCES projects(id) ON DELETE SET NULL,
    title TEXT NOT NULL,
    due_at TEXT,
    priority INTEGER NOT NULL DEFAULT 0 CHECK(priority BETWEEN 0 AND 3),
    completed INTEGER NOT NULL DEFAULT 0,
    completed_at TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    external_id TEXT UNIQUE,
    provider TEXT NOT NULL DEFAULT 'manual',
    calendar_name TEXT NOT NULL DEFAULT 'Aura',
    title TEXT NOT NULL,
    starts_at TEXT NOT NULL,
    ends_at TEXT,
    location TEXT,
    all_day INTEGER NOT NULL DEFAULT 0,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_tasks_digest
    ON tasks(completed, due_at, priority);
CREATE INDEX IF NOT EXISTS idx_events_window
    ON events(starts_at);

CREATE TABLE IF NOT EXISTS audio_captures (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL DEFAULT 'aura-watch',
    filename TEXT NOT NULL,
    content_type TEXT NOT NULL DEFAULT 'audio/wav',
    storage_path TEXT NOT NULL UNIQUE,
    byte_count INTEGER NOT NULL,
    sha256 TEXT NOT NULL UNIQUE,
    duration_seconds REAL,
    captured_at TEXT,
    uploaded_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    status TEXT NOT NULL DEFAULT 'uploaded',
    error TEXT
);

CREATE TABLE IF NOT EXISTS transcripts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    capture_id INTEGER NOT NULL UNIQUE REFERENCES audio_captures(id) ON DELETE CASCADE,
    text TEXT NOT NULL,
    language TEXT,
    provider TEXT NOT NULL DEFAULT 'manual',
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS proposals (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    transcript_id INTEGER REFERENCES transcripts(id) ON DELETE SET NULL,
    kind TEXT NOT NULL CHECK(kind IN ('task', 'event', 'reminder', 'note')),
    title TEXT NOT NULL,
    details TEXT,
    proposed_at TEXT,
    due_at TEXT,
    starts_at TEXT,
    ends_at TEXT,
    priority INTEGER NOT NULL DEFAULT 0 CHECK(priority BETWEEN 0 AND 3),
    status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending', 'approved', 'rejected')),
    resolved_at TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS memories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    proposal_id INTEGER UNIQUE REFERENCES proposals(id) ON DELETE SET NULL,
    title TEXT NOT NULL,
    body TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS notifications (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL DEFAULT 'aura-watch',
    app_id TEXT,
    title TEXT,
    body TEXT,
    external_id TEXT,
    dismissed INTEGER NOT NULL DEFAULT 0,
    received_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS provider_sync (
    provider TEXT PRIMARY KEY,
    status TEXT NOT NULL,
    detail TEXT,
    synced_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_captures_status ON audio_captures(status, uploaded_at);
CREATE INDEX IF NOT EXISTS idx_proposals_status ON proposals(status, created_at);
CREATE INDEX IF NOT EXISTS idx_memories_created ON memories(created_at);
CREATE INDEX IF NOT EXISTS idx_notifications_unread ON notifications(dismissed, received_at);
"""


class Database:
    def __init__(self, path: Path):
        self.path = path

    def initialize(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as connection:
            connection.executescript(SCHEMA)

    @contextmanager
    def connect(self) -> Iterator[sqlite3.Connection]:
        connection = sqlite3.connect(self.path, timeout=10)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys=ON")
        try:
            yield connection
            connection.commit()
        except Exception:
            connection.rollback()
            raise
        finally:
            connection.close()
