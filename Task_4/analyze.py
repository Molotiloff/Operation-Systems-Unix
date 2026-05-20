#!/usr/bin/env python3
import glob
import re
import sys
from pathlib import Path

def parse_server_log(path: Path):
    first_req = None
    last_resp = None
    first_accept = None
    last_accept = None

    accept_re = re.compile(r"^ACCEPT\s+([0-9]+\.[0-9]+)\s+fd=(\d+)")
    req_re = re.compile(r"^REQ\s+([0-9]+\.[0-9]+)\s+fd=(\d+)")
    resp_re = re.compile(r"^RESP\s+([0-9]+\.[0-9]+)\s+fd=(\d+)")

    with path.open() as f:
        for line in f:
            line = line.strip()

            m = accept_re.match(line)
            if m:
                rec = (float(m.group(1)), m.group(2))
                if first_accept is None:
                    first_accept = rec
                last_accept = rec
                continue

            m = req_re.match(line)
            if m:
                ts = float(m.group(1))
                if first_req is None:
                    first_req = ts
                continue

            m = resp_re.match(line)
            if m:
                ts = float(m.group(1))
                last_resp = ts
                continue

    return first_req, last_resp, first_accept, last_accept

def parse_client_delays(pattern: str):
    delays = []
    delay_re = re.compile(r"total_delay=([0-9]+\.[0-9]+)")

    for name in glob.glob(pattern):
        with open(name) as f:
            text = f.read().strip()

        m = delay_re.search(text)
        if m:
            delays.append(float(m.group(1)))

    return delays

def main():
    if len(sys.argv) != 3:
        print("Usage: analyze.py <server_log> <client_delay_glob>", file=sys.stderr)
        sys.exit(1)

    server_log = Path(sys.argv[1])
    delay_glob = sys.argv[2]

    first_req, last_resp, first_accept, last_accept = parse_server_log(server_log)
    delays = parse_client_delays(delay_glob)

    if first_req is None or last_resp is None:
        print("effective_time=NA")
        sys.exit(0)

    raw_duration = last_resp - first_req
    max_delay = max(delays) if delays else 0.0
    effective = raw_duration - max_delay

    print(f"first_request={first_req:.6f}")
    print(f"last_response={last_resp:.6f}")
    print(f"raw_duration={raw_duration:.6f}")
    print(f"max_client_delay={max_delay:.6f}")
    print(f"effective_time={effective:.6f}")

    if first_accept is not None and last_accept is not None:
        print(f"first_accept=ts:{first_accept[0]:.6f} fd:{first_accept[1]}")
        print(f"last_accept=ts:{last_accept[0]:.6f} fd:{last_accept[1]}")

if __name__ == "__main__":
    main()
    