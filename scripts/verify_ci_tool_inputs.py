#!/usr/bin/env python3
"""Verify byte-locked host tools used by the D1L release workflow."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import subprocess
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
GIT_SHA_RE = re.compile(r"^[0-9a-f]{40}$")
RECEIPT_KIND = "d1l_ci_tool_bytes"


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


def _positive_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"{label} must be a positive integer")
    return value


def _sha256(value: Any, label: str) -> str:
    digest = _required_string(value, label)
    if SHA256_RE.fullmatch(digest) is None:
        raise ValueError(f"{label} must be a lowercase SHA-256")
    return digest


def _archive_contract(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{label} archive identity is missing")
    filename = _required_string(value.get("filename"), f"{label} archive filename")
    url = _required_string(value.get("url"), f"{label} archive URL")
    if Path(filename).name != filename:
        raise ValueError(f"{label} archive filename must be a basename")
    parsed = urlparse(url)
    if parsed.scheme != "https" or Path(parsed.path).name != filename:
        raise ValueError(f"{label} archive URL does not identify {filename}")
    return {
        "url": url,
        "filename": filename,
        "sha256": _sha256(value.get("sha256"), f"{label} archive SHA-256"),
        "size": _positive_int(value.get("size"), f"{label} archive size"),
    }


def _executable_contract(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{label} executable identity is missing")
    commands = value.get("commands")
    if (
        not isinstance(commands, list)
        or not commands
        or any(not isinstance(item, str) or not item.strip() for item in commands)
        or len(set(commands)) != len(commands)
    ):
        raise ValueError(f"{label} executable commands are invalid")
    resolved_path = _required_string(
        value.get("resolved_path"), f"{label} executable resolved path"
    )
    normalized_path = resolved_path.replace("\\", "/")
    if not (
        normalized_path.startswith("/")
        or re.fullmatch(r"[A-Za-z]:/.*", normalized_path) is not None
    ):
        raise ValueError(f"{label} executable resolved path must be absolute")
    return {
        "commands": commands,
        "resolved_path": normalized_path,
        "sha256": _sha256(value.get("sha256"), f"{label} executable SHA-256"),
        "size": _positive_int(value.get("size"), f"{label} executable size"),
    }


def clang_contract(data: dict[str, Any]) -> dict[str, Any]:
    ci_tools = data.get("ci_tools")
    conformance = (
        ci_tools.get("meshcore_conformance") if isinstance(ci_tools, dict) else None
    )
    clang = conformance.get("clang") if isinstance(conformance, dict) else None
    if not isinstance(conformance, dict) or not isinstance(clang, dict):
        raise ValueError("MeshCore conformance Clang inputs are missing")
    runner = _required_string(conformance.get("runner"), "conformance runner")
    architecture = _required_string(
        conformance.get("architecture"), "conformance runner architecture"
    )
    if runner != "ubuntu-24.04" or architecture != "x86_64":
        raise ValueError("MeshCore conformance runner identity is unsupported")
    version = _required_string(clang.get("version"), "Clang version")
    if re.fullmatch(r"\d+\.\d+\.\d+", version) is None:
        raise ValueError("Clang version must be exact")
    package_name = _required_string(clang.get("package_name"), "Clang package name")
    package_version = _required_string(
        clang.get("package_version"), "Clang package version"
    )
    if package_name != "clang-18" or package_version != "1:18.1.3-1ubuntu1":
        raise ValueError("Clang package identity is unsupported")
    return {
        "runner": runner,
        "architecture": architecture,
        "version": version,
        "package_name": package_name,
        "package_version": package_version,
        "archive": _archive_contract(clang.get("archive"), "Clang package"),
        "executable": _executable_contract(clang.get("executable"), "Clang"),
    }


def _verify_file(path: Path, expected: dict[str, Any], label: str) -> dict[str, Any]:
    if not path.is_file():
        raise ValueError(f"{label} is missing: {path}")
    size = path.stat().st_size
    digest = sha256_file(path)
    if size != expected["size"] or digest != expected["sha256"]:
        raise ValueError(f"{label} byte identity mismatch")
    return {"sha256": digest, "size": size, "verified": True}


def _git_commit(root: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    commit = result.stdout.strip()
    if GIT_SHA_RE.fullmatch(commit) is None:
        raise ValueError("source commit is not an exact Git SHA")
    return commit


def build_clang_receipt(
    metadata_path: Path,
    root: Path,
    *,
    archive_path: Path,
    cc_path: Path,
    cxx_path: Path,
    source_commit: str | None = None,
) -> dict[str, Any]:
    data = load_metadata(metadata_path)
    contract = clang_contract(data)
    archive = {
        **contract["archive"],
        **_verify_file(archive_path, contract["archive"], "Clang package archive"),
    }
    expected_executable = contract["executable"]
    command_paths = {"cc": cc_path, "cxx": cxx_path}
    executable_identities: dict[str, dict[str, Any]] = {}
    for role, command_path in command_paths.items():
        try:
            resolved = command_path.resolve(strict=True)
        except OSError as exc:
            raise ValueError(f"Clang {role} executable is missing: {command_path}") from exc
        if resolved.as_posix() != expected_executable["resolved_path"]:
            raise ValueError(f"Clang {role} resolved path mismatch")
        executable_identities[role] = {
            "command": expected_executable["commands"][0 if role == "cc" else 1],
            "resolved_path": resolved.as_posix(),
            **_verify_file(resolved, expected_executable, f"Clang {role} executable"),
        }
    commit = source_commit or _git_commit(root)
    if GIT_SHA_RE.fullmatch(commit) is None:
        raise ValueError("source commit is not an exact Git SHA")
    receipt = {
        "schema": 1,
        "kind": RECEIPT_KIND,
        "ok": True,
        "source_commit": commit,
        "metadata": {
            "path": metadata_path.relative_to(root).as_posix(),
            "sha256": sha256_file(metadata_path),
        },
        "runner": contract["runner"],
        "architecture": contract["architecture"],
        "clang": {
            "version": contract["version"],
            "package_name": contract["package_name"],
            "package_version": contract["package_version"],
            "archive": archive,
            "executables": executable_identities,
            "bytes_verified": True,
        },
    }
    validate_clang_receipt(receipt, data, commit, sha256_file(metadata_path))
    return receipt


def validate_clang_receipt(
    receipt: object,
    metadata: dict[str, Any],
    expected_commit: str,
    metadata_sha256: str,
) -> None:
    contract = clang_contract(metadata)
    if not isinstance(receipt, dict):
        raise ValueError("Clang tool-byte receipt must be an object")
    required = {
        "schema": receipt.get("schema") == 1,
        "kind": receipt.get("kind") == RECEIPT_KIND,
        "ok": receipt.get("ok") is True,
        "source_commit": receipt.get("source_commit") == expected_commit,
        "runner": receipt.get("runner") == contract["runner"],
        "architecture": receipt.get("architecture") == contract["architecture"],
    }
    metadata_receipt = receipt.get("metadata")
    required["metadata"] = isinstance(metadata_receipt, dict) and metadata_receipt == {
        "path": ".github/d1l-build-inputs.json",
        "sha256": metadata_sha256,
    }
    clang = receipt.get("clang")
    required["clang"] = isinstance(clang, dict)
    if isinstance(clang, dict):
        required.update(
            {
                "clang_version": clang.get("version") == contract["version"],
                "clang_package_name": clang.get("package_name")
                == contract["package_name"],
                "clang_package_version": clang.get("package_version")
                == contract["package_version"],
                "clang_archive": clang.get("archive")
                == {**contract["archive"], "verified": True},
                "clang_bytes_verified": clang.get("bytes_verified") is True,
            }
        )
        executables = clang.get("executables")
        required["clang_executables"] = isinstance(executables, dict)
        if isinstance(executables, dict):
            for index, role in enumerate(("cc", "cxx")):
                required[f"clang_{role}"] = executables.get(role) == {
                    "command": contract["executable"]["commands"][index],
                    "resolved_path": contract["executable"]["resolved_path"],
                    "sha256": contract["executable"]["sha256"],
                    "size": contract["executable"]["size"],
                    "verified": True,
                }
    failed = [name for name, passed in required.items() if not passed]
    if failed:
        raise ValueError("Clang tool-byte receipt is incomplete or stale: " + ", ".join(failed))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--metadata", default=".github/d1l-build-inputs.json")
    parser.add_argument("--clang-package", required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    root = Path(args.root).resolve()
    metadata_path = Path(args.metadata)
    if not metadata_path.is_absolute():
        metadata_path = root / metadata_path
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise SystemExit("Clang tool-byte verification requires Linux x86_64")
    receipt = build_clang_receipt(
        metadata_path,
        root,
        archive_path=Path(args.clang_package),
        cc_path=Path(args.cc),
        cxx_path=Path(args.cxx),
    )
    output = Path(args.out)
    if not output.is_absolute():
        output = root / output
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    output.write_text(payload, encoding="utf-8", newline="\n")
    print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
