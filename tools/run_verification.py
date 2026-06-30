from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_DIR = ROOT / "firmware" / "stm32" / "NFCAttend_Base"
ELF_PATH = FIRMWARE_DIR / "build" / "Demo_W25Q128.elf"


def run(cmd: list[str], cwd: Path = ROOT) -> str:
    print("+ " + " ".join(cmd))
    completed = subprocess.run(
        cmd,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if completed.stdout:
        print(completed.stdout, end="" if completed.stdout.endswith("\n") else "\n")
    return completed.stdout


def verify_elf_permissions() -> None:
    output = run(["arm-none-eabi-readelf", "-l", str(ELF_PATH)], cwd=FIRMWARE_DIR)
    load_lines = [line for line in output.splitlines() if line.strip().startswith("LOAD")]
    if not load_lines:
        raise RuntimeError("readelf output did not include LOAD program headers")

    joined = "\n".join(load_lines)
    if re.search(r"\bRWE\b|\bRWX\b", joined):
        raise RuntimeError("ELF has a writable and executable LOAD segment")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run NFC attendance host verification.")
    parser.add_argument(
        "--firmware-build",
        action="store_true",
        help="also run STM32 make clean/build and ELF segment permission check",
    )
    args = parser.parse_args()

    try:
        run([sys.executable, "-m", "compileall", "pc_tool", "server"])
        run([sys.executable, "pc_tool/tests/test_core.py"])
        run([sys.executable, "firmware/app/tests/test_run_host_tests_py.py"])
        run([sys.executable, "firmware/app/tests/run_host_tests.py"])
        run(["git", "diff", "--check"])

        if args.firmware_build:
            run(["make", "clean"], cwd=FIRMWARE_DIR)
            run(["make"], cwd=FIRMWARE_DIR)
            verify_elf_permissions()
    except (subprocess.CalledProcessError, RuntimeError) as exc:
        print(f"Verification failed: {exc}", file=sys.stderr)
        return 1

    print("Verification passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
