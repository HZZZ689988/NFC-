from pathlib import Path
import csv
import sqlite3
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT.parent))

from nfc_attendance_tool.database import Database, Person, parse_record_line
from nfc_attendance_tool.image_codec import build_image_blocks
from nfc_attendance_tool.protocol import (
    CardType,
    DeviceConfigPayload,
    PersonPayload,
    build_card_lock,
    build_card_lock_query,
    build_clear,
    build_config_commands,
    build_crc_frame,
    build_image_block,
    build_issue,
    build_network_query,
    build_ota_download,
    build_ota_install_reset,
    build_ota_status_query,
    build_time_query,
    build_weather_force_query,
    build_weather_query,
    build_weather_test,
    chunk_commands,
    crc16_ccitt_false,
    parse_crc_frame,
)
from nfc_attendance_tool.serial_client import SerialClient
from server import server as test_server


def test_protocol_commands() -> None:
    payload = PersonPayload("a1 b2 c3 d4", 1001, 0, CardType.IMAGE)
    assert build_issue(payload) == "ISSUE:A1B2C3D4,1001,0,1\n"
    admin_payload = PersonPayload("a1b2c3d4", 9001, 0, CardType.ADMIN)
    assert build_issue(admin_payload) == "ISSUE:A1B2C3D4,9001,0,2\n"
    assert build_clear("a1-b2-c3-d4") == "CLEAR:A1B2C3D4\n"
    assert build_card_lock(True) == "CARDLOCK:ON\n"
    assert build_card_lock(False) == "CARDLOCK:OFF\n"
    assert build_card_lock_query() == "CARDLOCK?\n"


def test_crc16_frame_roundtrip() -> None:
    assert crc16_ccitt_false("123456789") == 0x29B1
    frame = build_crc_frame("PING")
    assert frame == "$PING*6427\n"
    assert parse_crc_frame(frame) == "PING"


def test_build_config_commands() -> None:
    commands = build_config_commands(
        DeviceConfigPayload(
            device_id=7,
            work_mode=3,
            upload_enable=True,
            repeat_interval_sec=60,
            wifi_ssid="test-ssid",
            wifi_password="secret",
            server_host="192.168.1.10",
            server_port=9000,
            weather_key="weather-key",
            weather_location="hangzhou",
            timezone=8,
        )
    )

    assert commands == [
        "CFG:DEV=7|MODE=3|UPLOAD=1|REPEAT=60|TZ=8\n",
        "CFG:SSID=test-ssid\n",
        "CFG:PWD=secret\n",
        "CFG:HOST=192.168.1.10|PORT=9000\n",
        "CFG:WKEY=weather-key|WLOC=hangzhou\n",
    ]


def test_build_config_rejects_ambiguous_text() -> None:
    try:
        build_config_commands(
            DeviceConfigPayload(1, 3, True, 60, "ssid|bad", "", "127.0.0.1", 9000, "", "hangzhou", 8)
        )
    except ValueError:
        pass
    else:
        raise AssertionError("ambiguous config text was accepted")


def test_build_weather_commands() -> None:
    assert build_weather_query() == "WEATHER?\n"
    assert build_weather_test(" Sunny 20C ") == "WEATHERTEST:Sunny 20C\n"
    assert build_weather_force_query() == "WEATHER!\n"
    assert build_time_query() == "TIME?\n"
    assert build_network_query() == "NET?\n"
    assert build_ota_status_query() == "OTA?\n"
    assert build_ota_download() == "OTA!\n"
    assert build_ota_install_reset() == "OTARST\n"


def test_build_weather_test_rejects_ambiguous_text() -> None:
    for value in ("", "Bad|Text", "Bad=Text"):
        try:
            build_weather_test(value)
        except ValueError:
            pass
        else:
            raise AssertionError("ambiguous weather text was accepted")


def test_issue_rejects_uint32_overflow() -> None:
    try:
        build_issue(PersonPayload("A1B2C3D4", 0x1_0000_0000, 0, CardType.IMAGE))
    except ValueError:
        pass
    else:
        raise AssertionError("SID overflow was accepted")


def test_image_blocks_shape() -> None:
    blocks = build_image_blocks(None, "张三", "计算机学院")
    assert len(blocks.portrait) == 24
    assert len(blocks.name) == 10
    assert len(blocks.department) == 10
    commands = chunk_commands("IMGN", blocks.name)
    assert commands[0].startswith("IMGN00:")
    assert commands[-1].startswith("IMGN09:")


def test_image_command_validation() -> None:
    assert build_image_block("IMGA", 23, "00112233445566778899AABBCCDDEEFF") == (
        "IMGA23:00112233445566778899AABBCCDDEEFF\n"
    )

    for prefix, index, hex32 in (
        ("IMGA", 24, "00112233445566778899AABBCCDDEEFF"),
        ("IMGN", 10, "00112233445566778899AABBCCDDEEFF"),
        ("IMGD", 10, "00112233445566778899AABBCCDDEEFF"),
        ("BAD", 0, "00112233445566778899AABBCCDDEEFF"),
        ("IMGA", 0, "00112233445566778899AABBCCDDEEFG"),
    ):
        try:
            build_image_block(prefix, index, hex32)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid image block command was accepted")

    try:
        chunk_commands("IMGA", [b"short"])
    except ValueError:
        pass
    else:
        raise AssertionError("short image block was accepted")


def test_record_parse() -> None:
    parsed = parse_record_line("REC:SEQ=7|UID=A1B2C3D4|SID=1001|IN|2026-06-25 09:30:00|DEV=2|OK")
    assert parsed["seq"] == 7
    assert parsed["uid_hex"] == "A1B2C3D4"
    assert parsed["sid"] == 1001
    assert parsed["record_type"] == "IN"
    assert parsed["device_id"] == 2
    assert parsed["status"] == "OK"


def test_database_roundtrip() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        db = Database(Path(tmp) / "attendance.db")
        db.upsert_person(Person("A1B2C3D4", 1001, "张三", "研发部", 1))
        assert db.list_people()[0]["name"] == "张三"
        db.import_record_line("REC:SEQ=1|UID=A1B2C3D4|SID=1001|IN|2026-06-25 09:30:00|DEV=1|OK")
        assert db.list_attendance()[0]["sid"] == 1001
        db.close()


def test_database_ignores_duplicate_device_seq() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        db = Database(Path(tmp) / "attendance.db")
        line = "REC:SEQ=1|UID=A1B2C3D4|SID=1001|IN|2026-06-25 09:30:00|DEV=1|OK"
        db.import_record_line(line)
        db.import_record_line(line)
        assert len(db.list_attendance()) == 1
        db.close()


def test_record_parse_upload_state() -> None:
    parsed = parse_record_line("REC:SEQ=8|UID=A1B2C3D5|SID=1002|NORMAL|1783014648|DEV=1|OK|UP=done")

    assert parsed["seq"] == 8
    assert parsed["record_type"] == "NORMAL"
    assert parsed["occurred_at"] == "1783014648"
    assert parsed["upload_state"] == "DONE"


def test_database_issue_log_and_lost_flag() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        db = Database(Path(tmp) / "attendance.db")
        person = Person("A1B2C3D6", 1003, "Alice", "R&D", 1, 5)

        db.upsert_person(person)
        db.add_issue_log(person, "ISSUE", "OK")
        db.mark_lost(person.uid_hex, True)
        assert db.list_people()[0]["lost"] == 1

        db.mark_lost(person.uid_hex, False)
        assert db.list_people()[0]["lost"] == 0

        issue_log = db.conn.execute(
            "SELECT uid_hex, sid, name, department, card_type, action, status FROM issue_logs"
        ).fetchone()
        assert dict(issue_log) == {
            "uid_hex": "A1B2C3D6",
            "sid": 1003,
            "name": "Alice",
            "department": "R&D",
            "card_type": 1,
            "action": "ISSUE",
            "status": "OK",
        }
        db.close()


def test_database_imports_and_exports_upload_state() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        db = Database(Path(tmp) / "attendance.db")
        line = "REC:SEQ=2|UID=A1B2C3D7|SID=1004|NORMAL|1783014648|DEV=1|OK|UP=DONE"
        db.import_record_line(line)

        row = db.list_attendance()[0]
        assert row["upload_state"] == "DONE"

        csv_path = Path(tmp) / "attendance.csv"
        db.export_attendance_csv(csv_path)
        raw = csv_path.read_bytes()
        assert raw.startswith(b"\xef\xbb\xbf")

        with csv_path.open("r", newline="", encoding="utf-8-sig") as handle:
            rows = list(csv.reader(handle))

        assert rows[0] == [
            "seq",
            "uid_hex",
            "sid",
            "record_type",
            "occurred_at",
            "device_id",
            "status",
            "upload_state",
            "imported_at",
        ]
        assert rows[1][:8] == ["2", "A1B2C3D7", "1004", "NORMAL", "1783014648", "1", "OK", "DONE"]
        db.close()


def test_database_refreshes_duplicate_upload_state() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        db = Database(Path(tmp) / "attendance.db")
        pending = "REC:SEQ=4|UID=A1B2C3D9|SID=1006|NORMAL|1783014651|DEV=1|OK|UP=PENDING"
        done = "REC:SEQ=4|UID=A1B2C3D9|SID=1006|NORMAL|1783014651|DEV=1|OK|UP=DONE"

        db.import_record_line(pending)
        db.import_record_line(done)
        rows = db.list_attendance()

        assert len(rows) == 1
        assert rows[0]["upload_state"] == "DONE"
        db.close()


def test_database_migrates_upload_state_column() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "attendance.db"
        conn = sqlite3.connect(path)
        conn.executescript(
            """
            CREATE TABLE attendance_records (
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
            """
        )
        conn.close()

        db = Database(path)
        columns = {
            row["name"]
            for row in db.conn.execute("PRAGMA table_info(attendance_records)")
        }
        assert "upload_state" in columns

        db.import_record_line("REC:SEQ=3|UID=A1B2C3D8|SID=1005|NORMAL|1783014650|DEV=1|OK|UP=PENDING")
        assert db.list_attendance()[0]["upload_state"] == "PENDING"
        db.close()


class FakeSerial:
    def __init__(self, client: SerialClient | None = None) -> None:
        self.is_open = True
        self.writes: list[bytes] = []
        self.client = client
        self.responses = ["UID:A1B2C3D4"]

    def write(self, data: bytes) -> None:
        self.writes.append(data)
        if self.client:
            for response in self.responses:
                self.client._line_queue.put(response)  # type: ignore[attr-defined]

    def flush(self) -> None:
        pass


def test_serial_transact_drains_stale_lines() -> None:
    client = SerialClient()
    fake = FakeSerial(client)
    client._serial = fake  # type: ignore[attr-defined]
    client._line_queue.put("OK")

    lines = client.transact("READ\n", timeout=0.2)

    assert fake.writes == [b"READ\n"]
    assert lines == ["UID:A1B2C3D4"]


def test_serial_transact_accepts_ok_prefix() -> None:
    client = SerialClient()
    fake = FakeSerial(client)
    fake.responses = ["OK:ISSUE"]
    client._serial = fake  # type: ignore[attr-defined]

    lines = client.transact("ISSUE:A1B2C3D4,1001,0,1\n", timeout=0.2)

    assert fake.writes == [b"ISSUE:A1B2C3D4,1001,0,1\n"]
    assert lines == ["OK:ISSUE"]


def test_serial_transact_accepts_weather_response() -> None:
    client = SerialClient()
    fake = FakeSerial(client)
    fake.responses = ["WEATHER:Sunny 20C"]
    client._serial = fake  # type: ignore[attr-defined]

    lines = client.transact("WEATHER?\n", timeout=0.2)

    assert fake.writes == [b"WEATHER?\n"]
    assert lines == ["WEATHER:Sunny 20C"]


def test_serial_transact_accepts_time_response() -> None:
    client = SerialClient()
    fake = FakeSerial(client)
    fake.responses = ["TIME:1782999000|VALID=1"]
    client._serial = fake  # type: ignore[attr-defined]

    lines = client.transact("TIME?\n", timeout=0.2)

    assert fake.writes == [b"TIME?\n"]
    assert lines == ["TIME:1782999000|VALID=1"]


def test_serial_transact_accepts_network_response() -> None:
    client = SerialClient()
    fake = FakeSerial(client)
    fake.responses = ["NET:READY=0|CFG=1|UPLOAD=1|STATE=2|TEXT=WIFI_CONNECTING"]
    client._serial = fake  # type: ignore[attr-defined]

    lines = client.transact("NET?\n", timeout=0.2)

    assert fake.writes == [b"NET?\n"]
    assert lines == ["NET:READY=0|CFG=1|UPLOAD=1|STATE=2|TEXT=WIFI_CONNECTING"]


def test_serial_transact_accepts_ota_response() -> None:
    client = SerialClient()
    fake = FakeSerial(client)
    fake.responses = ["OTA:STATE=READY|VER=v1|RX=4|SIZE=4|CRC32=12345678|ACT=12345678"]
    client._serial = fake  # type: ignore[attr-defined]

    lines = client.transact("OTA?\n", timeout=0.2)

    assert fake.writes == [b"OTA?\n"]
    assert lines == ["OTA:STATE=READY|VER=v1|RX=4|SIZE=4|CRC32=12345678|ACT=12345678"]


def test_serial_transact_accepts_cardlock_query_response() -> None:
    client = SerialClient()
    fake = FakeSerial(client)
    fake.responses = ["CARDLOCK:ON"]
    client._serial = fake  # type: ignore[attr-defined]

    lines = client.transact("CARDLOCK?\n", timeout=0.2)

    assert fake.writes == [b"CARDLOCK?\n"]
    assert lines == ["CARDLOCK:ON"]


def test_success_response_accepts_ok_prefix() -> None:
    from nfc_attendance_tool.protocol import is_success_response

    assert is_success_response("OK")
    assert is_success_response("OK:ISSUE")
    assert not is_success_response("ERR:CARD")


def test_server_ack_upload_sequence() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        test_server.DATA_DIR = Path(tmp)
        test_server.LOG_FILE = Path(tmp) / "uploads.log"
        test_server.DB_FILE = Path(tmp) / "attendance.db"

        response = test_server.AttendanceHandler.handle_line(
            None,
            "127.0.0.1:12345",
            "UPLOAD:SEQ=42|UID=A1B2C3D4|SID=1001|TYPE=2|TS=1782691200|DEV=1",
        )

        assert response == "ACK:UPLOAD:42"
        assert test_server.LOG_FILE.read_text(encoding="utf-8").count("UPLOAD:SEQ=42") == 1

        conn = sqlite3.connect(test_server.DB_FILE)
        try:
            row = conn.execute(
                """
                SELECT seq, uid, sid, record_type, record_ts, dev, peer
                FROM uploads
                WHERE dev = 1 AND seq = 42
                """
            ).fetchone()
        finally:
            conn.close()

        assert row == (42, "A1B2C3D4", 1001, 2, 1782691200, 1, "127.0.0.1:12345")


def test_server_heartbeat_batch_crc_and_blacklist() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        test_server.DATA_DIR = Path(tmp)
        test_server.LOG_FILE = Path(tmp) / "uploads.log"
        test_server.DB_FILE = Path(tmp) / "attendance.db"

        heartbeat = test_server.AttendanceHandler.handle_line(
            None,
            "127.0.0.1:12345",
            "HEARTBEAT:DEV=7|TEMP=26.5|FW=1.2.3",
        )
        assert heartbeat == "ACK:HEARTBEAT|BL=0"

        batch = test_server.AttendanceHandler.handle_line(
            None,
            "127.0.0.1:12345",
            "UPLOADB:DEV=7|R=50,AABBCCDD,2001,0,1782691201;51,11223344,2002,1,1782691202",
        )
        assert batch == "ACK:UPLOADB:50,51"

        payload = "UPLOAD:SEQ=52|UID=55667788|SID=2003|TYPE=2|TS=1782691203|DEV=7"
        crc_line = f"CRC:{payload}*{test_server._crc16_ccitt_false(payload):04X}"
        crc_response = test_server.AttendanceHandler.handle_line(
            None,
            "127.0.0.1:12345",
            crc_line,
        )
        assert crc_response.startswith("CRC:ACK:UPLOAD:52*")

        conn = sqlite3.connect(test_server.DB_FILE)
        try:
            upload_count = conn.execute("SELECT COUNT(*) FROM uploads").fetchone()[0]
            device = conn.execute(
                "SELECT dev, heartbeat_count, upload_count, temperature_c, firmware FROM devices WHERE dev = 7"
            ).fetchone()
            conn.execute(
                """
                INSERT INTO blacklist(uid, sid, reason, active, created_at, updated_at)
                VALUES('AABBCCDD', 2001, 'lost', 1, '2026-07-03T00:00:00', '2026-07-03T00:00:00')
                """
            )
            conn.commit()
        finally:
            conn.close()

        assert upload_count == 3
        assert device == (7, 1, 3, 26.5, "1.2.3")

        blacklist = test_server.AttendanceHandler.handle_line(None, "127.0.0.1:12345", "BL?")
        assert blacklist == "BL:COUNT=1|UIDS=AABBCCDD"


def test_server_ota_query_and_chunk_protocol() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        test_server.DATA_DIR = Path(tmp)
        test_server.LOG_FILE = Path(tmp) / "uploads.log"
        test_server.DB_FILE = Path(tmp) / "attendance.db"
        ota_dir = Path(tmp) / "ota"
        ota_dir.mkdir()
        package = bytes([0x01, 0x02, 0xA5, 0x5A])
        (ota_dir / "current.bin").write_bytes(package)
        (ota_dir / "version.txt").write_text("v1", encoding="utf-8")

        crc32 = test_server._crc32_bytes(package)
        response = test_server.AttendanceHandler.handle_line(None, "127.0.0.1:12345", "OTA?")
        assert response == f"OTA:VERSION=v1|SIZE=4|CRC32={crc32:08X}|CHUNK=192"

        chunk = test_server.AttendanceHandler.handle_line(
            None,
            "127.0.0.1:12345",
            "OTA:GET:OFFSET=1|LEN=2",
        )
        chunk_crc32 = test_server._crc32_bytes(package[1:3])
        assert chunk == f"OTA:DATA:OFFSET=1|LEN=2|CRC32={chunk_crc32:08X}|HEX=02A5"

        payload = "OTA?"
        wrapped = f"CRC:{payload}*{test_server._crc16_ccitt_false(payload):04X}"
        wrapped_response = test_server.AttendanceHandler.handle_line(
            None,
            "127.0.0.1:12345",
            wrapped,
        )
        assert wrapped_response.startswith("CRC:OTA:VERSION=v1|SIZE=4|")
        assert test_server._unwrap_crc(wrapped_response) is not None


def test_server_ota_slot_specific_package_protocol() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        test_server.DATA_DIR = Path(tmp)
        test_server.LOG_FILE = Path(tmp) / "uploads.log"
        test_server.DB_FILE = Path(tmp) / "attendance.db"
        ota_dir = Path(tmp) / "ota"
        ota_dir.mkdir()
        package_a = bytes([0xAA, 0x01])
        package_b = bytes([0xBB, 0x02, 0x03])
        (ota_dir / "current_A.bin").write_bytes(package_a)
        (ota_dir / "current_B.bin").write_bytes(package_b)
        (ota_dir / "version_A.txt").write_text("slot-a", encoding="utf-8")
        (ota_dir / "version_B.txt").write_text("slot-b", encoding="utf-8")

        crc_b = test_server._crc32_bytes(package_b)
        response = test_server.AttendanceHandler.handle_line(None, "127.0.0.1:12345", "OTA?SLOT=B")
        assert response == f"OTA:VERSION=slot-b|SIZE=3|CRC32={crc_b:08X}|CHUNK=192|SLOT=B"

        chunk = test_server.AttendanceHandler.handle_line(
            None,
            "127.0.0.1:12345",
            "OTA:GET:OFFSET=1|LEN=2|SLOT=B",
        )
        chunk_crc32 = test_server._crc32_bytes(package_b[1:3])
        assert chunk == f"OTA:DATA:OFFSET=1|LEN=2|CRC32={chunk_crc32:08X}|HEX=0203"


def test_server_ota_slot_request_does_not_fall_back_to_legacy_package() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        test_server.DATA_DIR = Path(tmp)
        test_server.LOG_FILE = Path(tmp) / "uploads.log"
        test_server.DB_FILE = Path(tmp) / "attendance.db"
        ota_dir = Path(tmp) / "ota"
        ota_dir.mkdir()
        (ota_dir / "current.bin").write_bytes(bytes([0x01, 0x02]))
        (ota_dir / "version.txt").write_text("legacy", encoding="utf-8")

        response = test_server.AttendanceHandler.handle_line(None, "127.0.0.1:12345", "OTA?SLOT=B")
        assert response == "OTA:NONE"


def _run_tests() -> None:
    tests = [
        value
        for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    for test in tests:
        test()


if __name__ == "__main__":
    _run_tests()
