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
        commit_tree = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        commit_hash = "unknown"
        commit_tree = "unknown"

    config.extra["commit_hash"] = commit_hash
    config.extra["commit_tree_url"] = f"{config.repo_url}/tree/{commit_tree}"
    return config
