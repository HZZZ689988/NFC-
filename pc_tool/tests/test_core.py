from pathlib import Path
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
    build_clear,
    build_config_commands,
    build_crc_frame,
    build_issue,
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
    assert build_clear("a1-b2-c3-d4") == "CLEAR:A1B2C3D4\n"


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


def test_success_response_accepts_ok_prefix() -> None:
    from nfc_attendance_tool.protocol import is_success_response

    assert is_success_response("OK")
    assert is_success_response("OK:ISSUE")
    assert not is_success_response("ERR:CARD")


def test_server_ack_upload_sequence() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        test_server.DATA_DIR = Path(tmp)
        test_server.LOG_FILE = Path(tmp) / "uploads.log"

        response = test_server.AttendanceHandler.handle_line(
            None,
            "127.0.0.1:12345",
            "UPLOAD:SEQ=42|UID=A1B2C3D4|SID=1001|TYPE=2|TS=1782691200|DEV=1",
        )

        assert response == "ACK:UPLOAD:42"


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
