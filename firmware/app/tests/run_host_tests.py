from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
INC_DIR = ROOT / "firmware" / "app" / "Inc"
TEST_DIR = ROOT / "firmware" / "app" / "tests"
SRC_DIR = ROOT / "firmware" / "app" / "Src"
LITTLEFS_DIR = ROOT / "firmware" / "third_party" / "littlefs"


@dataclass(frozen=True)
class Toolchain:
    name: str
    exe: str
    mode: str


@dataclass(frozen=True)
class HostTest:
    name: str
    defines: tuple[str, ...]
    includes: tuple[Path, ...]
    sources: tuple[Path, ...]
    needs_display_stub: bool = False


DISPLAY_STUB = r'''
#include "att_display.h"

att_status_t att_display_init(void) { return ATT_OK; }
void att_display_set_config(const att_device_config_t *config) { (void)config; }
void att_display_set_record_count(uint32_t count) { (void)count; }
void att_display_set_network(att_display_network_state_t state) { (void)state; }
void att_display_set_weather(const char *text) { (void)text; }
void att_display_show_ready(uint32_t now_sec) { (void)now_sec; }
void att_display_show_attendance_ok(uint32_t seq, uint32_t sid, uint32_t now_sec)
{
    (void)seq;
    (void)sid;
    (void)now_sec;
}
void att_display_show_attendance_result(uint32_t seq, uint32_t sid,
                                        att_record_type_t record_type,
                                        uint32_t now_sec,
                                        uint32_t duration_sec,
                                        const char *status_text,
                                        const char *result_text)
{
    (void)seq;
    (void)sid;
    (void)record_type;
    (void)now_sec;
    (void)duration_sec;
    (void)status_text;
    (void)result_text;
}
void att_display_show_attendance_duplicate(uint32_t now_sec) { (void)now_sec; }
void att_display_show_attendance_invalid(uint32_t now_sec) { (void)now_sec; }
void att_display_show_error(const char *reason, uint32_t now_sec)
{
    (void)reason;
    (void)now_sec;
}
void att_display_show_weather(uint32_t now_sec) { (void)now_sec; }
void att_display_show_oled_test(void) {}
void att_display_show_admin(uint32_t device_id, att_work_mode_t mode,
                            uint8_t field, const char *message,
                            uint32_t now_sec)
{
    (void)device_id;
    (void)mode;
    (void)field;
    (void)message;
    (void)now_sec;
}
uint8_t att_display_page_prev(void) { return 0u; }
uint8_t att_display_page_next(void) { return 0u; }
void att_display_poll(uint32_t now_sec) { (void)now_sec; }
'''


TESTS = (
    HostTest(
        name="test_att_protocol_host",
        defines=(),
        includes=(INC_DIR,),
        sources=(
            TEST_DIR / "test_att_protocol_host.c",
            SRC_DIR / "att_protocol.c",
            SRC_DIR / "att_crc16.c",
        ),
    ),
    HostTest(
        name="test_attendance_serial_host",
        defines=(),
        includes=(INC_DIR,),
        sources=(
            TEST_DIR / "test_attendance_serial_host.c",
            SRC_DIR / "attendance_app.c",
            SRC_DIR / "att_protocol.c",
            SRC_DIR / "att_crc16.c",
        ),
        needs_display_stub=True,
    ),
    HostTest(
        name="test_attendance_nfc_host",
        defines=(),
        includes=(INC_DIR,),
        sources=(
            TEST_DIR / "test_attendance_nfc_host.c",
            SRC_DIR / "attendance_app.c",
            SRC_DIR / "att_protocol.c",
            SRC_DIR / "att_crc16.c",
        ),
        needs_display_stub=True,
    ),
    HostTest(
        name="test_att_network_host",
        defines=(),
        includes=(TEST_DIR, INC_DIR),
        sources=(
            TEST_DIR / "test_att_network_host.c",
            SRC_DIR / "att_network.c",
            SRC_DIR / "att_crc16.c",
            SRC_DIR / "att_crc32.c",
        ),
    ),
    HostTest(
        name="test_att_storage_host",
        defines=("ATT_STORAGE_MAX_RECORDS=4",),
        includes=(INC_DIR, LITTLEFS_DIR),
        sources=(
            TEST_DIR / "test_att_storage_host.c",
            SRC_DIR / "att_storage.c",
            SRC_DIR / "att_crc16.c",
            SRC_DIR / "att_crc32.c",
            LITTLEFS_DIR / "lfs.c",
            LITTLEFS_DIR / "lfs_util.c",
        ),
    ),
    HostTest(
        name="test_att_display_host",
        defines=("ATT_ENABLE_DISPLAY=1",),
        includes=(TEST_DIR, INC_DIR),
        sources=(
            TEST_DIR / "test_att_display_host.c",
            SRC_DIR / "att_display.c",
        ),
    ),
    HostTest(
        name="test_attendance_network_host",
        defines=("ATT_ENABLE_NETWORK=1",),
        includes=(INC_DIR,),
        sources=(
            TEST_DIR / "test_attendance_network_host.c",
            SRC_DIR / "attendance_app.c",
            SRC_DIR / "att_protocol.c",
            SRC_DIR / "att_crc16.c",
        ),
        needs_display_stub=True,
    ),
)


def find_toolchain() -> Toolchain:
    env_cc = os.environ.get("CC")
    if env_cc:
        return Toolchain("cc", env_cc, "gcc")

    for name, mode in (
        ("gcc", "gcc"),
        ("clang", "gcc"),
        ("zig", "zig"),
        ("cl", "msvc"),
    ):
        exe = shutil.which(name)
        if exe:
            return Toolchain(name, exe, mode)

    raise RuntimeError("no native C compiler found; install gcc, clang, zig or MSVC cl")


def run(cmd: list[str]) -> None:
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def compile_cmd(toolchain: Toolchain, test: HostTest, display_stub: Path, exe: Path) -> list[str]:
    sources = list(test.sources)
    if test.needs_display_stub:
        sources.append(display_stub)

    if toolchain.mode == "zig":
        return (
            [toolchain.exe, "cc"]
            + [f"-D{define}" for define in test.defines]
            + [f"-I{path}" for path in test.includes]
            + [str(path) for path in sources]
            + ["-o", str(exe)]
        )

    if toolchain.mode == "msvc":
        return (
            [toolchain.exe, "/nologo"]
            + [f"/D{define}" for define in test.defines]
            + [f"/I{path}" for path in test.includes]
            + [str(path) for path in sources]
            + [f"/Fo:{exe.parent}{os.sep}"]
            + [f"/Fe:{exe}"]
        )

    return (
        [toolchain.exe]
        + [f"-D{define}" for define in test.defines]
        + [f"-I{path}" for path in test.includes]
        + [str(path) for path in sources]
        + ["-o", str(exe)]
    )


def main() -> int:
    try:
        toolchain = find_toolchain()
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    print(f"Using native C compiler: {toolchain.name} ({toolchain.exe})")
    with tempfile.TemporaryDirectory(prefix="nfc-host-tests-") as tmp:
        tmp_dir = Path(tmp)
        display_stub = tmp_dir / "att_display_stub.c"
        display_stub.write_text(DISPLAY_STUB, encoding="ascii")

        for test in TESTS:
            exe = tmp_dir / (test.name + (".exe" if os.name == "nt" else ""))
            run(compile_cmd(toolchain, test, display_stub, exe))
            run([str(exe)])
            print(f"{test.name} passed")

    print("Native C host tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
