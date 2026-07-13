#!/usr/bin/env python3
"""Verify the exact Arduino/RP2040 archives used by the release workflow."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
GIT_SHA_RE = re.compile(r"^[0-9a-f]{40}$")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_metadata(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("build-input metadata must be an object")
    if data.get("schema") != 1 or data.get("kind") != "d1l_build_inputs":
        raise ValueError("unsupported build-input metadata")
    return data


def _required_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{label} must be a nonempty string")
    return value


def _git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def validate_metadata(data: dict[str, Any], root: Path) -> dict[str, Any]:
    arduino = data.get("arduino")
    if not isinstance(arduino, dict):
        raise ValueError("arduino build inputs are missing")
    cli = arduino.get("cli")
    rp2040 = arduino.get("rp2040")
    if not isinstance(cli, dict) or not isinstance(rp2040, dict):
        raise ValueError("arduino CLI or RP2040 build inputs are missing")

    cli_version = _required_string(cli.get("version"), "arduino CLI version")
    if not re.fullmatch(r"\d+\.\d+\.\d+", cli_version):
        raise ValueError("arduino CLI version must be exact")
    setup_action = cli.get("setup_action")
    if not isinstance(setup_action, dict):
        raise ValueError("arduino setup action identity is missing")
    action_commit = _required_string(setup_action.get("commit"), "arduino setup action commit")
    if not GIT_SHA_RE.fullmatch(action_commit):
        raise ValueError("arduino setup action commit must be a full Git SHA")

    core_version = _required_string(rp2040.get("version"), "RP2040 core version")
    archive = rp2040.get("platform_archive")
    tools = rp2040.get("tools")
    if not isinstance(archive, dict) or not isinstance(tools, list) or not tools:
        raise ValueError("RP2040 platform archive or tool inventory is missing")

    inventory: list[dict[str, Any]] = []
    seen_names: set[str] = set()
    seen_files: set[str] = set()
    for item in [archive, *tools]:
        if not isinstance(item, dict):
            raise ValueError("Arduino archive inventory entry must be an object")
        filename = _required_string(item.get("filename"), "Arduino archive filename")
        url = _required_string(item.get("url"), f"{filename} URL")
        digest = _required_string(item.get("sha256"), f"{filename} SHA-256")
        size = item.get("size")
        if Path(filename).name != filename:
            raise ValueError(f"Arduino archive filename must be a basename: {filename}")
        parsed_url = urlparse(url)
        if parsed_url.scheme != "https" or Path(parsed_url.path).name != filename:
            raise ValueError(f"Arduino archive URL does not identify {filename}")
        if filename in seen_files:
            raise ValueError(f"duplicate Arduino archive filename: {filename}")
        if not SHA256_RE.fullmatch(digest) or not isinstance(size, int) or size <= 0:
            raise ValueError(f"invalid Arduino archive identity: {filename}")
        seen_files.add(filename)
        inventory.append({"filename": filename, "url": url, "sha256": digest, "size": size})

    for tool in tools:
        name = _required_string(tool.get("name"), "Arduino tool name")
        _required_string(tool.get("version"), f"Arduino tool version for {name}")
        if name in seen_names:
            raise ValueError(f"duplicate Arduino tool: {name}")
        seen_names.add(name)
    if rp2040.get("compiler_tool") not in seen_names:
        raise ValueError("declared Arduino compiler tool is absent from the tool inventory")

    workflow = (root / ".github" / "workflows" / "d1l-ci.yml").read_text(encoding="utf-8")
    required_workflow_tokens = (
        f"arduino/setup-arduino-cli@{action_commit}",
        f'version: "{cli_version}"',
        f"arduino-cli core download rp2040:rp2040@{core_version}",
        f"arduino-cli core install rp2040:rp2040@{core_version}",
        "scripts/verify_arduino_build_inputs.py",
    )
    missing_tokens = [token for token in required_workflow_tokens if token not in workflow]
    if missing_tokens:
        raise ValueError("workflow is missing immutable Arduino inputs: " + ", ".join(missing_tokens))

    submodules = data.get("submodules")
    if not isinstance(submodules, dict) or not submodules:
        raise ValueError("submodule build inputs are missing")
    actual_submodules: dict[str, str] = {}
    for path, expected in sorted(submodules.items()):
        if not isinstance(path, str) or not GIT_SHA_RE.fullmatch(str(expected)):
            raise ValueError(f"invalid submodule build input: {path}")
        line = _git(root, "ls-tree", "HEAD", "--", path)
        fields = line.split()
        if len(fields) < 4 or fields[0] != "160000" or fields[1] != "commit":
            raise ValueError(f"submodule gitlink is missing: {path}")
        actual = fields[2]
        if actual != expected:
            raise ValueError(f"submodule gitlink mismatch for {path}: {actual} != {expected}")
        actual_submodules[path] = actual

    return {
        "cli_version": cli_version,
        "core_version": core_version,
        "archive_inventory": inventory,
        "submodules": actual_submodules,
    }


def verify_archives(data_dir: Path, inventory: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if not data_dir.is_dir():
        raise ValueError(f"Arduino data directory is missing: {data_dir}")
    verified: list[dict[str, Any]] = []
    for expected in inventory:
        filename = expected["filename"]
        matches = sorted(path for path in data_dir.rglob(filename) if path.is_file())
        if len(matches) != 1:
            raise ValueError(f"expected one downloaded Arduino archive {filename}, found {len(matches)}")
        path = matches[0]
        size = path.stat().st_size
        digest = sha256_file(path)
        if size != expected["size"] or digest != expected["sha256"]:
            raise ValueError(f"Arduino archive mismatch: {filename}")
        verified.append(
            {
                "filename": filename,
                "relative_path": path.relative_to(data_dir).as_posix(),
                "sha256": digest,
                "size": size,
            }
        )
    return sorted(verified, key=lambda item: item["filename"])


def build_receipt(
    metadata_path: Path,
    root: Path,
    *,
    arduino_data_dir: Path | None,
    arduino_cli_version: str | None,
) -> dict[str, Any]:
    data = load_metadata(metadata_path)
    validated = validate_metadata(data, root)
    expected_cli = validated["cli_version"]
    if arduino_cli_version is not None and arduino_cli_version != expected_cli:
        raise ValueError(f"Arduino CLI version mismatch: {arduino_cli_version} != {expected_cli}")
    archives = (
        verify_archives(arduino_data_dir, validated["archive_inventory"])
        if arduino_data_dir is not None
        else []
    )
    commit = _git(root, "rev-parse", "HEAD")
    if not GIT_SHA_RE.fullmatch(commit):
        raise ValueError("source commit is not an exact Git SHA")
    return {
        "schema": 1,
        "kind": "d1l_arduino_build_inputs",
        "ok": True,
        "source_commit": commit,
        "metadata": {
            "path": metadata_path.relative_to(root).as_posix(),
            "sha256": sha256_file(metadata_path),
        },
        "arduino_cli_version": expected_cli,
        "rp2040_core_version": validated["core_version"],
        "submodules": validated["submodules"],
        "archives_verified": arduino_data_dir is not None,
        "archives": archives,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--metadata", default=".github/d1l-build-inputs.json")
    parser.add_argument("--arduino-data-dir")
    parser.add_argument("--arduino-cli-version")
    parser.add_argument("--out")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    metadata_path = Path(args.metadata)
    if not metadata_path.is_absolute():
        metadata_path = root / metadata_path
    data_dir = Path(args.arduino_data_dir).resolve() if args.arduino_data_dir else None
    receipt = build_receipt(
        metadata_path,
        root,
        arduino_data_dir=data_dir,
        arduino_cli_version=args.arduino_cli_version,
    )
    payload = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if args.out:
        output = Path(args.out)
        if not output.is_absolute():
            output = root / output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(payload, encoding="utf-8", newline="\n")
    print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
