# Pre-build: set FW_VERSION from $FW_VERSION (CI), else `git describe`, else a dev fallback.
import os
import subprocess

Import("env")  # noqa: F821

version = os.environ.get("FW_VERSION", "").strip()
if not version:
    try:
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"), stderr=subprocess.DEVNULL).decode().strip()  # noqa: F821
    except Exception:
        version = ""
version = version.lstrip("v") or "0.0.0-dev"
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])  # noqa: F821
print(f"Firmware version: {version}")
