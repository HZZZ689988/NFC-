from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


RUNNER_PATH = Path(__file__).with_name("run_host_tests.py")


def load_runner():
    spec = importlib.util.spec_from_file_location("run_host_tests", RUNNER_PATH)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules["run_host_tests"] = module
    spec.loader.exec_module(module)
    return module


def test_msvc_command_writes_objects_to_temp_dir() -> None:
    runner = load_runner()
    toolchain = runner.Toolchain("cl", "cl", "msvc")
    test = runner.TESTS[0]
    temp_dir = Path("C:/Temp/nfc-host-tests")
    command = runner.compile_cmd(
        toolchain,
        test,
        temp_dir / "att_display_stub.c",
        temp_dir / "test_att_protocol_host.exe",
    )

    assert any(part.startswith("/Fo") and str(temp_dir) in part for part in command)


def main() -> int:
    test_msvc_command_writes_objects_to_temp_dir()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
