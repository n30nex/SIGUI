#!/usr/bin/env python3
"""Evidence-based public release gate audit for MeshCore DeskOS D1L."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from verify_checksums import verify_sha256_manifest
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.verify_checksums import verify_sha256_manifest


REQUIRED_SCROLL_SCREENS = {"messages", "nodes", "packets", "settings", "map"}
REQUIRED_NOTICE_FILES = {
    "notices/LICENSE",
    "notices/THIRD_PARTY_NOTICES.md",
    "notices/ATTRIBUTIONS.md",
    "notices/SOURCE_AUDIT_AND_ATTRIBUTION.md",
}
FULL_SOAK_SECONDS = 12 * 60 * 60
TOP_LEVEL_COMMIT_FIELDS = (
    "commit",
    "commit_sha",
    "commitSha",
    "head_sha",
    "headSha",
    "head_sha256",
    "git_commit",
    "gitCommit",
    "git_sha",
    "gitSha",
    "source_commit",
    "sourceCommit",
    "source_head_sha",
    "sourceHeadSha",
    "firmware_commit",
    "firmwareCommit",
    "firmware_head_sha",
    "firmwareHeadSha",
    "firmware_git_sha",
    "firmwareGitSha",
    "build_commit",
    "buildCommit",
    "build_head_sha",
    "buildHeadSha",
    "workflow_sha",
    "workflowSha",
    "github_sha",
    "githubSha",
)
NESTED_COMMIT_FIELDS = (
    "commit",
    "commit_sha",
    "commitSha",
    "head_sha",
    "headSha",
    "head_sha256",
    "git_commit",
    "gitCommit",
    "git_sha",
    "gitSha",
    "source_commit",
    "sourceCommit",
    "source_head_sha",
    "sourceHeadSha",
    "firmware_commit",
    "firmwareCommit",
    "firmware_head_sha",
    "firmwareHeadSha",
    "firmware_git_sha",
    "firmwareGitSha",
    "build_commit",
    "buildCommit",
    "build_head_sha",
    "buildHeadSha",
)
COMMIT_METADATA_CONTAINERS = ("git", "artifact", "firmware", "build", "source", "workflow", "github", "metadata")
GENERIC_SHA_COMMIT_CONTAINERS = {"git", "workflow", "github"}


def default_d1l_port() -> str:
    return "COM" + "12"


def default_meshbot_port() -> str:
    return "COM" + "11"


def default_hardware_dir() -> str:
    return str(Path("artifacts") / "hardware" / default_d1l_port().lower())


@dataclass
class GateResult:
    gate_id: str
    severity: str
    ok: bool
    title: str
    evidence: list[str]
    message: str
    details: dict[str, Any] | None = None

    def to_dict(self) -> dict:
        payload = {
            "id": self.gate_id,
            "severity": self.severity,
            "ok": self.ok,
            "title": self.title,
            "evidence": self.evidence,
            "message": self.message,
        }
        if self.details:
            payload["details"] = self.details
        return payload


def read_json(path: Path | None) -> dict:
    if not path or not path.is_file():
        return {}
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def rel(path: Path, root: Path) -> str:
    try:
        return str(path.resolve().relative_to(root.resolve())).replace("\\", "/")
    except ValueError:
        return str(path)


def newest_file(root: Path, *patterns: str) -> Path | None:
    files: list[Path] = []
    for pattern in patterns:
        files.extend(path for path in root.glob(pattern) if path.is_file())
    if not files:
        return None
    return max(files, key=lambda path: path.stat().st_mtime)


def commit_file(root: Path, commit: str | None, *patterns: str) -> Path | None:
    if not commit:
        return newest_file(root, *patterns)
    short = commit[:7]
    commit_patterns = [pattern.replace("*", f"*{short}*", 1) for pattern in patterns]
    return newest_file(root, *commit_patterns)


def artifact_commit_values(data: dict) -> list[str]:
    if not isinstance(data, dict):
        return []
    values: list[Any] = [
        data.get(field)
        for field in TOP_LEVEL_COMMIT_FIELDS
    ]
    for container_name in COMMIT_METADATA_CONTAINERS:
        container = data.get(container_name)
        if isinstance(container, dict):
            values.extend(container.get(field) for field in NESTED_COMMIT_FIELDS)
            if container_name in GENERIC_SHA_COMMIT_CONTAINERS:
                values.append(container.get("sha"))
    return [value.strip() for value in values if isinstance(value, str) and value.strip()]


def commit_value_matches(value: str, commit: str) -> bool:
    full = commit.lower()
    short = full[:7]
    lowered = value.lower()
    return lowered == full or lowered.startswith(short)


def artifact_commit_matches(path: Path, data: dict, commit: str | None) -> bool:
    if not commit:
        return True
    metadata_values = artifact_commit_values(data)
    if metadata_values:
        return any(commit_value_matches(value, commit) for value in metadata_values)

    name = path.name.lower()
    full = commit.lower()
    short = full[:7]
    return short in name or full in name


def newest_commit_json(root: Path, commit: str | None, *patterns: str) -> Path | None:
    files: list[Path] = []
    for pattern in patterns:
        files.extend(path for path in root.glob(pattern) if path.is_file())
    for candidate in sorted(set(files), key=lambda path: path.stat().st_mtime, reverse=True):
        if artifact_commit_matches(candidate, read_json(candidate), commit):
            return candidate
    return None


def find_release_package(github_run_dir: Path | None) -> Path | None:
    if not github_run_dir:
        return None
    package_root = github_run_dir / "d1l-release-package"
    if not package_root.is_dir():
        return None
    candidates = [path for path in package_root.iterdir() if path.is_dir() and path.name.startswith("d1l-release-")]
    if not candidates:
        return None
    return max(candidates, key=lambda path: path.stat().st_mtime)


def checksum_gate(github_run_dir: Path | None, root: Path) -> GateResult:
    manifests: list[Path] = []
    labels = [
        "d1l-firmware-artifacts/SHA256SUMS.txt",
        "rp2040-sd-bridge-firmware/SHA256SUMS.txt",
    ]
    if github_run_dir:
        manifests.extend(github_run_dir / label for label in labels)
        package = find_release_package(github_run_dir)
        if package:
            manifests.append(package / "SHA256SUMS.txt")
    present = [path for path in manifests if path.is_file()]
    missing = [path for path in manifests if not path.is_file()]
    failed = [path for path in present if not verify_sha256_manifest(path)]
    ok = bool(present) and not missing and not failed and len(present) == 3
    return GateResult(
        "ci_artifacts_checksums",
        "P0",
        ok,
        "GitHub Actions artifacts and checksums",
        [rel(path, root) for path in present],
        "All required downloaded Actions checksum manifests verify."
        if ok else "Downloaded Actions artifacts are missing or have a checksum failure.",
        {
            "missing": [rel(path, root) for path in missing],
            "failed": [rel(path, root) for path in failed],
        },
    )


def notices_gate(github_run_dir: Path | None, root: Path) -> GateResult:
    package = find_release_package(github_run_dir) if github_run_dir else None
    manifest = read_json(package / "manifest.json" if package else None)
    notice_entries = manifest.get("notice_files") if isinstance(manifest.get("notice_files"), list) else []
    listed = {entry.get("path") for entry in notice_entries if isinstance(entry, dict)}
    existing = {path for path in REQUIRED_NOTICE_FILES if package and (package / path).is_file()}
    missing = sorted(REQUIRED_NOTICE_FILES - listed)
    missing_files = sorted(REQUIRED_NOTICE_FILES - existing)
    ok = package is not None and not missing and not missing_files
    evidence = [rel(package / path, root) for path in sorted(existing)] if package else []
    if package and (package / "manifest.json").is_file():
        evidence.append(rel(package / "manifest.json", root))
    return GateResult(
        "release_notices_included",
        "P0",
        ok,
        "Third-party notices and attributions packaged",
        evidence,
        "Release package includes all required notice and attribution documents."
        if ok else "Release package is missing one or more required notice/attribution files.",
        {"missing_manifest_entries": missing, "missing_files": missing_files},
    )


def simple_json_ok_gate(
    gate_id: str,
    title: str,
    path: Path | None,
    root: Path,
    predicate,
    success: str,
    failure: str,
    severity: str = "P0",
) -> GateResult:
    data = read_json(path)
    ok = bool(path and data and predicate(data))
    return GateResult(
        gate_id,
        severity,
        ok,
        title,
        [rel(path, root)] if path else [],
        success if ok else failure,
        {"path_found": bool(path), "artifact_ok": data.get("ok") if data else None},
    )


def all_checks_true(data: dict) -> bool:
    checks = data.get("checks")
    return isinstance(checks, dict) and bool(checks) and all(value is True for value in checks.values())


def ui_tab_abuse_ok(data: dict, expected_port: str) -> bool:
    return (
        data.get("ok") is True
        and data.get("port") == expected_port
        and int(data.get("cycles") or 0) >= 100
        and int(data.get("failure_count") or 0) == 0
    )


def scroll_probe_ok(data: dict, expected_port: str) -> bool:
    screens = set(data.get("screens") or [])
    return (
        data.get("ok") is True
        and data.get("port") == expected_port
        and int(data.get("failure_count") or 0) == 0
        and REQUIRED_SCROLL_SCREENS.issubset(screens)
    )


def dm_probe_ok(data: dict, expected_port: str, expected_bot_port: str) -> bool:
    return (
        data.get("ok") is True
        and data.get("port") == expected_port
        and data.get("meshbot_expected_port") == expected_bot_port
        and data.get("public_rf_transmit") is False
        and all_checks_true(data)
    )


def sd_gate(preflight_path: Path | None, root: Path) -> GateResult:
    preflight = read_json(preflight_path)
    classification = preflight.get("classification") if isinstance(preflight.get("classification"), dict) else {}
    ready = preflight.get("ready_for_sd_acceptance") is True and classification.get("storage_file_gate_ready") is True
    ok = bool(preflight_path and preflight.get("ok") is True and ready)
    details = {
        "ready_for_sd_acceptance": preflight.get("ready_for_sd_acceptance"),
        "classification": classification,
        "candidate_volume_count": len(preflight.get("candidate_volumes") or []),
        "artifact_sha256": (preflight.get("artifact") or {}).get("sha256"),
    }
    return GateResult(
        "sd_acceptance_matrix",
        "P0",
        ok,
        "SD auto-prepare, retained stores, exports, map tiles, and reboot/remount",
        [rel(preflight_path, root)] if preflight_path else [],
        "SD preflight is ready for acceptance matrix; run and attach the full matrix artifacts."
        if ok else "SD matrix is not release-ready; RP2040/SD file gate or follow-up matrix evidence is missing.",
        details,
    )


def full_soak_ok(data: dict) -> bool:
    summary = data.get("summary") if isinstance(data.get("summary"), dict) else {}
    return (
        data.get("ok") is True
        and data.get("mode") == "hardware"
        and float(data.get("duration_sec") or 0) >= FULL_SOAK_SECONDS
        and summary.get("ok") is True
        and not summary.get("threshold_failures")
        and summary.get("board_ready_all") is True
        and summary.get("ui_ready_all") is True
        and summary.get("uptime_monotonic") is True
    )


def full_soak_gate(soak_root: Path, root: Path, commit: str | None) -> GateResult:
    candidates = [
        path for path in sorted(soak_root.glob("*.json"), key=lambda path: path.stat().st_mtime, reverse=True)
        if artifact_commit_matches(path, read_json(path), commit)
    ]
    for candidate in candidates:
        data = read_json(candidate)
        if full_soak_ok(data):
            return GateResult(
                "full_duration_idle_soak",
                "P0",
                True,
                "12-hour idle/listening soak",
                [rel(candidate, root)],
                "12-hour idle/listening soak artifact passes.",
                {"duration_sec": data.get("duration_sec")},
            )
    newest = candidates[0] if candidates else None
    newest_data = read_json(newest)
    return GateResult(
        "full_duration_idle_soak",
        "P0",
        False,
        "12-hour idle/listening soak",
        [rel(newest, root)] if newest else [],
        "No passing 12-hour idle/listening hardware soak artifact was found.",
        {"latest_duration_sec": newest_data.get("duration_sec") if newest_data else None},
    )


def manual_evidence_gate(hardware_dir: Path, root: Path, commit: str | None) -> GateResult:
    review = newest_commit_json(hardware_dir, commit, "manual_touch_review*.json", "*manual*ui*review*.json")
    photos = [path for pattern in ("photos/*", "screen_photos/*", "*screen_photo*") for path in hardware_dir.glob(pattern) if path.is_file()]
    data = read_json(review)
    ok = bool(review and data.get("ok") is True and photos)
    evidence = []
    if review:
        evidence.append(rel(review, root))
    evidence.extend(rel(path, root) for path in sorted(photos)[:10])
    return GateResult(
        "manual_physical_ui_review",
        "P0",
        ok,
        "Manual physical UI/touch review and screen photos",
        evidence,
        "Manual physical UI/touch review and screen photos are present."
        if ok else "Manual physical UI/touch review and physical screen photos are still missing.",
        {"photo_count": len(photos), "review_found": bool(review)},
    )


def full_rf_gate(hardware_dir: Path, root: Path, commit: str | None) -> GateResult:
    candidates = [
        newest_commit_json(hardware_dir, commit, "rf_full_acceptance*.json", "*ack_path*.json", "*direct_route*.json", "*inbound_dm*.json")
    ]
    evidence_paths = [path for path in candidates if path]
    passing = False
    for path in evidence_paths:
        data = read_json(path)
        checks = data.get("checks") if isinstance(data.get("checks"), dict) else {}
        passing = data.get("ok") is True and all(
            checks.get(name) is True
            for name in ("inbound_dm", "ack_path", "direct_route")
        )
        if passing:
            break
    return GateResult(
        "full_rf_dm_acceptance",
        "P0",
        passing,
        "Inbound DM, ACK/PATH, and direct-route RF proof",
        [rel(path, root) for path in evidence_paths],
        "Full RF/DM acceptance evidence is present."
        if passing else "Only partial RF/DM evidence is present; inbound DM, ACK/PATH, and direct-route proof remain open.",
        {"candidate_count": len(evidence_paths)},
    )


def docs_freshness_gate(root: Path, commit: str | None, run_id: str | None) -> GateResult:
    docs = [root / "docs" / name for name in ("DESKOSFINAL.md", "ROADMAP.md", "RELEASE_CHECKLIST.md", "KNOWN_LIMITATIONS.md")]
    texts = {path: path.read_text(encoding="utf-8") for path in docs if path.is_file()}
    del commit, run_id
    combined = "\n".join(texts.values())
    needle_values = [
        "release_gate_audit_d1l.py",
        "ready_for_public_release=false",
        "No release tag should be cut until",
    ]
    missing = []
    for needle in needle_values:
        if needle not in combined:
            missing.append(needle)
    ok = not missing and len(texts) == len(docs)
    return GateResult(
        "docs_current_evidence",
        "P1",
        ok,
        "Release docs include fail-closed audit gate",
        [rel(path, root) for path in texts],
        "Release docs include the fail-closed audit gate and no-release-tag policy."
        if ok else "Release docs do not yet include the fail-closed audit gate and no-release-tag policy.",
        {"missing_needles": missing},
    )


def build_audit(args: argparse.Namespace) -> dict:
    root = Path(args.root).resolve()
    if args.github_run_id and not args.github_run_dir:
        args.github_run_dir = str(root / "artifacts" / "github" / args.github_run_id)
    github_run_dir = Path(args.github_run_dir).resolve() if args.github_run_dir else None
    hardware_dir = Path(args.hardware_dir).resolve()
    soak_dir = Path(args.soak_dir).resolve()
    gates: list[GateResult] = []

    gates.append(checksum_gate(github_run_dir, root))
    gates.append(notices_gate(github_run_dir, root))
    gates.append(
        simple_json_ok_gate(
            "com12_smoke",
            "D1L hardware smoke",
            newest_commit_json(hardware_dir, args.commit, "smoke_*.json"),
            root,
            lambda data: data.get("ok") is True and data.get("port") == args.d1l_port,
            "Current-commit D1L smoke artifact passes.",
            "No passing current-commit D1L smoke artifact was found.",
        )
    )
    gates.append(
        simple_json_ok_gate(
            "ui_tab_abuse",
            "100-cycle D1L tab abuse",
            newest_commit_json(hardware_dir, args.commit, "ui_tab_abuse_*.json"),
            root,
            lambda data: ui_tab_abuse_ok(data, args.d1l_port),
            "100-cycle D1L tab abuse artifact passes.",
            "No passing 100-cycle D1L tab abuse artifact was found.",
        )
    )
    gates.append(
        simple_json_ok_gate(
            "ui_scroll_probe",
            "D1L scroll probe",
            newest_commit_json(hardware_dir, args.commit, "scroll_probe_*.json"),
            root,
            lambda data: scroll_probe_ok(data, args.d1l_port),
            "D1L scroll probe artifact passes.",
            "No passing D1L scroll probe artifact was found.",
        )
    )
    gates.append(
        simple_json_ok_gate(
            "outbound_dm_com11",
            "D1L outbound DM proof against meshbot",
            newest_commit_json(hardware_dir, args.commit, "dm_probe_*.json"),
            root,
            lambda data: dm_probe_ok(data, args.d1l_port, args.meshbot_port),
            "Outbound D1L-to-meshbot DM proof passes.",
            "No passing outbound D1L-to-meshbot DM proof artifact was found.",
        )
    )
    gates.append(sd_gate(newest_commit_json(hardware_dir, args.commit, "rp2040_preflight_*.json", "rp2040_sd_preflight_*.json"), root))
    gates.append(full_soak_gate(soak_dir, root, args.commit))
    gates.append(manual_evidence_gate(hardware_dir, root, args.commit))
    gates.append(full_rf_gate(hardware_dir, root, args.commit))
    gates.append(docs_freshness_gate(root, args.commit, args.github_run_id))

    p0_failed = [gate for gate in gates if gate.severity == "P0" and not gate.ok]
    failed = [gate for gate in gates if not gate.ok]
    return {
        "schema": 1,
        "mode": "release-gate-audit",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "root": str(root),
        "github_run_id": args.github_run_id,
        "github_run_dir": str(github_run_dir) if github_run_dir else None,
        "commit": args.commit,
        "hardware_dir": str(hardware_dir),
        "ready_for_public_release": not p0_failed,
        "p0_failed_count": len(p0_failed),
        "failed_count": len(failed),
        "gates": [gate.to_dict() for gate in gates],
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--github-run-id")
    parser.add_argument("--github-run-dir")
    parser.add_argument("--commit")
    parser.add_argument("--d1l-port", default=default_d1l_port())
    parser.add_argument("--meshbot-port", default=default_meshbot_port())
    parser.add_argument("--hardware-dir", default=default_hardware_dir())
    parser.add_argument("--soak-dir", default="artifacts/soak")
    parser.add_argument("--out")
    parser.add_argument("--fail-on-open-p0", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    if args.github_run_id and not args.github_run_dir:
        args.github_run_dir = str(Path(args.root) / "artifacts" / "github" / args.github_run_id)
    report = build_audit(args)
    text = json.dumps(report, indent=2, sort_keys=True)
    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text + "\n", encoding="utf-8")
    print(text)
    if args.fail_on_open_p0 and not report["ready_for_public_release"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
