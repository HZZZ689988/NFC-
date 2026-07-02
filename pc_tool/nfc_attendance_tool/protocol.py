from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import re
from typing import Iterable


UID_PATTERN = re.compile(r"^[0-9A-F]{8}$")
HEX16_PATTERN = re.compile(r"^[0-9A-F]{32}$")
UINT32_MAX = 0xFFFFFFFF


class CardType(IntEnum):
    NORMAL = 0
    IMAGE = 1
    ADMIN = 2


@dataclass(frozen=True)
class PersonPayload:
    uid_hex: str
    sid: int
    points: int
    card_type: CardType


@dataclass(frozen=True)
class DeviceConfigPayload:
    device_id: int
    work_mode: int
    upload_enable: bool
    repeat_interval_sec: int
    wifi_ssid: str
    wifi_password: str
    server_host: str
    server_port: int
    weather_key: str
    weather_location: str
    timezone: int


def normalize_uid(uid: str) -> str:
    value = uid.strip().replace(" ", "").replace("-", "").upper()
    if not UID_PATTERN.fullmatch(value):
        raise ValueError("UID 必须是 8 位十六进制字符")
    return value


def validate_hex_block(hex32: str) -> str:
    value = hex32.strip().upper()
    if not HEX16_PATTERN.fullmatch(value):
        raise ValueError("图像块必须是 32 位十六进制字符")
    return value


def crc16_ccitt_false(data: bytes | str) -> int:
    if isinstance(data, str):
        raw = data.encode("ascii")
    else:
        raw = data
    crc = 0xFFFF
    for byte in raw:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_crc_frame(payload: str) -> str:
    value = payload.strip()
    if not value:
        raise ValueError("协议载荷不能为空")
    return f"${value}*{crc16_ccitt_false(value):04X}\n"


def parse_crc_frame(line: str) -> str:
    value = line.strip()
    if not value.startswith("$"):
        return value
    try:
        payload, crc_text = value[1:].rsplit("*", 1)
    except ValueError as exc:
        raise ValueError("CRC 帧格式错误") from exc
    if len(crc_text) != 4:
        raise ValueError("CRC 长度错误")
    expected = int(crc_text, 16)
    actual = crc16_ccitt_false(payload)
    if expected != actual:
        raise ValueError(f"CRC 校验失败: expected={expected:04X}, actual={actual:04X}")
    return payload


def build_read() -> str:
    return "READ\n"


def build_config_query() -> str:
    return "CFG?\n"


def build_weather_query() -> str:
    return "WEATHER?\n"


def build_weather_test(text: str) -> str:
    value = _validate_config_text(text, "Weather text", 31)
    if not value:
        raise ValueError("Weather text cannot be empty")
    return f"WEATHERTEST:{value}\n"


def build_weather_force_query() -> str:
    return "WEATHER!\n"


def build_time_query() -> str:
    return "TIME?\n"


def build_clear(uid_hex: str) -> str:
    return f"CLEAR:{normalize_uid(uid_hex)}\n"


def build_list(count: int | None = None) -> str:
    if count is None:
        return "LIST:ALL\n"
    if count <= 0:
        raise ValueError("记录数量必须大于 0")
    if count > UINT32_MAX:
        raise ValueError("记录数量不能超过 32 位无符号整数范围")
    return f"LIST:{count}\n"


def build_issue(payload: PersonPayload) -> str:
    uid = normalize_uid(payload.uid_hex)
    if payload.sid < 0:
        raise ValueError("工号不能为负数")
    if payload.points < 0:
        raise ValueError("积分不能为负数")
    if payload.sid > UINT32_MAX:
        raise ValueError("工号不能超过 32 位无符号整数范围")
    if payload.points > UINT32_MAX:
        raise ValueError("积分不能超过 32 位无符号整数范围")
    return f"ISSUE:{uid},{payload.sid},{payload.points},{int(payload.card_type)}\n"


def _validate_config_text(value: str, label: str, max_len: int) -> str:
    text = value.strip()
    if len(text) > max_len:
        raise ValueError(f"{label} 不能超过 {max_len} 个字符")
    if any(ord(ch) < 0x20 or ch in "|=" for ch in text):
        raise ValueError(f"{label} 不能包含 |、= 或控制字符")
    return text


def build_config_commands(payload: DeviceConfigPayload) -> list[str]:
    if payload.device_id <= 0 or payload.device_id > UINT32_MAX:
        raise ValueError("设备 ID 必须在 1..4294967295 范围内")
    if payload.work_mode < 0 or payload.work_mode > 3:
        raise ValueError("工作模式必须在 0..3 范围内")
    if payload.repeat_interval_sec < 0 or payload.repeat_interval_sec > 0xFFFF:
        raise ValueError("防重复间隔必须在 0..65535 秒范围内")
    if payload.server_port <= 0 or payload.server_port > 0xFFFF:
        raise ValueError("服务器端口必须在 1..65535 范围内")
    if payload.timezone < -12 or payload.timezone > 14:
        raise ValueError("时区必须在 -12..14 范围内")

    ssid = _validate_config_text(payload.wifi_ssid, "WiFi SSID", 31)
    password = _validate_config_text(payload.wifi_password, "WiFi 密码", 63)
    host = _validate_config_text(payload.server_host, "服务器地址", 63)
    weather_key = _validate_config_text(payload.weather_key, "天气 Key", 47)
    weather_location = _validate_config_text(payload.weather_location, "天气位置", 31)

    return [
        (
            "CFG:"
            f"DEV={payload.device_id}|"
            f"MODE={payload.work_mode}|"
            f"UPLOAD={1 if payload.upload_enable else 0}|"
            f"REPEAT={payload.repeat_interval_sec}|"
            f"TZ={payload.timezone}\n"
        ),
        f"CFG:SSID={ssid}\n",
        f"CFG:PWD={password}\n",
        f"CFG:HOST={host}|PORT={payload.server_port}\n",
        f"CFG:WKEY={weather_key}|WLOC={weather_location}\n",
    ]


def build_image_block(prefix: str, index: int, hex32: str) -> str:
    if prefix not in {"IMGA", "IMGN", "IMGD"}:
        raise ValueError("图像命令前缀必须是 IMGA/IMGN/IMGD")
    max_index = 23 if prefix == "IMGA" else 9
    if index < 0 or index > max_index:
        raise ValueError(f"{prefix} 块索引必须在 0..{max_index} 范围内")
    return f"{prefix}{index:02d}:{validate_hex_block(hex32)}\n"


def build_update_image() -> str:
    return "UPDATEIMG\n"


def chunk_commands(prefix: str, blocks: Iterable[bytes]) -> list[str]:
    commands: list[str] = []
    for index, block in enumerate(blocks):
        if len(block) != 16:
            raise ValueError("每个图像块必须正好 16 字节")
        commands.append(build_image_block(prefix, index, block.hex().upper()))
    return commands


def is_success_response(line: str) -> bool:
    value = line.strip().upper()
    return value == "OK" or value.startswith("OK:")


def is_error_response(line: str) -> bool:
    return line.strip().upper().startswith("ERR:")
