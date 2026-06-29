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
    return line.strip().upper() == "OK"


def is_error_response(line: str) -> bool:
    return line.strip().upper().startswith("ERR:")
