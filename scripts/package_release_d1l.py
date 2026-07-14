#!/usr/bin/env python3
"""Package MeshCore DeskOS D1L firmware artifacts for release handoff."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from datetime import datetime, timedelta, timezone
from pathlib import Path

try:
    from verify_checksums import is_link_or_reparse, verify_checksum_tree
except ModuleNotFoundError:
    from scripts.verify_checksums import is_link_or_reparse, verify_checksum_tree

if __package__:
    from .meshcore_conformance_d1l import (
        CANONICAL_EVIDENCE_PROFILE,
        canonicalize_release_report,
    )
    from .provenance_d1l import write_package_provenance
    from .sbom_d1l import (
        discover_source_identity,
        exact_sha,
        write_package_sbom,
    )
else:
    from meshcore_conformance_d1l import (  # type: ignore[no-redef]
        CANONICAL_EVIDENCE_PROFILE,
        canonicalize_release_report,
    )
    from provenance_d1l import write_package_provenance  # type: ignore[no-redef]
    from sbom_d1l import (  # type: ignore[no-redef]
        discover_source_identity,
        exact_sha,
        write_package_sbom,
    )


PROJECT = "MeshCore DeskOS D1L"
DEFAULT_FLASH_SIZE = 8 * 1024 * 1024
FLASH_BAUD = 460800
EXPECTED_BSP_PATCHES = (
    Path("patches/sensecap_indicator_touch_fix.patch"),
    Path("patches/sensecap_indicator_idf55_compat.patch"),
)
EXPECTED_BSP_SUBMODULE = Path("third_party/sensecap_indicator_esp32")
RELEASE_DOC_SPECS = [
    ("docs/USER_GUIDE_D1L.md", "USER_GUIDE_D1L.md"),
    ("docs/DEVELOPER_GUIDE_D1L.md", "DEVELOPER_GUIDE_D1L.md"),
    ("docs/FLASH_RECOVERY_D1L.md", "FLASH_RECOVERY_D1L.md"),
    ("docs/RP2040_SD_BRIDGE_FLASH_D1L.md", "RP2040_SD_BRIDGE_FLASH_D1L.md"),
]
RP2040_ARTIFACT_NAMES = [
    "rp2040-sd-bridge-firmware",
    "rp2040-sd-smoke-firmware",
    "rp2040-seeed-official-sd-smoke-firmware",
]
MESHCORE_CONFORMANCE_ARTIFACT_TYPE = "d1l_meshcore_wire_conformance"
MESHCORE_CONFORMANCE_BOUNDARY = "wire_envelope_only"
MESHCORE_CONFORMANCE_MAX_AGE_DAYS = 14
MESHCORE_CONFORMANCE_CLOCK_SKEW_MINUTES = 5
MESHCORE_CONFORMANCE_ACTIONS_ARTIFACT = "d1l-meshcore-wire-conformance"


def utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_offset(value: str) -> int:
    return int(value, 0)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_utc_timestamp(value: object, field: str) -> datetime:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"MeshCore conformance {field} is missing")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (ValueError, OverflowError) as exc:
        raise ValueError(f"MeshCore conformance {field} is not a valid timestamp") from exc
    if parsed.tzinfo is None:
        raise ValueError(f"MeshCore conformance {field} must include a timezone")
    try:
        return parsed.astimezone(timezone.utc)
    except (ValueError, OverflowError) as exc:
        raise ValueError(f"MeshCore conformance {field} is outside the supported range") from exc


def copy_meshcore_conformance_evidence(
    source: Path | None,
    package_dir: Path,
    expected_commit: str | None,
) -> dict | None:
    if source is None:
        return None
    source = source.resolve()
    if not source.is_file():
        raise FileNotFoundError(f"Missing MeshCore conformance JSON {source}")
    if not expected_commit:
        raise ValueError("Cannot verify MeshCore conformance without an expected release commit")

    try:
        report = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"MeshCore conformance JSON is unreadable: {source}") from exc
    if not isinstance(report, dict):
        raise ValueError("MeshCore conformance JSON must contain an object")

    source_verification = report.get("source_verification")
    source_commit = (
        source_verification.get("repository_commit")
        if isinstance(source_verification, dict)
        else None
    )
    required = {
        "schema_version": report.get("schema_version") == 1,
        "artifact_type": report.get("artifact_type") == MESHCORE_CONFORMANCE_ARTIFACT_TYPE,
        "passed": report.get("passed") is True,
        "status": report.get("status") == "pass",
        "execution_complete": report.get("execution_complete") is True,
        "coverage_boundary": report.get("coverage_boundary") == MESHCORE_CONFORMANCE_BOUNDARY,
        "coverage_level": report.get("coverage_level") == MESHCORE_CONFORMANCE_BOUNDARY,
        "closure_ready_false": report.get("closure_ready") is False,
        "issue_65_closure_eligible_false": report.get("issue_65_closure_eligible") is False,
        "source_commit": source_commit == expected_commit,
    }
    failed = [name for name, ok in required.items() if not ok]
    if failed:
        raise ValueError("MeshCore conformance validation failed: " + ", ".join(failed))

    generated_at = parse_utc_timestamp(report.get("generated_at"), "generated_at")
    now = datetime.now(timezone.utc)
    if generated_at > now + timedelta(minutes=MESHCORE_CONFORMANCE_CLOCK_SKEW_MINUTES):
        raise ValueError("MeshCore conformance generated_at is in the future")
    try:
        expires_at = generated_at + timedelta(days=MESHCORE_CONFORMANCE_MAX_AGE_DAYS)
    except OverflowError as exc:
        raise ValueError("MeshCore conformance generated_at is outside the supported range") from exc
    if now >= expires_at:
        raise ValueError("MeshCore conformance evidence is expired")

    expected_name = f"meshcore_conformance_{expected_commit}.json"
    if source.name != expected_name:
        raise ValueError(
            f"MeshCore conformance filename must be {expected_name}, got {source.name}"
        )
    raw_size = source.stat().st_size
    raw_sha256 = sha256_file(source)
    canonical_report = canonicalize_release_report(report)
    if canonical_report.get("evidence_profile") != CANONICAL_EVIDENCE_PROFILE:
        raise ValueError("MeshCore conformance canonical evidence profile is invalid")

    evidence_dir = package_dir / "evidence"
    evidence_dir.mkdir(parents=True, exist_ok=True)
    dest = evidence_dir / expected_name
    dest.write_text(
        json.dumps(canonical_report, indent=2, sort_keys=True, ensure_ascii=True) + "\n",
        encoding="ascii",
    )
    return {
        "artifact_type": MESHCORE_CONFORMANCE_ARTIFACT_TYPE,
        "path": dest.relative_to(package_dir).as_posix(),
        "size": dest.stat().st_size,
        "sha256": sha256_file(dest),
        "source_commit": source_commit,
        "evidence_profile": CANONICAL_EVIDENCE_PROFILE,
        "run_receipt": {
            "artifact": MESHCORE_CONFORMANCE_ACTIONS_ARTIFACT,
            "path": expected_name,
            "size": raw_size,
            "sha256": raw_sha256,
            "generated_at": generated_at.isoformat().replace("+00:00", "Z"),
            "expires_at": expires_at.isoformat().replace("+00:00", "Z"),
        },
        "max_age_days": MESHCORE_CONFORMANCE_MAX_AGE_DAYS,
        "coverage_boundary": MESHCORE_CONFORMANCE_BOUNDARY,
        "coverage_level": MESHCORE_CONFORMANCE_BOUNDARY,
        "closure_ready": False,
        "issue_65_closure_eligible": False,
        "passed": True,
        "execution_complete": True,
        "note": "Structural wire-envelope prerequisite only; this evidence does not close issue #65.",
    }


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
    # Porcelain status uses the first two columns as semantic XY state. Keep
    # leading spaces so a worktree-only submodule edit cannot be confused with
    # a staged gitlink change.
    return result.stdout.rstrip("\r\n") or None


def command_succeeds(cwd: Path, args: list[str]) -> bool:
    try:
        subprocess.run(
            args,
            cwd=cwd,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return False
    return True


def expected_bsp_patches_applied(root: Path) -> bool:
    root = root.resolve()
    submodule = root / EXPECTED_BSP_SUBMODULE
    if not submodule.exists():
        return False

    for relative_patch in EXPECTED_BSP_PATCHES:
        patch = root / relative_patch
        if not patch.exists() or not command_succeeds(
            submodule,
            [
                "git",
                "apply",
                "--unidiff-zero",
                "--reverse",
                "--check",
                "--ignore-space-change",
                str(patch),
            ],
        ):
            return False
    return True


def run_git_command(
    cwd: Path,
    args: list[str],
    *,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str] | None:
    try:
        return subprocess.run(
            ["git", *args],
            cwd=cwd,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            env=env,
        )
    except (FileNotFoundError, subprocess.CalledProcessError, OSError):
        return None


def exact_expected_bsp_patch_state(root: Path) -> bool:
    """Prove the BSP worktree is exactly HEAD plus the two tracked patches."""
    root = root.resolve()
    submodule = root / EXPECTED_BSP_SUBMODULE
    patches = [root / relative for relative in EXPECTED_BSP_PATCHES]
    if not submodule.is_dir() or any(not patch.is_file() for patch in patches):
        return False

    gitlink = run_git_command(
        root, ["ls-tree", "HEAD", "--", EXPECTED_BSP_SUBMODULE.as_posix()]
    )
    submodule_head = run_git_command(submodule, ["rev-parse", "HEAD"])
    if gitlink is None or submodule_head is None:
        return False
    match = re.fullmatch(
        r"160000 commit ([0-9a-f]{40})\t" + re.escape(EXPECTED_BSP_SUBMODULE.as_posix()),
        gitlink.stdout.strip(),
    )
    if match is None or submodule_head.stdout.strip() != match.group(1):
        return False

    # The expected build patches are unstaged worktree edits. Reject staged or
    # untracked content before comparing the exact tracked tree.
    if run_git_command(submodule, ["diff", "--cached", "--quiet", "--exit-code"]) is None:
        return False
    untracked = run_git_command(
        submodule, ["ls-files", "--others", "--exclude-standard"]
    )
    if untracked is None or untracked.stdout.strip():
        return False

    with tempfile.TemporaryDirectory(prefix="d1l-bsp-patch-state-") as temporary:
        temp = Path(temporary)
        expected_env = os.environ.copy()
        expected_env["GIT_INDEX_FILE"] = str(temp / "expected.index")
        actual_env = os.environ.copy()
        actual_env["GIT_INDEX_FILE"] = str(temp / "actual.index")

        if run_git_command(submodule, ["read-tree", "HEAD"], env=expected_env) is None:
            return False
        for patch in patches:
            if run_git_command(
                submodule,
                [
                    "apply",
                    "--cached",
                    "--unidiff-zero",
                    "--ignore-space-change",
                    str(patch),
                ],
                env=expected_env,
            ) is None:
                return False
        expected_tree = run_git_command(submodule, ["write-tree"], env=expected_env)

        if run_git_command(submodule, ["read-tree", "HEAD"], env=actual_env) is None:
            return False
        if run_git_command(submodule, ["add", "-u", "--", "."], env=actual_env) is None:
            return False
        actual_tree = run_git_command(submodule, ["write-tree"], env=actual_env)
        return (
            expected_tree is not None
            and actual_tree is not None
            and expected_tree.stdout.strip() == actual_tree.stdout.strip()
        )


def clean_release_status_entries(root: Path, status: str) -> tuple[list[str], list[str]]:
    entries = [line for line in status.splitlines() if line.strip()]
    if not entries:
        return [], []

    expected_submodule = EXPECTED_BSP_SUBMODULE.as_posix()
    expected_entries = [line for line in entries if status_path(line) == expected_submodule]
    other_entries = [line for line in entries if status_path(line) != expected_submodule]
    expected_entry_is_worktree_only = (
        len(expected_entries) == 1
        and len(expected_entries[0]) >= 3
        and expected_entries[0][0] == " "
        and expected_entries[0][1] in {"M", "m"}
    )
    if (
        expected_entry_is_worktree_only
        and exact_expected_bsp_patch_state(root)
    ):
        return other_entries, [patch.as_posix() for patch in EXPECTED_BSP_PATCHES]

    return entries, []


def status_path(status_line: str) -> str:
    parts = status_line.split(maxsplit=1)
    return parts[1] if len(parts) == 2 else ""


def git_info(root: Path) -> dict:
    root = root.resolve()
    status = git_value(root, "status", "--porcelain") or ""
    dirty_entries, source_patches = clean_release_status_entries(root, status)
    return {
        "commit": git_value(root, "rev-parse", "HEAD"),
        "short_commit": git_value(root, "rev-parse", "--short", "HEAD"),
        "branch": git_value(root, "branch", "--show-current"),
        "dirty": bool(dirty_entries),
        "dirty_entries": dirty_entries,
        "source_patches": source_patches,
    }


def load_flasher_args(build_dir: Path) -> dict:
    path = build_dir / "flasher_args.json"
    if not path.exists():
        raise FileNotFoundError(f"Missing {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def ordered_flash_files(flasher_args: dict) -> list[tuple[int, str]]:
    files = flasher_args.get("flash_files", {})
    if not isinstance(files, dict) or not files:
        raise ValueError("flasher_args.json has no flash_files map")
    return sorted((parse_offset(offset), rel_path) for offset, rel_path in files.items())


def flash_role_for_path(path: str) -> str:
    name = Path(path).name
    if "bootloader" in path.replace("\\", "/"):
        return "bootloader"
    if name == "partition-table.bin":
        return "partition-table"
    if name.endswith(".bin"):
        return "app"
    return "artifact"


def copy_flash_files(build_dir: Path, firmware_dir: Path, flasher_args: dict) -> list[dict]:
    firmware_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for offset, rel_path in ordered_flash_files(flasher_args):
        source = build_dir / rel_path
        if not source.exists():
            raise FileNotFoundError(f"Missing flash file {source}")
        dest = firmware_dir / Path(rel_path).name
        shutil.copy2(source, dest)
        entries.append(
            {
                "role": flash_role_for_path(rel_path),
                "offset": f"0x{offset:x}",
                "source": rel_path.replace("\\", "/"),
                "path": dest.relative_to(firmware_dir.parent).as_posix(),
                "size": dest.stat().st_size,
                "sha256": sha256_file(dest),
            }
        )
    flasher_dest = firmware_dir / "flasher_args.json"
    shutil.copy2(build_dir / "flasher_args.json", flasher_dest)
    return entries


def copy_optional_debug_files(build_dir: Path, package_dir: Path) -> list[dict]:
    debug_dir = package_dir / "debug"
    copied = []
    for pattern in ["*.elf", "*.map"]:
        for source in sorted(build_dir.glob(pattern)):
            debug_dir.mkdir(parents=True, exist_ok=True)
            dest = debug_dir / source.name
            shutil.copy2(source, dest)
            copied.append(
                {
                    "path": dest.relative_to(package_dir).as_posix(),
                    "size": dest.stat().st_size,
                    "sha256": sha256_file(dest),
                }
            )
    return copied


def copy_notice_files(root: Path, package_dir: Path) -> list[dict]:
    notice_specs = [
        ("LICENSE", "LICENSE"),
        ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
        ("docs/ATTRIBUTIONS.md", "ATTRIBUTIONS.md"),
        ("docs/SOURCE_AUDIT_AND_ATTRIBUTION.md", "SOURCE_AUDIT_AND_ATTRIBUTION.md"),
    ]
    notices_dir = package_dir / "notices"
    copied = []
    for source_rel, dest_name in notice_specs:
        source = root / source_rel
        if not source.exists():
            continue
        notices_dir.mkdir(parents=True, exist_ok=True)
        dest = notices_dir / dest_name
        shutil.copy2(source, dest)
        copied.append({
            "path": dest.relative_to(package_dir).as_posix(),
            "source": source_rel,
            "sha256": sha256_file(dest),
        })
    return copied


def copy_release_docs(root: Path, package_dir: Path) -> list[dict]:
    docs_dir = package_dir / "docs"
    copied = []
    for source_rel, dest_name in RELEASE_DOC_SPECS:
        source = root / source_rel
        if not source.exists():
            continue
        docs_dir.mkdir(parents=True, exist_ok=True)
        dest = docs_dir / dest_name
        shutil.copy2(source, dest)
        copied.append({
            "path": dest.relative_to(package_dir).as_posix(),
            "source": source_rel,
            "sha256": sha256_file(dest),
        })
    return copied


def copy_rp2040_artifacts(artifact_root: Path | None, package_dir: Path) -> list[dict]:
    if artifact_root is None:
        return []
    try:
        artifact_root = artifact_root.resolve(strict=True)
    except (OSError, RuntimeError):
        raise FileNotFoundError(f"Missing RP2040 artifact root {artifact_root}") from None
    if not artifact_root.is_dir():
        raise FileNotFoundError(f"Missing RP2040 artifact root {artifact_root}")

    missing = [name for name in RP2040_ARTIFACT_NAMES if not (artifact_root / name).is_dir()]
    if missing:
        raise FileNotFoundError("Missing RP2040 release artifacts: " + ", ".join(missing))

    copied = []
    rp2040_dir = package_dir / "rp2040"
    for artifact_name in RP2040_ARTIFACT_NAMES:
        source_dir = artifact_root / artifact_name
        try:
            source_resolved = source_dir.resolve(strict=True)
            source_resolved.relative_to(artifact_root)
        except (OSError, RuntimeError, ValueError):
            raise ValueError(
                f"{artifact_name} source directory must be a real direct child "
                "of the RP2040 artifact root"
            ) from None
        if (
            is_link_or_reparse(source_dir)
            or not source_dir.is_dir()
            or source_resolved != source_dir
        ):
            raise ValueError(
                f"{artifact_name} source directory must be a real direct child "
                "of the RP2040 artifact root"
            )

        source_manifests = sorted(source_dir.rglob("SHA256SUMS.txt"))
        source_manifest = source_dir / "SHA256SUMS.txt"
        if source_manifests != [source_manifest] or not verify_checksum_tree(source_dir):
            raise ValueError(
                f"{artifact_name} must contain exactly one valid root SHA256SUMS.txt"
            )
        dest_dir = rp2040_dir / artifact_name
        shutil.copytree(source_dir, dest_dir)
        dest_manifests = sorted(dest_dir.rglob("SHA256SUMS.txt"))
        dest_manifest = dest_dir / "SHA256SUMS.txt"
        if dest_manifests != [dest_manifest] or not verify_checksum_tree(dest_dir):
            raise ValueError(f"{artifact_name} checksum verification changed after copy")
        files = []
        uf2_files = []
        for path in sorted(dest_dir.rglob("*")):
            if not path.is_file():
                continue
            rel_path = path.relative_to(package_dir).as_posix()
            entry = {
                "path": rel_path,
                "size": path.stat().st_size,
                "sha256": sha256_file(path),
            }
            files.append(entry)
            if path.suffix.lower() == ".uf2":
                uf2_files.append(rel_path)
        if not uf2_files:
            raise ValueError(f"{artifact_name} does not contain a UF2 file")
        copied.append({
            "name": artifact_name,
            "path": dest_dir.relative_to(package_dir).as_posix(),
            "uf2_files": uf2_files,
            "files": files,
        })
    return copied


def d1l_firmware_version(root: Path) -> str:
    config = root / "main" / "d1l_config.h"
    if not config.exists():
        return "unknown"
    match = re.search(r'#define\s+D1L_FIRMWARE_VERSION\s+"([^"]+)"', config.read_text(encoding="utf-8"))
    return match.group(1) if match else "unknown"


def workflow_info() -> dict:
    run_id = os.environ.get("GITHUB_RUN_ID")
    repository = os.environ.get("GITHUB_REPOSITORY")
    server_url = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
    return {
        "run_id": run_id,
        "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT"),
        "workflow": os.environ.get("GITHUB_WORKFLOW"),
        "sha": os.environ.get("GITHUB_SHA"),
        "ref": os.environ.get("GITHUB_REF"),
        "repository": repository,
        "run_url": f"{server_url}/{repository}/actions/runs/{run_id}" if repository and run_id else None,
    }


def app_entry(entries: list[dict]) -> dict:
    for entry in entries:
        if entry["role"] == "app":
            return entry
    raise ValueError("No app binary found in flash files")


def copy_update_image(package_dir: Path, firmware_dir: Path, app: dict) -> dict:
    update_dir = package_dir / "update"
    update_dir.mkdir(parents=True, exist_ok=True)
    source = firmware_dir.parent / app["path"]
    dest = update_dir / "meshcore_deskos_d1l-app.bin"
    shutil.copy2(source, dest)
    return {
        "path": dest.relative_to(package_dir).as_posix(),
        "size": dest.stat().st_size,
        "sha256": sha256_file(dest),
        "note": "Application image for OTA/update flows once enabled; serial release flashing still uses the project flash set.",
    }


def write_full_flash_image(build_dir: Path, package_dir: Path, flasher_args: dict, size: int) -> dict:
    image_dir = package_dir / "full-flash"
    image_dir.mkdir(parents=True, exist_ok=True)
    image = image_dir / "meshcore_deskos_d1l-full-8mb.bin"
    with image.open("wb") as out:
        out.write(b"\xff" * size)
    with image.open("r+b") as out:
        for offset, rel_path in ordered_flash_files(flasher_args):
            data = (build_dir / rel_path).read_bytes()
            end = offset + len(data)
            if end > size:
                raise ValueError(f"{rel_path} at 0x{offset:x} exceeds full image size")
            out.seek(offset)
            out.write(data)
    return {
        "path": image.relative_to(package_dir).as_posix(),
        "size": image.stat().st_size,
        "sha256": sha256_file(image),
        "flash_offset": "0x0",
        "warning": "Factory/full-flash image is 0xff padded and intended for full-image recovery or factory flows, not NVS-preserving app updates.",
    }


def write_sha256sums(package_dir: Path) -> None:
    rows = []
    manifest = package_dir / "SHA256SUMS.txt"
    paths = sorted(
        package_dir.rglob("*"),
        key=lambda path: path.relative_to(package_dir).as_posix(),
    )
    for path in paths:
        if not path.is_file() or path == manifest:
            continue
        rel = path.relative_to(package_dir).as_posix()
        rows.append(f"{sha256_file(path)}  ./{rel}")
    manifest.write_text("\n".join(rows) + "\n", encoding="ascii")


def command_flash_files(entries: list[dict]) -> list[str]:
    args: list[str] = []
    for entry in sorted(entries, key=lambda item: parse_offset(item["offset"])):
        args.extend([entry["offset"], entry["path"]])
    return args


def write_flash_scripts(package_dir: Path, entries: list[dict], flasher_args: dict, full_image: dict) -> dict:
    flash_settings = flasher_args.get("flash_settings", {})
    flash_mode = flash_settings.get("flash_mode", "dio")
    flash_size = flash_settings.get("flash_size", "8MB")
    flash_freq = flash_settings.get("flash_freq", "80m")
    project_args = command_flash_files(entries)

    ps_project = package_dir / "flash_project.ps1"
    ps_project.write_text(
        "\n".join(
            [
                "param([string]$Port = $env:D1L_PORT)",
                '$ErrorActionPreference = "Stop"',
                'if ([string]::IsNullOrWhiteSpace($Port)) { throw "No D1L port supplied. Set D1L_PORT or pass -Port." }',
                "$Root = Split-Path -Parent $MyInvocation.MyCommand.Path",
                "$Firmware = Join-Path $Root 'firmware'",
                "python -m esptool --chip esp32s3 --port $Port --baud "
                f"{FLASH_BAUD} --before default-reset --after hard-reset write-flash "
                f"--flash-mode {flash_mode} --flash-size {flash_size} --flash-freq {flash_freq} "
                + " ".join(
                    f"{project_args[i]} (Join-Path $Root '{project_args[i + 1]}')"
                    for i in range(0, len(project_args), 2)
                ),
                "if ($LASTEXITCODE -ne 0) { throw \"Project flash failed with exit code $LASTEXITCODE\" }",
                "",
            ]
        ),
        encoding="ascii",
    )

    ps_full = package_dir / "flash_full_8mb.ps1"
    ps_full.write_text(
        "\n".join(
            [
                "param([string]$Port = $env:D1L_PORT)",
                '$ErrorActionPreference = "Stop"',
                'if ([string]::IsNullOrWhiteSpace($Port)) { throw "No D1L port supplied. Set D1L_PORT or pass -Port." }',
                "$Root = Split-Path -Parent $MyInvocation.MyCommand.Path",
                'Write-Warning "This writes the full 8MB image at 0x0 and can overwrite persisted settings/logs."',
                '$Confirm = Read-Host "Type FULL-FLASH-$Port to continue"',
                'if ($Confirm -ne "FULL-FLASH-$Port") { throw "Full flash confirmation failed." }',
                "python -m esptool --chip esp32s3 --port $Port --baud "
                f"{FLASH_BAUD} --before default-reset --after hard-reset write-flash "
                f"--flash-mode {flash_mode} --flash-size {flash_size} --flash-freq {flash_freq} "
                f"{full_image['flash_offset']} (Join-Path $Root '{full_image['path']}')",
                "if ($LASTEXITCODE -ne 0) { throw \"Full flash failed with exit code $LASTEXITCODE\" }",
                "",
            ]
        ),
        encoding="ascii",
    )

    sh_project = package_dir / "flash_project.sh"
    sh_project.write_text(
        "\n".join(
            [
                "#!/usr/bin/env sh",
                "set -eu",
                ': "${D1L_PORT:?Set D1L_PORT to the D1L serial port.}"',
                'ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"',
                "python -m esptool --chip esp32s3 --port \"$D1L_PORT\" --baud "
                f"{FLASH_BAUD} --before default-reset --after hard-reset write-flash "
                f"--flash-mode {flash_mode} --flash-size {flash_size} --flash-freq {flash_freq} "
                + " ".join(
                    f"{project_args[i]} \"$ROOT/{project_args[i + 1]}\""
                    for i in range(0, len(project_args), 2)
                ),
                "",
            ]
        ),
        encoding="ascii",
    )
    sh_project.chmod(0o755)

    return {
        "windows_project_flash": ps_project.name,
        "windows_full_flash": ps_full.name,
        "posix_project_flash": sh_project.name,
    }


def write_release_readme(package_dir: Path, package_name: str, manifest: dict) -> None:
    readme = package_dir / "README_RELEASE.md"
    app = app_entry(manifest["flash_files"])
    if manifest.get("rp2040_artifacts"):
        rp2040_contents = "- `rp2040/` contains the Actions-built RP2040 SD bridge, legacy smoke, and official Seeed SD smoke UF2 artifacts."
    else:
        rp2040_contents = (
            "- `rp2040/` is omitted from this ESP32-only package. Re-run `d1l-ci` with "
            "`include_sd_bridge=true`, or change SD/RP2040 sources, when bridge UF2 artifacts are required."
        )
    readme.write_text(
        f"""# {PROJECT} Release Package

Package: `{package_name}`

Git commit: `{manifest['git'].get('commit') or 'unknown'}`

## Contents

- `firmware/` contains the bootloader, partition table, app binary, and `flasher_args.json`.
{rp2040_contents}
- `update/meshcore_deskos_d1l-app.bin` is the application image for future OTA/update flows.
- `full-flash/meshcore_deskos_d1l-full-8mb.bin` is an 8MB factory/recovery image padded with `0xff`.
- `docs/` contains the user guide, developer guide, ESP32 flash recovery guide, and RP2040 SD bridge flash guide.
- `notices/` contains the project license, third-party notices, source audit notes, and attributions for public distribution.
- `evidence/` contains current-commit MeshCore wire-envelope conformance JSON when supplied by CI. It is a structural prerequisite and does not close issue #65.
- `{manifest['sbom']['path']}` is the deterministic SPDX 2.3 SBOM bound to the exact source, submodule, and package inputs.
- `{manifest['provenance']['path']}` is deterministic unsigned SLSA v1 provenance. Its checksums are verifiable, but authenticity requires a separately signed attestation.
- `SHA256SUMS.txt` covers every file in this package except itself.

## Normal Flash

Normal project flashing writes bootloader, partition table, and app at their ESP-IDF offsets while preserving unrelated flash regions.

```powershell
$env:D1L_PORT = "COMx"
.\\flash_project.ps1 -Port $env:D1L_PORT
```

Do not use a bot, bridge, or other non-D1L serial port for D1L flashing/testing unless the operator explicitly reassigns the hardware.

## Full Flash Image

The full 8MB image is for factory/recovery workflows. It can overwrite persisted settings, logs, contacts, and message state.

```powershell
$env:D1L_PORT = "COMx"
.\\flash_full_8mb.ps1 -Port $env:D1L_PORT
```

## Checksums

```powershell
Get-FileHash -Algorithm SHA256 firmware\\meshcore_deskos_d1l.bin
Get-Content .\\SHA256SUMS.txt
```

App image: `{app['path']}`

App SHA256: `{app['sha256']}`
""",
        encoding="ascii",
    )


def create_release_package(
    root: Path,
    build_dir: Path,
    out_dir: Path,
    package_name: str,
    full_size: int,
    rp2040_artifact_root: Path | None = None,
    meshcore_conformance_json: Path | None = None,
) -> dict:
    flasher_args = load_flasher_args(build_dir)
    package_dir = out_dir / package_name
    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.mkdir(parents=True, exist_ok=True)

    source_git = git_info(root)
    if source_git.get("dirty"):
        raise ValueError("Release packaging requires a clean source worktree")
    requested_commit = os.environ.get("GITHUB_SHA") or source_git.get("commit")
    source_identity = discover_source_identity(root, requested_commit)
    expected_commit = source_identity["commit"]
    repository_commit = source_git.get("commit")
    if repository_commit is not None and exact_sha(
        repository_commit, "repository source commit"
    ) != expected_commit:
        raise ValueError("release package source identity does not match repository HEAD")
    source_git["commit"] = expected_commit
    source_git["short_commit"] = expected_commit[:7]
    meshcore_conformance = copy_meshcore_conformance_evidence(
        meshcore_conformance_json,
        package_dir,
        expected_commit,
    )

    firmware_dir = package_dir / "firmware"
    entries = copy_flash_files(build_dir, firmware_dir, flasher_args)
    app = app_entry(entries)
    update_image = copy_update_image(package_dir, firmware_dir, app)
    full_image = write_full_flash_image(build_dir, package_dir, flasher_args, full_size)
    debug_files = copy_optional_debug_files(build_dir, package_dir)
    notice_files = copy_notice_files(root, package_dir)
    release_docs = copy_release_docs(root, package_dir)
    rp2040_artifacts = copy_rp2040_artifacts(rp2040_artifact_root, package_dir)
    scripts = write_flash_scripts(package_dir, entries, flasher_args, full_image)

    manifest = {
        "schema": 1,
        "project": PROJECT,
        "app_version": d1l_firmware_version(root),
        "package": package_name,
        "created_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "git": source_git,
        "workflow": workflow_info(),
        "source_build_dir": str(build_dir),
        "flash_settings": flasher_args.get("flash_settings", {}),
        "flash_files": entries,
        "rp2040_artifacts": rp2040_artifacts,
        "meshcore_conformance": meshcore_conformance,
        "update_image": update_image,
        "full_flash_image": full_image,
        "debug_files": debug_files,
        "release_docs": release_docs,
        "notice_files": notice_files,
        "scripts": scripts,
        "notes": [
            "Project flash scripts require D1L_PORT or an explicit -Port.",
            "Full 8MB flash script requires a typed confirmation because it can overwrite persisted state.",
            "Flash backup may be skipped only when the operator explicitly requests that for hardware validation.",
        ],
    }
    manifest["sbom"] = write_package_sbom(
        root,
        package_dir,
        manifest,
        source_identity=source_identity,
        expected_source_sha=expected_commit,
    )
    manifest["provenance"] = write_package_provenance(
        root,
        package_dir,
        manifest,
        source_identity=source_identity,
        expected_source_sha=expected_commit,
    )
    (package_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="ascii")
    write_release_readme(package_dir, package_name, manifest)
    write_sha256sums(package_dir)
    manifest["package_dir"] = str(package_dir)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--out-dir", default="artifacts/release")
    parser.add_argument("--package-name", default=None)
    parser.add_argument("--full-size", type=lambda value: int(value, 0), default=DEFAULT_FLASH_SIZE)
    parser.add_argument("--rp2040-artifact-root", default=None)
    parser.add_argument("--meshcore-conformance-json", default=None)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = root / build_dir
    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = root / out_dir
    info = git_info(root)
    package_name = args.package_name
    if not package_name:
        suffix = info.get("short_commit") or utc_stamp()
        package_name = f"d1l-release-{suffix}"

    rp2040_artifact_root = Path(args.rp2040_artifact_root) if args.rp2040_artifact_root else None
    if rp2040_artifact_root and not rp2040_artifact_root.is_absolute():
        rp2040_artifact_root = root / rp2040_artifact_root

    meshcore_conformance_json = (
        Path(args.meshcore_conformance_json) if args.meshcore_conformance_json else None
    )
    if meshcore_conformance_json and not meshcore_conformance_json.is_absolute():
        meshcore_conformance_json = root / meshcore_conformance_json

    manifest = create_release_package(
        root,
        build_dir,
        out_dir,
        package_name,
        args.full_size,
        rp2040_artifact_root=rp2040_artifact_root,
        meshcore_conformance_json=meshcore_conformance_json,
    )
    print(json.dumps(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
