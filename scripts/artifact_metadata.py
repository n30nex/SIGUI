#!/usr/bin/env python3
"""Small helpers for stamping validation artifacts with source metadata."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Any


def git_value(root: Path, *args: str) -> str | None:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None
    return result.stdout.strip() or None


def git_metadata(root: Path) -> dict[str, Any]:
    status = git_value(root, "status", "--porcelain") or ""
    dirty_entries = [line for line in status.splitlines() if line.strip()]
    return {
        "commit": git_value(root, "rev-parse", "HEAD"),
        "short_commit": git_value(root, "rev-parse", "--short", "HEAD"),
        "branch": git_value(root, "branch", "--show-current"),
        "dirty": bool(dirty_entries),
        "dirty_entries": dirty_entries,
    }


def stamp_report(report: dict, root: Path) -> dict:
    git = git_metadata(root)
    if git.get("commit"):
        report.setdefault("commit", git["commit"])
    report.setdefault("git", git)
    return report
