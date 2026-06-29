from __future__ import annotations

import csv
import sqlite3
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Person:
    uid_hex: str
    sid: int
    name: str
    department: str
    card_type: int
    points: int = 0


class Database:
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()
        self.conn = sqlite3.connect(self.path, check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        self.init_schema()

    def close(self) -> None:
        with self._lock:
            self.conn.close()

    def init_schema(self) -> None:
        with self._lock:
            self.conn.executescript(
                """
                CREATE TABLE IF NOT EXISTS people (
                    uid_hex TEXT PRIMARY KEY,
                    sid INTEGER NOT NULL,
                    name TEXT NOT NULL,
                    department TEXT NOT NULL,
                    card_type INTEGER NOT NULL,
                    points INTEGER NOT NULL DEFAULT 0,
                    lost INTEGER NOT NULL DEFAULT 0,
                    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
                );

                CREATE TABLE IF NOT EXISTS issue_logs (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    uid_hex TEXT NOT NULL,
                    sid INTEGER NOT NULL,
                    name TEXT NOT NULL,
                    department TEXT NOT NULL,
                    card_type INTEGER NOT NULL,
                    action TEXT NOT NULL,
                    status TEXT NOT NULL,
                    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
                );

                CREATE TABLE IF NOT EXISTS attendance_records (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    seq INTEGER,
                    uid_hex TEXT,
                    sid INTEGER,
                    record_type TEXT,
                    occurred_at TEXT,
                    device_id INTEGER,
                    status TEXT,
                    raw_line TEXT NOT NULL,
                    imported_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
                );

                DELETE FROM attendance_records
                WHERE id NOT IN (
                    SELECT MIN(id)
                    FROM attendance_records
                    WHERE device_id IS NOT NULL AND seq IS NOT NULL
                    GROUP BY device_id, seq
                    UNION
                    SELECT id
                    FROM attendance_records
                    WHERE device_id IS NULL OR seq IS NULL
                );

                CREATE UNIQUE INDEX IF NOT EXISTS idx_attendance_device_seq
                ON attendance_records(device_id, seq)
                WHERE device_id IS NOT NULL AND seq IS NOT NULL;
                """
            )
            self.conn.commit()

    def upsert_person(self, person: Person) -> None:
        with self._lock:
            self.conn.execute(
                """
                INSERT INTO people (uid_hex, sid, name, department, card_type, points)
                VALUES (?, ?, ?, ?, ?, ?)
                ON CONFLICT(uid_hex) DO UPDATE SET
                    sid=excluded.sid,
                    name=excluded.name,
                    department=excluded.department,
                    card_type=excluded.card_type,
                    points=excluded.points,
                    updated_at=CURRENT_TIMESTAMP
                """,
                (person.uid_hex, person.sid, person.name, person.department, person.card_type, person.points),
            )
            self.conn.commit()

    def mark_lost(self, uid_hex: str, lost: bool = True) -> None:
        with self._lock:
            self.conn.execute(
                "UPDATE people SET lost=?, updated_at=CURRENT_TIMESTAMP WHERE uid_hex=?",
                (1 if lost else 0, uid_hex),
            )
            self.conn.commit()

    def list_people(self) -> list[sqlite3.Row]:
        with self._lock:
            return list(
                self.conn.execute(
                    "SELECT uid_hex, sid, name, department, card_type, points, lost, updated_at FROM people ORDER BY updated_at DESC"
                )
            )

    def add_issue_log(self, person: Person, action: str, status: str) -> None:
        with self._lock:
            self.conn.execute(
                """
                INSERT INTO issue_logs (uid_hex, sid, name, department, card_type, action, status)
                VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (person.uid_hex, person.sid, person.name, person.department, person.card_type, action, status),
            )
            self.conn.commit()

    def import_record_line(self, raw_line: str) -> None:
        parsed = parse_record_line(raw_line)
        with self._lock:
            self.conn.execute(
                """
                INSERT OR IGNORE INTO attendance_records
                    (seq, uid_hex, sid, record_type, occurred_at, device_id, status, raw_line)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    parsed.get("seq"),
                    parsed.get("uid_hex"),
                    parsed.get("sid"),
                    parsed.get("record_type"),
                    parsed.get("occurred_at"),
                    parsed.get("device_id"),
                    parsed.get("status"),
                    raw_line,
                ),
            )
            self.conn.commit()

    def list_attendance(self, limit: int = 200) -> list[sqlite3.Row]:
        with self._lock:
            return list(
                self.conn.execute(
                    """
                    SELECT seq, uid_hex, sid, record_type, occurred_at, device_id, status, imported_at
                    FROM attendance_records
                    ORDER BY id DESC
                    LIMIT ?
                    """,
                    (limit,),
                )
            )

    def export_attendance_csv(self, target: str | Path) -> None:
        rows = self.list_attendance(limit=100000)
        with Path(target).open("w", newline="", encoding="utf-8-sig") as handle:
            writer = csv.writer(handle)
            writer.writerow(["seq", "uid_hex", "sid", "record_type", "occurred_at", "device_id", "status", "imported_at"])
            for row in rows:
                writer.writerow([row[key] for key in row.keys()])


def parse_record_line(raw_line: str) -> dict[str, object]:
    line = raw_line.strip()
    if not line.upper().startswith("REC:"):
        return {"status": "RAW"}
    body = line[4:]
    parts = body.split("|")
    parsed: dict[str, object] = {}
    for index, part in enumerate(parts):
        if "=" in part:
            key, value = part.split("=", 1)
            key = key.strip().upper()
            value = value.strip()
            if key == "SEQ":
                parsed["seq"] = _to_int(value)
            elif key == "UID":
                parsed["uid_hex"] = value.upper()
            elif key == "SID":
                parsed["sid"] = _to_int(value)
            elif key == "DEV":
                parsed["device_id"] = _to_int(value)
            else:
                parsed[key.lower()] = value
        elif index == 3:
            parsed["record_type"] = part.strip()
        elif index == 4:
            parsed["occurred_at"] = part.strip()
        elif index == 6:
            parsed["status"] = part.strip()
    return parsed


def _to_int(value: str) -> int | None:
    try:
        return int(value)
    except ValueError:
        return None
