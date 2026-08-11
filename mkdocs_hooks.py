from __future__ import annotations

import subprocess


def on_config(config):
    try:
        commit_hash = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        commit_hash = "unknown"

    config.extra["commit_hash"] = commit_hash
    return config
