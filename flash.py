#!/usr/bin/env python3
import argparse
import os
import sys
import subprocess
from pathlib import Path

MAIN_BIN_REL = Path("build") / "vigilant-engine.bin"
RECOVERY_BIN_REL = Path("vigilant-engine-recovery") / "build" / "vigilant-engine-recovery.bin"

# Debug output to verify paths
# print(f"MAIN_BIN_REL: {MAIN_BIN_REL}")
# print(f"RECOVERY_BIN_REL: {RECOVERY_BIN_REL}")

PART_MAIN = "ota_0"
PART_RECOVERY = "factory"


def run(cmd, cwd=None):
    print(">>", " ".join(map(str, cmd)))
    subprocess.run(cmd, cwd=cwd, check=True)


def require_idf_path() -> Path:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise RuntimeError(
            "IDF_PATH ist nicht gesetzt. Starte die ESP-IDF Shell/PowerShell oder führe export.bat/export.sh aus."
        )
    return Path(idf_path)


def find_parttool() -> Path:
    idf = require_idf_path()
    parttool = idf / "components" / "partition_table" / "parttool.py"
    if not parttool.exists():
        raise FileNotFoundError(f"parttool.py nicht gefunden: {parttool}")
    return parttool


def find_idfpy() -> Path:
    idf = require_idf_path()
    idfpy = idf / "tools" / "idf.py"
    if not idfpy.exists():
        raise FileNotFoundError(f"idf.py nicht gefunden: {idfpy}")
    return idfpy


def idf_build(repo_root: Path, project_dir: Path | None = None):
    idfpy = find_idfpy()
    cmd = [sys.executable, str(idfpy)]
    if project_dir is not None:
        # works on Windows + Linux, avoids relying on file associations
        cmd += ["-C", str(project_dir)]
    #reconfigure builds sdkconfig
    cmd += ["reconfigure", "build"]
    run(cmd, cwd=repo_root)


def write_partition(port: str, baud: int, part_name: str, bin_path: Path):
    if not bin_path.exists():
        raise FileNotFoundError(f"BIN nicht gefunden: {bin_path}")

    parttool = find_parttool()
    run([
        sys.executable, str(parttool),
        "--port", port,
        "--baud", str(baud),
        "write_partition",
        "--partition-name", part_name,
        "--input", str(bin_path),
    ])


def main():
    parser = argparse.ArgumentParser(
        description="Cross-platform flash helper for Vigilant Engine (ESP-IDF)."
    )
    parser.add_argument("target", choices=["main", "recovery"], help="What to flash")
    parser.add_argument("--port", required=True, help="Serial port (e.g. COM7 or /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default: 921600)")
    parser.add_argument("--no-build", action="store_true", help="Skip idf.py build step")

    args = parser.parse_args()
    repo_root = Path(__file__).resolve().parent

    if args.target == "main":
        if not args.no_build:
            idf_build(repo_root)
        write_partition(args.port, args.baud, PART_MAIN, repo_root / MAIN_BIN_REL)

    else:  # recovery
        if not args.no_build:
            idf_build(repo_root, project_dir=Path("vigilant-engine-recovery"))
        write_partition(args.port, args.baud, PART_RECOVERY, repo_root / RECOVERY_BIN_REL)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        print(f"\n❌ Command failed with exit code {e.returncode}", file=sys.stderr)
        sys.exit(e.returncode)
    except Exception as e:
        print(f"\n❌ {e}", file=sys.stderr)
        sys.exit(1)
