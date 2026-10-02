#!/usr/bin/env python3
"""Simulate other Net2RF LED controllers on the network (UDP heartbeat, port 4049).

Sends heartbeats for a few made-up controllers every 5 s and prints every heartbeat it receives. Use it to
check the Dashboard's controller list without owning several controllers.

  python3 tools/peer_sim.py                      # broadcast on the local subnet
  python3 tools/peer_sim.py --to 192.168.1.50    # unicast to one controller (e.g. on another subnet)
  python3 tools/peer_sim.py --to 192.168.1.50 --drop-after 30   # "Porch" stops beating after 30 s
  python3 tools/peer_sim.py --listen             # only print what arrives

The first beat says "hello", so real controllers answer straight away (directly to us as well, which works
across subnets). Ctrl-C to stop.
"""
import argparse
import json
import socket
import time

PORT = 4049

PEERS = [
    {"id": "0A01", "name": "Sim Back Yard", "host": "net2rf-0a01", "radio": "ready", "input": "live", "out": True,
     "test": False, "zones": 4},
    {"id": "FF02", "name": "Sim Porch", "host": "net2rf-ff02", "radio": "not_detected", "input": "none",
     "out": True, "test": True, "zones": 1},
    {"id": "FF03", "name": "Sim Garage", "host": "net2rf-ff03", "radio": "ready", "input": "idle", "out": False,
     "test": False, "zones": 2},
]


def beat(p, start, hello):
    msg = {"p": "net2rf", "v": 1, "id": p["id"], "name": p["name"], "host": p["host"], "fw": "sim",
           "radio": p["radio"], "input": p["input"], "out": p["out"], "test": p["test"], "zones": p["zones"],
           "up": int(time.time() - start)}
    if hello:
        msg["hello"] = True
    return json.dumps(msg, separators=(",", ":")).encode()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--to", help="unicast to this controller IP instead of broadcasting")
    ap.add_argument("--drop-after", type=float, default=0,
                    help="'Sim Porch' stops sending after this many seconds (watch it go offline after ~15 s)")
    ap.add_argument("--no-low-id", action="store_true",
                    help="leave out 'Sim Back Yard' (ID 0A01), so the real controller keeps the net2rf.local alias")
    ap.add_argument("--listen", action="store_true", help="don't send, only print received heartbeats")
    ap.add_argument("--interval", type=float, default=5.0)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("", PORT))
    sock.settimeout(0.2)
    dest = (args.to or "255.255.255.255", PORT)
    peers = [p for p in PEERS if not (args.no_low_id and p["id"] == "0A01")]
    sim_ids = {p["id"] for p in peers}

    start = time.time()
    next_beat = 0.0
    first = True
    print(f"{'listening' if args.listen else 'simulating ' + ', '.join(p['name'] for p in peers)} on UDP {PORT}"
          f"{'' if args.listen else ' -> ' + dest[0]}")
    try:
        while True:
            now = time.time()
            if not args.listen and now >= next_beat:
                for p in peers:
                    if args.drop_after and p["id"] == "FF02" and now - start > args.drop_after:
                        if first or int(now - start) % 30 < args.interval:
                            print(f"[{now - start:5.0f}s] {p['name']} is now silent")
                        continue
                    sock.sendto(beat(p, start, first), dest)
                first = False
                next_beat = now + args.interval
            try:
                data, addr = sock.recvfrom(2048)
            except socket.timeout:
                continue
            try:
                msg = json.loads(data)
            except ValueError:
                continue
            if msg.get("p") != "net2rf" or msg.get("id") in sim_ids:
                continue  # not a heartbeat, or our own broadcast
            print(f"[{time.time() - start:5.0f}s] from {addr[0]}: {msg.get('name')} ({msg.get('host')}, id "
                  f"{msg.get('id')}) radio={msg.get('radio')} input={msg.get('input')} out={msg.get('out')} "
                  f"test={msg.get('test')} zones={msg.get('zones')} up={msg.get('up')}s"
                  f"{' HELLO' if msg.get('hello') else ''}")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
