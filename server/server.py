from __future__ import annotations

import argparse
import html
import json
import socketserver
import sqlite3
import threading
import zlib
from contextlib import contextmanager
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlparse


DATA_DIR = Path(__file__).resolve().parent / "data"
LOG_FILE = DATA_DIR / "uploads.log"
DB_FILE = DATA_DIR / "attendance.db"
HEARTBEAT_ONLINE_WINDOW_SEC = 90
OTA_CHUNK_SIZE = 192
OTA_MAX_CHUNK_SIZE = 192
OTA_FILE_NAME = "current.bin"
OTA_SLOT_FILE_TEMPLATE = "current_{slot}.bin"


def _now() -> str:
    return datetime.now().isoformat(timespec="seconds")


def _crc16_ccitt_false(text: str) -> int:
    crc = 0xFFFF
    for byte in text.encode("utf-8"):
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _wrap_crc(response: str) -> str:
    return f"CRC:{response}*{_crc16_ccitt_false(response):04X}"


def _unwrap_crc(line: str) -> tuple[str, bool] | None:
    if not line.startswith("CRC:"):
        return line, False
    if "*" not in line:
        return None
    payload, crc_text = line[4:].rsplit("*", 1)
    try:
        expected = int(crc_text, 16)
    except ValueError:
        return None
    if _crc16_ccitt_false(payload) != expected:
        return None
    return payload, True


def _parse_fields(prefix: str, line: str) -> dict[str, str]:
    if not line.startswith(prefix):
        return {}

    fields: dict[str, str] = {}
    payload = line[len(prefix) :]
    for part in payload.split("|"):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        fields[key.strip().upper()] = value.strip()
    return fields


def _parse_int(value: str | None) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(value, 10)
    except ValueError:
        return None


def _parse_float(value: str | None) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def _ensure_database(conn: sqlite3.Connection) -> None:
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS uploads (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            seq INTEGER NOT NULL,
            uid TEXT,
            sid INTEGER,
            record_type INTEGER,
            record_ts INTEGER,
            dev INTEGER NOT NULL,
            peer TEXT NOT NULL,
            received_at TEXT NOT NULL,
            raw_line TEXT NOT NULL,
            UNIQUE(dev, seq)
        )
        """
    )
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS devices (
            dev INTEGER PRIMARY KEY,
            peer TEXT NOT NULL,
            first_seen TEXT NOT NULL,
            last_seen TEXT NOT NULL,
            heartbeat_count INTEGER NOT NULL DEFAULT 0,
            upload_count INTEGER NOT NULL DEFAULT 0,
            temperature_c REAL,
            firmware TEXT,
            blacklist_count INTEGER NOT NULL DEFAULT 0
        )
        """
    )
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS blacklist (
            uid TEXT PRIMARY KEY,
            sid INTEGER,
            reason TEXT,
            active INTEGER NOT NULL DEFAULT 1,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
        """
    )
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS ota_packages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            version TEXT NOT NULL,
            file_name TEXT NOT NULL,
            sha256 TEXT,
            size_bytes INTEGER,
            created_at TEXT NOT NULL
        )
        """
    )


@contextmanager
def _connect_db():
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_FILE, timeout=5.0)
    conn.row_factory = sqlite3.Row
    try:
        _ensure_database(conn)
        yield conn
    finally:
        conn.close()


def _append_log(peer: str, line: str, received_at: str) -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    with LOG_FILE.open("a", encoding="utf-8") as file:
        file.write(f"{received_at}\t{peer}\t{line}\n")


def _touch_device(
    conn: sqlite3.Connection,
    dev: int,
    peer: str,
    received_at: str,
    *,
    heartbeat: bool = False,
    upload_count: int = 0,
    temperature_c: float | None = None,
    firmware: str | None = None,
) -> None:
    bl_count = conn.execute(
        "SELECT COUNT(*) FROM blacklist WHERE active = 1"
    ).fetchone()[0]
    conn.execute(
        """
        INSERT INTO devices
            (dev, peer, first_seen, last_seen, heartbeat_count, upload_count,
             temperature_c, firmware, blacklist_count)
        VALUES
            (?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(dev) DO UPDATE SET
            peer = excluded.peer,
            last_seen = excluded.last_seen,
            heartbeat_count = devices.heartbeat_count + ?,
            upload_count = devices.upload_count + ?,
            temperature_c = COALESCE(excluded.temperature_c, devices.temperature_c),
            firmware = COALESCE(excluded.firmware, devices.firmware),
            blacklist_count = excluded.blacklist_count
        """,
        (
            dev,
            peer,
            received_at,
            received_at,
            1 if heartbeat else 0,
            upload_count,
            temperature_c,
            firmware,
            bl_count,
            1 if heartbeat else 0,
            upload_count,
        ),
    )


def _persist_upload_fields(
    conn: sqlite3.Connection,
    peer: str,
    fields: dict[str, str],
    received_at: str,
    raw_line: str,
    default_dev: int | None = None,
) -> int | None:
    seq = _parse_int(fields.get("SEQ"))
    if seq is None:
        return None
    dev = _parse_int(fields.get("DEV")) if fields.get("DEV") is not None else default_dev
    sid = _parse_int(fields.get("SID"))
    record_type = _parse_int(fields.get("TYPE"))
    record_ts = _parse_int(fields.get("TS"))
    uid = fields.get("UID")

    conn.execute(
        """
        INSERT INTO uploads
            (seq, uid, sid, record_type, record_ts, dev, peer, received_at, raw_line)
        VALUES
            (?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(dev, seq) DO UPDATE SET
            uid = excluded.uid,
            sid = excluded.sid,
            record_type = excluded.record_type,
            record_ts = excluded.record_ts,
            peer = excluded.peer,
            received_at = excluded.received_at,
            raw_line = excluded.raw_line
        """,
        (seq, uid, sid, record_type, record_ts, dev if dev is not None else 0, peer, received_at, raw_line),
    )
    _touch_device(conn, dev if dev is not None else 0, peer, received_at, upload_count=1)
    return seq


def _persist_upload(peer: str, line: str, received_at: str) -> str:
    fields = _parse_fields("UPLOAD:", line)
    seq_text = fields.get("SEQ", "UNKNOWN")
    with _connect_db() as conn:
        _persist_upload_fields(conn, peer, fields, received_at, line)
        conn.commit()
    return seq_text


def _parse_batch_record(item: str, default_dev: int | None) -> dict[str, str] | None:
    item = item.strip()
    if not item:
        return None
    parts = [part.strip() for part in item.split(",")]
    if len(parts) < 5:
        return None
    fields = {
        "SEQ": parts[0],
        "UID": parts[1].upper(),
        "SID": parts[2],
        "TYPE": parts[3],
        "TS": parts[4],
    }
    if len(parts) >= 6:
        fields["DEV"] = parts[5]
    elif default_dev is not None:
        fields["DEV"] = str(default_dev)
    return fields


def _persist_batch(peer: str, line: str, received_at: str) -> list[int]:
    fields = _parse_fields("UPLOADB:", line)
    dev = _parse_int(fields.get("DEV"))
    records = fields.get("R") or fields.get("RECS") or fields.get("ITEMS") or ""
    seqs: list[int] = []
    with _connect_db() as conn:
        for item in records.split(";"):
            record_fields = _parse_batch_record(item, dev)
            if record_fields is None:
                continue
            seq = _persist_upload_fields(conn, peer, record_fields, received_at, line, dev)
            if seq is not None:
                seqs.append(seq)
        conn.commit()
    return seqs


def _handle_heartbeat(peer: str, line: str, received_at: str) -> str:
    fields = _parse_fields("HEARTBEAT:", line)
    dev = _parse_int(fields.get("DEV")) or 0
    temp = _parse_float(fields.get("TEMP"))
    firmware = fields.get("FW")
    with _connect_db() as conn:
        _touch_device(
            conn,
            dev,
            peer,
            received_at,
            heartbeat=True,
            temperature_c=temp,
            firmware=firmware,
        )
        bl_count = conn.execute(
            "SELECT COUNT(*) FROM blacklist WHERE active = 1"
        ).fetchone()[0]
        conn.commit()
    return f"ACK:HEARTBEAT|BL={bl_count}"


def _handle_blacklist_query() -> str:
    with _connect_db() as conn:
        rows = conn.execute(
            "SELECT uid FROM blacklist WHERE active = 1 ORDER BY uid LIMIT 16"
        ).fetchall()
    uids = ",".join(row["uid"] for row in rows)
    return f"BL:COUNT={len(rows)}|UIDS={uids}"


def _ota_slot_from_line(line: str) -> str | None:
    if "?" in line and line.startswith("OTA?"):
        query = line.split("?", 1)[1]
        if query:
            fields = _parse_fields("", query.lstrip("|"))
            slot = fields.get("SLOT")
            if slot is None and query.upper().startswith("SLOT="):
                slot = query.split("=", 1)[1]
            if slot is not None:
                slot = slot.strip().upper()[:1]
                return slot if slot in ("A", "B") else None
    fields = _parse_fields("OTA:GET:", line)
    slot = fields.get("SLOT")
    if slot is not None:
        slot = slot.strip().upper()[:1]
        return slot if slot in ("A", "B") else None
    return None


def _handle_ota_query(line: str) -> str:
    slot = _ota_slot_from_line(line)
    info = _latest_ota_info(slot)
    if info is None:
        return "OTA:NONE"
    response = (
        "OTA:VERSION={version}|SIZE={size}|CRC32={crc32:08X}|CHUNK={chunk}|SLOT={slot}"
        .format(**info)
    )
    if slot not in ("A", "B"):
        response = response.rsplit("|SLOT=", 1)[0]
    return response


def _handle_ota_get(line: str) -> str:
    slot = _ota_slot_from_line(line)
    info = _latest_ota_info(slot)
    if info is None:
        return "OTA:ERR:NO_PACKAGE"
    fields = _parse_fields("OTA:GET:", line)
    offset = _parse_int(fields.get("OFFSET"))
    length = _parse_int(fields.get("LEN"))
    size = int(info["size"])
    if offset is None or length is None or offset < 0 or length <= 0:
        return "OTA:ERR:ARG"
    if offset >= size:
        return f"OTA:END|SIZE={size}|CRC32={int(info['crc32']):08X}"
    length = min(length, OTA_MAX_CHUNK_SIZE, size - offset)
    with Path(str(info["path"])).open("rb") as file:
        file.seek(offset)
        data = file.read(length)
    return (
        f"OTA:DATA:OFFSET={offset}|LEN={len(data)}|CRC32={_crc32_bytes(data):08X}|HEX={data.hex().upper()}"
    )


def _dispatch_line(peer: str, line: str, received_at: str) -> str:
    if line.startswith("HEARTBEAT:"):
        return _handle_heartbeat(peer, line, received_at)
    if line.startswith("UPLOAD:"):
        seq = _persist_upload(peer, line, received_at)
        return f"ACK:UPLOAD:{seq}"
    if line.startswith("UPLOADB:"):
        seqs = _persist_batch(peer, line, received_at)
        return "ACK:UPLOADB:" + ",".join(str(seq) for seq in seqs)
    if line.startswith("BL?") or line.startswith("BLACKLIST?"):
        return _handle_blacklist_query()
    if line.startswith("OTA?"):
        return _handle_ota_query(line)
    if line.startswith("OTA:GET:"):
        return _handle_ota_get(line)
    return "ERR:UNKNOWN"


class AttendanceHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        peer = f"{self.client_address[0]}:{self.client_address[1]}"
        while True:
            raw = self.rfile.readline()
            if not raw:
                break
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            response = self.handle_line(peer, line)
            self.wfile.write((response + "\n").encode("ascii", errors="replace"))
            self.wfile.flush()

    def handle_line(self, peer: str, line: str) -> str:
        received_at = _now()
        _append_log(peer, line, received_at)

        unwrapped = _unwrap_crc(line)
        if unwrapped is None:
            return "ERR:CRC"
        payload, wrapped = unwrapped
        response = _dispatch_line(peer, payload, received_at)
        return _wrap_crc(response) if wrapped else response


class ThreadingTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def _is_online(last_seen: str | None) -> bool:
    if not last_seen:
        return False
    try:
        last = datetime.fromisoformat(last_seen)
    except ValueError:
        return False
    return datetime.now() - last <= timedelta(seconds=HEARTBEAT_ONLINE_WINDOW_SEC)


def _html_page(title: str, body: str) -> bytes:
    nav = """
    <nav>
      <a href="/">设备</a>
      <a href="/records">考勤记录</a>
      <a href="/blacklist">黑名单</a>
      <a href="/ota">OTA</a>
    </nav>
    """
    page = f"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{html.escape(title)}</title>
  <style>
    body {{ margin: 0; font-family: Arial, "Microsoft YaHei", sans-serif; background: #f6f7f9; color: #17202a; }}
    header {{ background: #263238; color: white; padding: 14px 22px; }}
    main {{ padding: 18px 22px; max-width: 1180px; margin: 0 auto; }}
    nav {{ background: #ffffff; border-bottom: 1px solid #d8dee4; padding: 10px 22px; }}
    nav a {{ color: #1f5f99; margin-right: 18px; text-decoration: none; font-weight: 600; }}
    table {{ width: 100%; border-collapse: collapse; background: white; border: 1px solid #d8dee4; }}
    th, td {{ border-bottom: 1px solid #e6eaee; padding: 8px 10px; text-align: left; font-size: 14px; }}
    th {{ background: #eef2f5; }}
    form {{ background: white; border: 1px solid #d8dee4; padding: 12px; margin-bottom: 14px; }}
    input, select {{ padding: 7px; margin: 4px 8px 4px 0; border: 1px solid #c8d0d8; border-radius: 4px; }}
    button, .button {{ padding: 7px 11px; border: 1px solid #1f5f99; border-radius: 4px; background: #1f5f99; color: white; text-decoration: none; }}
    .ok {{ color: #137333; font-weight: 700; }}
    .bad {{ color: #b3261e; font-weight: 700; }}
    .muted {{ color: #637083; }}
  </style>
</head>
<body>
  <header><h2>{html.escape(title)}</h2></header>
  {nav}
  <main>{body}</main>
</body>
</html>"""
    return page.encode("utf-8")


def _query_params(path: str) -> dict[str, str]:
    parsed = urlparse(path)
    raw = parse_qs(parsed.query)
    return {key: values[0] for key, values in raw.items() if values}


def _parse_time_filter(value: str | None) -> int | None:
    if not value:
        return None
    parsed = _parse_int(value)
    if parsed is not None:
        return parsed
    try:
        return int(datetime.fromisoformat(value).timestamp())
    except ValueError:
        return None


def _ota_dir() -> Path:
    return DATA_DIR / "ota"


def _ota_file(slot: str | None = None) -> Path:
    if slot in ("A", "B"):
        return _ota_dir() / OTA_SLOT_FILE_TEMPLATE.format(slot=slot)
    return _ota_dir() / OTA_FILE_NAME


def _ota_version_file(slot: str | None = None) -> Path:
    if slot in ("A", "B"):
        return _ota_dir() / f"version_{slot}.txt"
    return _ota_dir() / "version.txt"


def _crc32_bytes(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _crc32_file(path: Path) -> int:
    crc = 0
    with path.open("rb") as file:
        while True:
            chunk = file.read(4096)
            if not chunk:
                break
            crc = zlib.crc32(chunk, crc)
    return crc & 0xFFFFFFFF


def _latest_ota_info(slot: str | None = None) -> dict[str, object] | None:
    path = _ota_file(slot)
    if not path.exists() or not path.is_file():
        return None
    size = path.stat().st_size
    if size <= 0:
        return None
    version_path = _ota_version_file(slot)
    if version_path.exists():
        version = version_path.read_text(encoding="utf-8", errors="replace").strip()[:15]
    else:
        version = datetime.fromtimestamp(path.stat().st_mtime).strftime("%Y%m%d%H%M%S")
    return {
        "version": version or "current",
        "size": size,
        "crc32": _crc32_file(path),
        "chunk": OTA_CHUNK_SIZE,
        "path": str(path),
        "slot": slot or "-",
    }


class WebHandler(BaseHTTPRequestHandler):
    server_version = "NFCAttendanceWeb/1.0"

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path in ("/", "/devices"):
            self._send_html("设备列表", self._render_devices())
        elif parsed.path == "/records":
            self._send_html("考勤记录", self._render_records(_query_params(self.path)))
        elif parsed.path == "/blacklist":
            self._send_html("黑名单", self._render_blacklist())
        elif parsed.path == "/ota":
            self._send_html("OTA 升级", self._render_ota())
        elif parsed.path == "/api/devices":
            self._send_json(self._api_devices())
        elif parsed.path == "/api/records":
            self._send_json(self._api_records(_query_params(self.path)))
        elif parsed.path == "/api/blacklist":
            self._send_json(self._api_blacklist())
        elif parsed.path == "/api/ota":
            self._send_json(self._api_ota())
        else:
            self.send_error(404)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8", errors="replace")
        fields = {key: values[0] for key, values in parse_qs(body).items() if values}
        if parsed.path == "/blacklist/add":
            self._add_blacklist(fields)
            self._redirect("/blacklist")
        elif parsed.path == "/blacklist/toggle":
            self._toggle_blacklist(fields)
            self._redirect("/blacklist")
        else:
            self.send_error(404)

    def log_message(self, format: str, *args: object) -> None:
        return

    def _send_html(self, title: str, body: str) -> None:
        data = _html_page(title, body)
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _send_json(self, obj: object) -> None:
        data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _redirect(self, location: str) -> None:
        self.send_response(303)
        self.send_header("Location", location)
        self.end_headers()

    def _api_devices(self) -> list[dict[str, object]]:
        with _connect_db() as conn:
            rows = conn.execute("SELECT * FROM devices ORDER BY last_seen DESC").fetchall()
        return [
            {
                "dev": row["dev"],
                "peer": row["peer"],
                "last_seen": row["last_seen"],
                "online": _is_online(row["last_seen"]),
                "heartbeat_count": row["heartbeat_count"],
                "upload_count": row["upload_count"],
                "temperature_c": row["temperature_c"],
                "firmware": row["firmware"],
                "blacklist_count": row["blacklist_count"],
            }
            for row in rows
        ]

    def _record_query(self, params: dict[str, str]) -> list[sqlite3.Row]:
        clauses: list[str] = []
        values: list[object] = []
        dev = _parse_int(params.get("dev"))
        sid = _parse_int(params.get("sid"))
        start_ts = _parse_time_filter(params.get("from"))
        end_ts = _parse_time_filter(params.get("to"))
        if dev is not None:
            clauses.append("dev = ?")
            values.append(dev)
        if sid is not None:
            clauses.append("sid = ?")
            values.append(sid)
        if start_ts is not None:
            clauses.append("record_ts >= ?")
            values.append(start_ts)
        if end_ts is not None:
            clauses.append("record_ts <= ?")
            values.append(end_ts)
        where = " WHERE " + " AND ".join(clauses) if clauses else ""
        with _connect_db() as conn:
            return conn.execute(
                f"""
                SELECT seq, uid, sid, record_type, record_ts, dev, peer, received_at
                FROM uploads
                {where}
                ORDER BY record_ts DESC, received_at DESC
                LIMIT 500
                """,
                values,
            ).fetchall()

    def _api_records(self, params: dict[str, str]) -> list[dict[str, object]]:
        rows = self._record_query(params)
        return [dict(row) for row in rows]

    def _api_blacklist(self) -> list[dict[str, object]]:
        with _connect_db() as conn:
            rows = conn.execute("SELECT * FROM blacklist ORDER BY updated_at DESC").fetchall()
        return [dict(row) for row in rows]

    def _api_ota(self) -> dict[str, object]:
        info = _latest_ota_info()
        return {"available": info is not None, **(info or {})}

    def _render_devices(self) -> str:
        devices = self._api_devices()
        rows = []
        for item in devices:
            status = '<span class="ok">在线</span>' if item["online"] else '<span class="bad">离线</span>'
            temp = item["temperature_c"]
            temp_text = f"{temp:.1f} C" if isinstance(temp, (int, float)) else '<span class="muted">N/A</span>'
            rows.append(
                "<tr>"
                f"<td>{item['dev']}</td><td>{html.escape(str(item['peer']))}</td><td>{status}</td>"
                f"<td>{html.escape(str(item['last_seen']))}</td><td>{temp_text}</td>"
                f"<td>{item['heartbeat_count']}</td><td>{item['upload_count']}</td>"
                f"<td>{html.escape(str(item['firmware'] or ''))}</td><td>{item['blacklist_count']}</td>"
                "</tr>"
            )
        empty = '<tr><td colspan="9">暂无设备</td></tr>'
        return (
            "<table><thead><tr><th>设备</th><th>地址</th><th>状态</th><th>最后心跳</th>"
            "<th>温度</th><th>心跳数</th><th>上传数</th><th>固件</th><th>黑名单数</th></tr></thead>"
            f"<tbody>{''.join(rows) or empty}</tbody></table>"
        )

    def _render_records(self, params: dict[str, str]) -> str:
        rows = self._record_query(params)
        form = f"""
        <form method="get" action="/records">
          设备 <input name="dev" value="{html.escape(params.get('dev', ''))}" placeholder="1">
          工号 <input name="sid" value="{html.escape(params.get('sid', ''))}" placeholder="1001">
          起始 <input name="from" value="{html.escape(params.get('from', ''))}" placeholder="Unix 或 2026-07-03T08:00:00">
          结束 <input name="to" value="{html.escape(params.get('to', ''))}" placeholder="Unix 或 ISO 时间">
          <button type="submit">筛选</button>
          <a class="button" href="/records">清空</a>
        </form>
        """
        body_rows = []
        for row in rows:
            body_rows.append(
                "<tr>"
                f"<td>{row['dev']}</td><td>{row['seq']}</td><td>{html.escape(str(row['uid'] or ''))}</td>"
                f"<td>{row['sid'] or ''}</td><td>{row['record_type'] if row['record_type'] is not None else ''}</td>"
                f"<td>{row['record_ts'] or ''}</td><td>{html.escape(str(row['received_at']))}</td>"
                f"<td>{html.escape(str(row['peer']))}</td>"
                "</tr>"
            )
        empty = '<tr><td colspan="8">暂无记录</td></tr>'
        table = (
            "<table><thead><tr><th>设备</th><th>序号</th><th>UID</th><th>工号</th><th>类型</th>"
            "<th>刷卡时间</th><th>服务器接收</th><th>来源</th></tr></thead>"
            f"<tbody>{''.join(body_rows) or empty}</tbody></table>"
        )
        return form + table

    def _render_blacklist(self) -> str:
        rows = self._api_blacklist()
        form = """
        <form method="post" action="/blacklist/add">
          UID <input name="uid" maxlength="8" required placeholder="A1B2C3D4">
          工号 <input name="sid" placeholder="1001">
          原因 <input name="reason" placeholder="lost card">
          <button type="submit">加入黑名单</button>
        </form>
        """
        body_rows = []
        for row in rows:
            active = "启用" if row["active"] else "停用"
            next_active = "0" if row["active"] else "1"
            body_rows.append(
                "<tr>"
                f"<td>{html.escape(str(row['uid']))}</td><td>{row['sid'] or ''}</td>"
                f"<td>{html.escape(str(row['reason'] or ''))}</td><td>{active}</td>"
                f"<td>{html.escape(str(row['updated_at']))}</td>"
                "<td><form method=\"post\" action=\"/blacklist/toggle\">"
                f"<input type=\"hidden\" name=\"uid\" value=\"{html.escape(str(row['uid']))}\">"
                f"<input type=\"hidden\" name=\"active\" value=\"{next_active}\">"
                "<button type=\"submit\">切换</button></form></td></tr>"
            )
        empty = '<tr><td colspan="6">暂无黑名单</td></tr>'
        table = (
            "<table><thead><tr><th>UID</th><th>工号</th><th>原因</th><th>状态</th><th>更新</th><th>操作</th></tr></thead>"
            f"<tbody>{''.join(body_rows) or empty}</tbody></table>"
        )
        return form + table

    def _render_ota(self) -> str:
        infos = [info for info in (_latest_ota_info("A"), _latest_ota_info("B"), _latest_ota_info(None))
                 if info is not None]
        if not infos:
            package = (
                "<p>No OTA package. Put a binary at <code>server/data/ota/current_A.bin</code>, "
                "<code>server/data/ota/current_B.bin</code>, or legacy <code>server/data/ota/current.bin</code>.</p>"
            )
        else:
            rows = []
            seen_paths = set()
            for info in infos:
                path = str(info["path"])
                if path in seen_paths:
                    continue
                seen_paths.add(path)
                rows.append(
                    "<tr>"
                    f"<td>{html.escape(str(info['slot']))}</td>"
                    f"<td>{html.escape(str(info['version']))}</td>"
                    f"<td>{info['size']}</td>"
                    f"<td>{int(info['crc32']):08X}</td>"
                    f"<td>{info['chunk']}</td>"
                    f"<td>{html.escape(path)}</td>"
                    "</tr>"
                )
            package = (
                "<table><thead><tr><th>Slot</th><th>Version</th><th>Size</th><th>CRC32</th><th>Chunk</th><th>Path</th></tr></thead>"
                f"<tbody>{''.join(rows)}</tbody></table>"
            )
        return (
            "<p>A/B OTA is enabled: <code>OTA?SLOT=A/B</code> advertises the requested slot package and "
            "<code>OTA:GET:OFFSET=n|LEN=m|SLOT=A/B</code> returns CRC-checked hex chunks.</p>"
            f"{package}"
            "<p>The firmware downloads and verifies the package into W25Q128; "
            "the bootloader then installs it into slot A or slot B and records boot state in LittleFS.</p>"
        )

    def _add_blacklist(self, fields: dict[str, str]) -> None:
        uid = fields.get("uid", "").strip().upper()
        if len(uid) != 8 or any(ch not in "0123456789ABCDEF" for ch in uid):
            return
        sid = _parse_int(fields.get("sid"))
        reason = fields.get("reason", "").strip()
        now = _now()
        with _connect_db() as conn:
            conn.execute(
                """
                INSERT INTO blacklist (uid, sid, reason, active, created_at, updated_at)
                VALUES (?, ?, ?, 1, ?, ?)
                ON CONFLICT(uid) DO UPDATE SET
                    sid = excluded.sid,
                    reason = excluded.reason,
                    active = 1,
                    updated_at = excluded.updated_at
                """,
                (uid, sid, reason, now, now),
            )
            conn.commit()

    def _toggle_blacklist(self, fields: dict[str, str]) -> None:
        uid = fields.get("uid", "").strip().upper()
        active = 1 if fields.get("active") == "1" else 0
        with _connect_db() as conn:
            conn.execute(
                "UPDATE blacklist SET active = ?, updated_at = ? WHERE uid = ?",
                (active, _now(), uid),
            )
            conn.commit()


def run_tcp_server(host: str, port: int) -> None:
    with ThreadingTCPServer((host, port), AttendanceHandler) as server:
        print(f"tcp listening on {host}:{port}")
        server.serve_forever()


def run_web_server(host: str, port: int) -> None:
    with ThreadingHTTPServer((host, port), WebHandler) as server:
        print(f"web listening on http://{host}:{port}")
        server.serve_forever()


def main() -> None:
    parser = argparse.ArgumentParser(description="NFC attendance TCP/Web server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--web-host", default="0.0.0.0")
    parser.add_argument("--web-port", type=int, default=8080)
    parser.add_argument("--no-web", action="store_true")
    args = parser.parse_args()

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    with _connect_db():
        pass

    if args.no_web:
        run_tcp_server(args.host, args.port)
        return

    tcp_thread = threading.Thread(
        target=run_tcp_server,
        args=(args.host, args.port),
        daemon=True,
    )
    tcp_thread.start()
    run_web_server(args.web_host, args.web_port)


if __name__ == "__main__":
    main()
