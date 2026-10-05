#!/usr/bin/env python3
"""Mock of the controller's HTTP API for developing web/index.html without hardware.

    python3 tools/mock_server.py [port]      # then open http://127.0.0.1:8765/

Serves web/index.html live (edit + refresh) and fakes /api/*. POSTs are logged to /mock/log.
"""
import json
import os
import random
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
T0 = time.time()
LOG = []

# MOCK_UPDATE=1 pretends a newer release exists (dashboard notice, Install button).
FW = "0.0.4"
LATEST = "v0.0.5" if os.environ.get("MOCK_UPDATE") else "v" + FW
GH_JOB = {"tag": "", "started": 0.0}
NETCHECK = {"started": 0.0}


def gh_job():
    """Fake a 6 s download, then report done for a while."""
    if not GH_JOB["started"]:
        return {"state": "idle"}
    t = time.time() - GH_JOB["started"]
    if t > 20:
        GH_JOB["started"] = 0.0
        return {"state": "idle"}
    return {"state": "downloading" if t < 6 else "done", "tag": GH_JOB["tag"], "progress": min(100, int(t / 6 * 100))}


APP = {
    "name": "Front Yard",
    "output_enabled": True,
    "bracelets": {"protocol": 0, "mode": "pixel", "color_order": "RGB", "base_layer": True},
    "radio": {"type": "cc1101", "tx_power": 10, "freq_p0": 433889000, "freq_p1": 433920000, "repeats": 3,
              "off_threshold": 16, "refresh_ms": 0, "tx_jitter_ms": 0,
              "lbt_enabled": True, "lbt_threshold_dbm": -75, "power": True},
    "update": {"repo": "mikeneiderhauser/net2rf-led", "auto_check": True, "check_hours": 12},
    "input": {"ddp_enabled": True, "ddp_port": 4048, "e131_enabled": False, "e131_universe": 1,
              "e131_multicast": True, "start_channel": 1, "timeout_s": 300},
    "zones": [  # the firmware's defaults: every group, then groups 1-4
        {"enabled": True, "name": "All Zones", "addr": "00FFFF0F", "start": 1},
        {"enabled": True, "name": "Zone 1", "addr": "0002000F", "start": 4},
        {"enabled": True, "name": "Zone 2", "addr": "0004000F", "start": 7},
        {"enabled": True, "name": "Zone 3", "addr": "0008000F", "start": 10},
        {"enabled": True, "name": "Zone 4", "addr": "0010000F", "start": 13},
    ],
}
NET = {"hostname": "net2rf-3f2a", "eth_enabled": True, "wifi_ssid": "", "wifi_pass_set": False, "dhcp": True,
       "ip": "", "gateway": "", "subnet": "255.255.255.0", "dns": "", "ap_mode": "no_connection"}
TEST = {"mode": "off", "rgb": "FFFFFF"}
AUTH = {"enabled": False}
STATS = {"packets": 0, "frames": 0}


def p1_packet(addr, rgb):
    """Protocol 1 packet for a zone address (group + byte 6) and colour, as the firmware builds it."""
    lvl = [((15 - (15 * int(rgb[k:k + 2], 16) + 127) // 255) << 4) | 0x0F for k in (0, 2, 4)]
    b = [0x55, int(addr[0:2], 16), *lvl, lvl[0] ^ lvl[1] ^ lvl[2] ^ 0x5A, int(addr[2:4], 16)]
    return "".join(f"{x:02X}" for x in b)


P0_CODES = {"FF0000": "0100", "00FF00": "0101", "0000FF": "0102", "FFFFFF": "0104", "000000": "00AA"}


def p0_packet(addr, rgb):
    """Protocol 0 packet: address, command for one of the palette colours (or off), checksum."""
    b = bytes.fromhex(addr + P0_CODES.get(rgb, "0104"))
    return (b + bytes([sum(b) & 0xFF])).hex().upper()


def p0_scene():
    """A base-layer scene: All Zones red, Zone 2 blue and Zone 3 green on top, the others following."""
    own = ["FF0000", "000000", "0000FF", "00FF00", "000000"]
    zs = APP["zones"]
    rgbs = [own[i] if i < len(own) else "000000" for i in range(len(zs))]
    if not APP["bracelets"].get("base_layer") or not zs or zs[0]["addr"][2:6] != "FFFF":
        return [(rgb, p0_packet(z["addr"], rgb)) for z, rgb in zip(zs, rgbs)]
    mask = 0xFFFF
    out = []
    for z, rgb in list(zip(zs, rgbs))[1:]:
        if rgb not in ("000000", rgbs[0]):
            mask &= ~int(z["addr"][2:6], 16)
            out.append((rgb, p0_packet(z["addr"], rgb)))
        else:
            out.append((rgb, p0_packet(z["addr"], rgbs[0])))  # follows the base
    return [(rgbs[0], p0_packet(f"00{mask:04X}0F", rgbs[0]))] + out


def status():
    up = time.time() - T0
    STATS["packets"] += 40
    STATS["frames"] += 40
    colors = ["FF0000", "00FF00", "0000FF", "FFFFFF"]
    scene = p0_scene() if APP["bracelets"]["protocol"] == 0 else None
    zones = []
    for i, _ in enumerate(APP["zones"]):
        rgb = scene[i][0] if scene else colors[(int(up / 2) + i) % 4]
        z = {"rgb": rgb, "fx": 0, "packet": scene[i][1] if scene else p1_packet(APP["zones"][i]["addr"], rgb),
             "tx": 120 + i, "tx_age_ms": random.randint(50, 900)}
        if APP["bracelets"]["mode"] == "vendor":
            z.update(gated=(i == 1), group=i)
        zones.append(z)
    return {
        "device": {"firmware": FW, "built": "Sep 28 2026 09:00:00", "uptime_s": int(up) + 3600,
                   "free_heap": 182344, "min_free_heap": 160112, "heap_bytes": 327680, "firmware_bytes": 1471297, "firmware_slot_bytes": 1966080, "chip": "ESP32-D0WD-V3", "chip_rev": 3,
                   "reset_reason": "power on", "display": True, "suffix": "3F2A", "name": APP["name"],
                   "auth": AUTH["enabled"], "update_pending": False, "update_rolled_back": False, "update_job": gh_job(),
                   "update_check": {"auto_check": APP["update"]["auto_check"], "check_hours": APP["update"]["check_hours"],
                                    "checking": False, "latest": LATEST, "available": LATEST != "v" + FW, "checked_ago_s": 840},
                   "display_info": {"present": False, "type": "ssd1306", "sda_pin": 5, "scl_pin": 17, "asleep": False, "sleep_min": 10, "button": False}},
        "network": {"interface": "ethernet", "ip": "192.168.250.60", "hostname": NET["hostname"], "dhcp": NET["dhcp"],
                    "ethernet": {"enabled": True, "link": True, "mac": "A8:03:2A:11:3F:2A", "speed": 100,
                                 "full_duplex": True, "ip": "192.168.250.60", "gateway": "192.168.250.1",
                                 "subnet": "255.255.255.0"},
                    "wifi": {"ssid": NET["wifi_ssid"], "active": False, "connected": False, "mac": "A8:03:2A:11:3F:28"},
                    "ap": {"active": False}},
        "engine": {
            "radio": {"type": APP["radio"]["type"], "name": {"cc1101": "CC1101", "sx1278": "SX1278"}[APP["radio"]["type"]],
                      "state": "ready" if APP["radio"]["power"] else "off", "power": APP["radio"]["power"], "detail": "version 0x14",
                      "min_power": -30, "max_power": 10, "queue": 0,
                      "lbt": {"enabled": APP["radio"]["lbt_enabled"], "supported": True,
                              "threshold_dbm": APP["radio"]["lbt_threshold_dbm"], "last_rssi_dbm": -97, "last_busy": False}},
            "input": {"packets": STATS["packets"], "ddp_packets": STATS["packets"], "e131_packets": 0,
                      "last_source": "ddp", "frames": STATS["frames"], "malformed": 0, "ignored": 2,
                      "bytes": STATS["packets"] * 16, "fps": 40.0, "seen": True, "timed_out": False, "age_ms": 25,
                      "source": "192.168.250.20", "timeout_s": APP["input"]["timeout_s"]},
            "output": {"enabled": APP["output_enabled"], "suppressed": 0 if APP["output_enabled"] else 57,
                       "updates": 812, "frames": 2436, "errors": 0, "manual": 4, "airtime_pct": 38.5,
                       "lbt_checks": 812, "lbt_waits": 9, "lbt_forced": 0, "lbt_wait_ms": 61},
            "test": TEST,
            "zones": zones,
        },
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            with open(os.path.join(ROOT, "web", "index.html"), "rb") as f:
                return self.send(200, f.read(), "text/html")
        if path == "/api/status":
            return self.send(200, status())
        if path == "/api/config":
            return self.send(200, {"app": APP, "network": NET})
        if path == "/api/wifi/scan":
            return self.send(200, {"running": False, "networks": [{"ssid": "HomeNet", "rssi": -52, "secure": True},
                                                                   {"ssid": "Guest", "rssi": -71, "secure": False}]})
        if path == "/api/export":
            return self.send(200, {"format": "net2rf-led", "app": APP, "network": NET})
        if path == "/api/discover":
            def st(radio="ready", inp="live", out=True, test=False, zones=4, up=86400):
                return {"radio": radio, "input": inp, "output_enabled": out, "test": test, "zones": zones,
                        "uptime_s": up}
            return self.send(200, {"running": False, "alias": "net2rf.local", "alias_claimed": True, "controllers": [
                {"name": APP["name"], "hostname": NET["hostname"], "ip": "192.168.250.60", "firmware": FW,
                 "id": "3F2A", "self": True, "online": True, "last_seen_ms": 0, "via": ["self"], "state": st()},
                {"name": "Back Yard", "hostname": "net2rf-81c4", "ip": "192.168.250.61", "firmware": FW,
                 "id": "81C4", "self": False, "online": True, "last_seen_ms": 1800, "via": ["udp", "mdns"],
                 "state": st(inp="idle", out=False)},
                {"name": "Porch", "hostname": "net2rf-90aa", "ip": "192.168.250.62", "firmware": FW,
                 "id": "90AA", "self": False, "online": True, "last_seen_ms": 4200, "via": ["udp"],
                 "state": st(radio="not_detected", inp="none", test=True, zones=1)},
                {"name": "Garage", "hostname": "net2rf-a1b2", "ip": "192.168.250.63", "firmware": FW,
                 "id": "A1B2", "self": False, "online": False, "last_seen_ms": 185000, "via": ["udp", "mdns"],
                 "state": st(inp="timed_out")},
                {"name": "Old Firmware", "hostname": "net2rf-c3d4", "ip": "192.168.250.64", "firmware": "0.0.2",
                 "id": "C3D4", "self": False, "online": False, "last_seen_ms": 30000, "via": ["mdns"]}]})
        if path == "/api/tools":
            scene = p0_scene() if APP["bracelets"]["protocol"] == 0 else []
            tx = [{"age_ms": 400 + 2100 * k, "p": 0, "pkt": pkt, "n": 3, "manual": k == 3, "ok": True}
                  for k, pkt in enumerate([s[1] for s in scene] + ["00FFFF0F00AAB7", "000C000F06AAC9"])]
            ch = [int(rgb[i:i + 2], 16) for rgb, _ in scene for i in (0, 2, 4)]
            return self.send(200, {"tx_total": 812, "tx": tx, "input": {"start": APP["input"]["start_channel"], "width": 3,
                                                                       "seen": True, "age_ms": 25, "channels": ch}})
        if path == "/api/rssi":
            noise = random.randint(-90, -84)
            return self.send(200, {"peak_dbm": noise + random.choice([0, 1, 2, 30]), "avg_dbm": noise, "freq_hz": 433889000})
        if path == "/api/net/check":
            running = time.time() - NETCHECK["started"] < 2.5
            out = {"host": "github.com", "running": running}
            if not running and NETCHECK["started"]:
                out.update(done_ago_s=int(time.time() - NETCHECK["started"] - 2.5), dns={"ok": True, "ms": 41, "ip": "140.82.112.4"},
                           tcp={"ok": True, "ms": 38}, https={"ok": True, "ms": 1320, "status": 200})
            return self.send(200, out)
        if path == "/api/flash":
            return self.send(200, {"flash_bytes": 4194304, "partitions": [
                {"label": "nvs", "offset": 0x9000, "bytes": 0x5000, "kind": "settings", "used_bytes": 5120},
                {"label": "otadata", "offset": 0xE000, "bytes": 0x2000, "kind": "boot_select"},
                {"label": "app0", "offset": 0x10000, "bytes": 0x1E0000, "kind": "firmware_running", "used_bytes": 1471297},
                {"label": "app1", "offset": 0x1F0000, "bytes": 0x1E0000, "kind": "firmware_next"},
                {"label": "spiffs", "offset": 0x3D0000, "bytes": 0x20000, "kind": "files"},
                {"label": "coredump", "offset": 0x3F0000, "bytes": 0x10000, "kind": "crash_dump"}]})
        if path == "/api/i2c/scan":
            return self.send(200, {"bus_ok": True, "devices": [], "sda_high": True, "scl_high": True, "orientation": "normal (SDA=IO5, SCL=IO17)"})
        if path == "/mock/log":
            return self.send(200, LOG)
        self.send(404, {"ok": False, "error": "not found"})

    def do_POST(self):
        path = self.path.split("?")[0]
        raw = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if path == "/update":
            LOG.append([path, len(raw)])
            return self.send(200, {"ok": True, "reboot": True})
        body = json.loads(raw or b"{}")
        LOG.append([path, body])
        reboot = False
        if path == "/api/net/check":
            NETCHECK["started"] = time.time()
            return self.send(200, {"ok": True, "reboot": False})
        if path == "/api/identify":
            return self.send(200, {"ok": True, "reboot": False})
        if path == "/api/update/check":
            return self.send(200, {"ok": True, "reboot": False})
        if path == "/api/update/github":
            GH_JOB.update(tag=body.get("tag", ""), started=time.time())
            return self.send(200, {"ok": True, "reboot": False})
        if path == "/api/auth":
            AUTH["enabled"] = bool(body.get("password"))
            return self.send(200, {"ok": True, "auth": AUTH["enabled"]})
        if path == "/api/output":
            APP["output_enabled"] = bool(body.get("enabled"))
            return self.send(200, {"ok": True, "enabled": APP["output_enabled"]})
        if path == "/api/radio":
            APP["radio"]["power"] = bool(body.get("power"))
            return self.send(200, {"ok": True, "power": APP["radio"]["power"]})
        if path == "/api/all-off":
            TEST.update(mode="off")
        if path in ("/api/send", "/api/raw", "/api/all-off") and not APP["radio"]["power"]:
            return self.send(409, {"ok": False, "error": "radio is shut down"})
        if path in ("/api/send", "/api/raw", "/api/all-off") and not APP["output_enabled"]:
            return self.send(409, {"ok": False, "error": "RF output is disabled"})
        if path == "/api/config":
            if "name" in body:
                APP["name"] = body["name"]
            for key in ("bracelets", "radio", "input", "update"):
                if key in body:
                    if key == "radio" and body[key].get("type") != APP["radio"]["type"]:
                        reboot = True
                    APP[key].update(body[key])
            if "zones" in body:
                w = 4 if APP["bracelets"]["mode"] == "dmx" else 3
                APP["zones"] = [dict(z, start=APP["input"]["start_channel"] + k * w) for k, z in enumerate(body["zones"])]
        elif path == "/api/network":
            NET.update({k: v for k, v in body.items() if k not in ("wifi_pass", "ap_pass")})
            reboot = True
        elif path == "/api/test":
            TEST.update(body)
        elif path in ("/api/reboot", "/api/factory-reset", "/api/import"):
            reboot = True
        self.send(200, {"ok": True, "reboot": reboot})


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    print(f"Mock controller on http://127.0.0.1:{port}/")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
