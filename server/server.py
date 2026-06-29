from __future__ import annotations

import argparse
import socketserver
from datetime import datetime
from pathlib import Path


DATA_DIR = Path(__file__).resolve().parent / "data"
LOG_FILE = DATA_DIR / "uploads.log"


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
            self.wfile.write((response + "\n").encode("ascii"))
            self.wfile.flush()

    def handle_line(self, peer: str, line: str) -> str:
        DATA_DIR.mkdir(exist_ok=True)
        now = datetime.now().isoformat(timespec="seconds")
        with LOG_FILE.open("a", encoding="utf-8") as file:
            file.write(f"{now}\t{peer}\t{line}\n")

        if line.startswith("HEARTBEAT:"):
            return "ACK:HEARTBEAT"
        if line.startswith("UPLOAD:"):
            seq = "UNKNOWN"
            for part in line.split("|"):
                if "SEQ=" in part:
                    seq = part.split("SEQ=", 1)[1].split("|", 1)[0]
                    break
            return f"ACK:UPLOAD:{seq}"
        return "ERR:UNKNOWN"


class ThreadingTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> None:
    parser = argparse.ArgumentParser(description="NFC attendance stage-3 TCP test server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=9000)
    args = parser.parse_args()

    with ThreadingTCPServer((args.host, args.port), AttendanceHandler) as server:
        print(f"listening on {args.host}:{args.port}")
        server.serve_forever()


if __name__ == "__main__":
    main()
