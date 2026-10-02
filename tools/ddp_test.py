#!/usr/bin/env python3
"""Send DDP frames to the Net2RF LED controller without xLights.

Examples:
  ddp_test.py 192.168.250.60 color ff0000            # group at channel 1 -> red
  ddp_test.py 192.168.250.60 color 00ff00 --start 5  # group starting at channel 5 -> green
  ddp_test.py 192.168.250.60 fx 50 --dmx             # DMX group: FX channel = 50 (protocol 0 built-in A)
  ddp_test.py 192.168.250.60 cycle --rate 2          # cycle red/green/blue/white/off at 2 Hz
"""
import argparse
import socket
import time

DDP_PORT = 4048


def ddp_packet(offset: int, data: bytes, seq: int) -> bytes:
    # flags: version 1 + PUSH, sequence, data type (RGB 8-bit), destination id 1 (display)
    header = bytes([0x41, seq & 0x0F, 0x0B, 0x01])
    header += offset.to_bytes(4, "big") + len(data).to_bytes(2, "big")
    return header + data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("cmd", choices=["color", "fx", "off", "cycle"])
    ap.add_argument("value", nargs="?", default="")
    ap.add_argument("--start", type=int, default=1, help="group start channel (1-based)")
    ap.add_argument("--port", type=int, default=DDP_PORT)
    ap.add_argument("--rate", type=float, default=1.0, help="cycle rate in Hz")
    ap.add_argument("--dmx", action="store_true", help="target a DMX group (R,G,B,FX) instead of a pixel (R,G,B)")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    offset = args.start - 1

    def send(rgb: bytes, fx: int = 0, seq: int = 1):
        data = rgb + bytes([fx]) if args.dmx else rgb
        sock.sendto(ddp_packet(offset, data, seq), (args.host, args.port))

    if args.cmd == "color":
        send(bytes.fromhex(args.value or "ff0000"))
    elif args.cmd == "fx":
        if not args.dmx:
            ap.error("fx needs --dmx (pixel groups have no FX channel)")
        send(b"\x00\x00\x00", int(args.value or "50"))
    elif args.cmd == "off":
        send(b"\x00\x00\x00")
    else:
        colors = ["ff0000", "00ff00", "0000ff", "ffffff", "000000"]
        seq = 0
        try:
            while True:
                for c in colors:
                    seq += 1
                    send(bytes.fromhex(c), seq=seq)
                    print(c)
                    time.sleep(1.0 / args.rate)
        except KeyboardInterrupt:
            send(b"\x00\x00\x00")


if __name__ == "__main__":
    main()
