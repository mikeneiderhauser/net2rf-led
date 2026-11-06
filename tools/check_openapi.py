#!/usr/bin/env python3
"""Check docs/reference/openapi.yaml against the routes the firmware serves (src/web.cpp).

    python3 tools/check_openapi.py

Every route must be in the spec with the same method, and every operation in the spec must be a route. An
operation needs `security: [{basicAuth: []}]` exactly when its route is wrapped in protect() (or is the firmware
upload, which checks credentials itself). Standard library only, so it runs anywhere the tests do.
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SELF_AUTH = {("POST", "/update")}  # checks credentials in its own handler


def firmware_routes():
    src = open(os.path.join(ROOT, "src", "web.cpp"), encoding="utf-8").read()
    routes = {}
    for m in re.finditer(r's_server\.on\("([^"]+)",\s*HTTP_([A-Z]+),\s*(\w*)', src):
        key = (m.group(2), m.group(1))
        routes[key] = m.group(3) == "protect" or key in SELF_AUTH
    return routes


def spec_operations():
    lines = open(os.path.join(ROOT, "docs", "reference", "openapi.yaml"), encoding="utf-8").read().splitlines()
    ops = {}
    path = op = None
    in_paths = False
    for line in lines:
        if not line.strip():
            continue
        if not line.startswith(" "):
            in_paths = line.startswith("paths:")
            continue
        if not in_paths:
            continue
        m = re.match(r"  (/\S*):\s*$", line)
        if m:
            path, op = m.group(1), None
            continue
        m = re.match(r"    (get|post|put|patch|delete):\s*$", line)
        if m and path:
            op = (m.group(1).upper(), path)
            ops[op] = False
            continue
        if op and re.match(r"      security:\s*\[\{basicAuth", line):
            ops[op] = True
    return ops


def main():
    routes, ops = firmware_routes(), spec_operations()
    errors = []
    for key in sorted(set(routes) - set(ops)):
        errors.append("not in the spec: %s %s" % key)
    for key in sorted(set(ops) - set(routes)):
        errors.append("not served by the firmware: %s %s" % key)
    for key in sorted(set(routes) & set(ops)):
        if routes[key] != ops[key]:
            errors.append("%s %s: the firmware %s a password, the spec says %s" % (
                key[0], key[1], "asks for" if routes[key] else "doesn't ask for",
                "it does" if ops[key] else "it doesn't"))
    for e in errors:
        print(e)
    print("%d routes, %d operations: %s" % (len(routes), len(ops), "FAILED" if errors else "OK"))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
