# Stage-3 Test Server

Minimal TCP server for ESP01S transparent-mode upload tests.

It accepts line-based messages:

```text
HEARTBEAT:DEV=1
UPLOAD:SEQ=1|UID=A1B2C3D4|SID=1001|TYPE=2|TS=1782691200|DEV=1
```

Run:

```powershell
cd server
python server.py --host 0.0.0.0 --port 9000
```

The server returns:

```text
ACK:HEARTBEAT
ACK:UPLOAD:<seq>
ERR:UNKNOWN
```
