#!/usr/bin/env python3
"""Validate and render the SIGUI production completion ledger.

The ledger is JSON-compatible YAML so this release-critical check has no
third-party parser dependency. JSON is a strict subset of YAML 1.2.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import subprocess
from pathlib import Path


VALID_STATES = (
    "blocked",
    "open",
    "in_progress",
    "host_green",
    "actions_green",
    "hardware_green",
    "merged",
    "released",
)
DEPENDENCY_GATES = ("merged", "proof_banked", "implementation_merged")
WORKING_DOC_STATES = ("working", "hardware_proven", "released")
SHA_RE = re.compile(r"^[0-9a-f]{40}$")
TEMPLATE_TOKEN_RE = re.compile(r"<[^>]+>")


class LedgerError(ValueError):
    """Raised when the ledger cannot be loaded."""


def load_ledger(path: Path) -> dict:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise LedgerError(
            "%s must be JSON-compatible YAML: %s" % (path, exc)
        ) from exc
    if not isinstance(payload, dict):
        raise LedgerError("ledger root must be an object")
    return payload


def _is_sha(value: object) -> bool:
    return isinstance(value, str) and SHA_RE.fullmatch(value) is not None


def _template_matches(template: str, filename: str) -> bool:
    cursor = 0
    pieces = []
    for match in TEMPLATE_TOKEN_RE.finditer(template):
        pieces.append(re.escape(template[cursor : match.start()]))
        pieces.append(r"[^/\\]+")
        cursor = match.end()
    pieces.append(re.escape(template[cursor:]))
    return re.fullmatch("".join(pieces), filename) is not None


def _valid_evidence(item: dict) -> list:
    rows = item.get("evidence", [])
    if not isinstance(rows, list):
        return []
    return [row for row in rows if isinstance(row, dict) and row.get("valid") is True]


def missing_required_evidence(item: dict) -> list:
    required = item.get("required_evidence", [])
    filenames = [
        row.get("filename")
        for row in _valid_evidence(item)
        if isinstance(row.get("filename"), str)
    ]
    return [
        template
        for template in required
        if not any(_template_matches(template, filename) for filename in filenames)
    ]


def dependency_satisfied(item: dict) -> bool:
    gate = item.get("dependency_gate", "merged")
    if gate == "proof_banked":
        return item.get("proof_banked") is True and item.get("status") in (
            "hardware_green",
            "merged",
            "released",
        )
    if gate == "implementation_merged":
        return item.get("implementation_merged") is True and item.get("status") in (
            "in_progress",
            "host_green",
            "actions_green",
            "hardware_green",
            "merged",
            "released",
        )
    return item.get("implementation_merged") is True and item.get("status") in (
        "merged",
        "released",
    )


def open_blocker_ids(ledger: dict) -> set:
    return {
        row.get("id")
        for row in ledger.get("blockers", [])
        if (
            isinstance(row, dict)
            and row.get("status") == "open"
            and row.get("blocks_execution", True) is True
        )
    }


def runnable_work_packages(ledger: dict) -> list:
    packages = {
        row.get("id"): row
        for row in ledger.get("work_packages", [])
        if isinstance(row, dict)
    }
    blockers = open_blocker_ids(ledger)
    result = []
    for package_id in ledger.get("execution_priority", []):
        item = packages.get(package_id)
        if (
            not item
            or item.get("status") in ("blocked", "merged", "released")
            or dependency_satisfied(item)
        ):
            continue
        if any(blocker in blockers for blocker in item.get("blockers", [])):
            continue
        dependencies = [packages.get(dep) for dep in item.get("depends_on", [])]
        if all(dependency is not None and dependency_satisfied(dependency) for dependency in dependencies):
            result.append(package_id)
    return result


def validate_ledger(ledger: dict) -> list:
    errors = []
    if ledger.get("schema_version") != 1:
        errors.append("schema_version must be 1")

    repository = ledger.get("repository", {})
    for label, value in (
        ("repository.main.commit", repository.get("main", {}).get("commit")),
        ("repository.pinned_meshcore", repository.get("pinned_meshcore")),
    ):
        if not _is_sha(value):
            errors.append("%s must be a full lowercase 40-character SHA" % label)

    packages_list = ledger.get("work_packages", [])
    if not isinstance(packages_list, list):
        return errors + ["work_packages must be a list"]
    packages = {}
    for index, item in enumerate(packages_list):
        if not isinstance(item, dict):
            errors.append("work_packages[%d] must be an object" % index)
            continue
        package_id = item.get("id")
        if not isinstance(package_id, str) or not package_id:
            errors.append("work_packages[%d].id must be a non-empty string" % index)
            continue
        if package_id in packages:
            errors.append("duplicate work package id %s" % package_id)
            continue
        packages[package_id] = item

    blocker_rows = ledger.get("blockers", [])
    blockers = {}
    if not isinstance(blocker_rows, list):
        errors.append("blockers must be a list")
        blocker_rows = []
    for index, blocker in enumerate(blocker_rows):
        if not isinstance(blocker, dict) or not isinstance(blocker.get("id"), str):
            errors.append("blockers[%d] must be an object with an id" % index)
            continue
        blocker_id = blocker["id"]
        if blocker_id in blockers:
            errors.append("duplicate blocker id %s" % blocker_id)
        blockers[blocker_id] = blocker
        if blocker.get("status") not in ("open", "closed"):
            errors.append("%s has unknown blocker status %r" % (blocker_id, blocker.get("status")))
        if blocker.get("work_package") not in packages:
            errors.append("%s names unknown work package %r" % (blocker_id, blocker.get("work_package")))
        if not isinstance(blocker.get("blocks_execution"), bool):
            errors.append("%s blocks_execution must be boolean" % blocker_id)

    for package_id, item in packages.items():
        status = item.get("status")
        if status not in VALID_STATES:
            errors.append("%s has unknown status %r" % (package_id, status))
        gate = item.get("dependency_gate", "merged")
        if gate not in DEPENDENCY_GATES:
            errors.append("%s has unknown dependency_gate %r" % (package_id, gate))
        if not isinstance(item.get("implementation_merged"), bool):
            errors.append("%s implementation_merged must be boolean" % package_id)
        if not isinstance(item.get("proof_banked"), bool):
            errors.append("%s proof_banked must be boolean" % package_id)
        depends_on = item.get("depends_on", [])
        if not isinstance(depends_on, list):
            errors.append("%s.depends_on must be a list" % package_id)
            depends_on = []
        for dependency in depends_on:
            if dependency == package_id:
                errors.append("%s cannot depend on itself" % package_id)
            elif dependency not in packages:
                errors.append("%s depends on unknown work package %s" % (package_id, dependency))

        commit = item.get("implementation_commit")
        if commit is not None and not _is_sha(commit):
            errors.append("%s implementation_commit must be null or a full SHA" % package_id)
        evidence_commit = item.get("evidence_commit", commit)
        if evidence_commit is not None and not _is_sha(evidence_commit):
            errors.append("%s evidence_commit must be null or a full SHA" % package_id)

        evidence = item.get("evidence", [])
        if not isinstance(evidence, list):
            errors.append("%s.evidence must be a list" % package_id)
            evidence = []
        for evidence_index, row in enumerate(evidence):
            if not isinstance(row, dict):
                errors.append("%s.evidence[%d] must be an object" % (package_id, evidence_index))
                continue
            filename = row.get("filename")
            if not isinstance(filename, str) or not filename:
                errors.append("%s.evidence[%d] needs a filename" % (package_id, evidence_index))
            if not isinstance(row.get("valid"), bool):
                errors.append("%s evidence %r valid must be boolean" % (package_id, filename))
            row_commit = row.get("commit")
            if not _is_sha(row_commit):
                errors.append("%s evidence %r must carry a full SHA" % (package_id, filename))
            elif row.get("valid") is True and evidence_commit and row_commit != evidence_commit:
                errors.append(
                    "%s valid evidence %s is for %s, expected %s"
                    % (package_id, filename, row_commit, evidence_commit)
                )

        latest_receipt = item.get("latest_valid_receipt")
        if latest_receipt is not None:
            if not isinstance(latest_receipt, dict):
                errors.append("%s latest_valid_receipt must be null or an object" % package_id)
            else:
                latest_filename = latest_receipt.get("filename")
                latest_commit = latest_receipt.get("commit")
                valid_pairs = {
                    (row.get("filename"), row.get("commit")) for row in _valid_evidence(item)
                }
                if (latest_filename, latest_commit) not in valid_pairs:
                    errors.append("%s latest_valid_receipt is not present as valid evidence" % package_id)
                if evidence_commit and latest_commit != evidence_commit:
                    errors.append("%s latest_valid_receipt does not match evidence_commit" % package_id)

        blocker_refs = item.get("blockers", [])
        if not isinstance(blocker_refs, list):
            errors.append("%s.blockers must be a list" % package_id)
        else:
            for blocker_id in blocker_refs:
                blocker = blockers.get(blocker_id)
                if blocker is None:
                    errors.append("%s references unknown blocker %s" % (package_id, blocker_id))
                elif blocker.get("work_package") != package_id:
                    errors.append("%s references blocker %s owned by another package" % (package_id, blocker_id))

        missing = missing_required_evidence(item)
        if status in ("hardware_green", "merged", "released") and missing:
            errors.append("%s is %s but lacks evidence: %s" % (package_id, status, ", ".join(missing)))
        if item.get("proof_banked") is True:
            if status not in ("hardware_green", "merged", "released"):
                errors.append("%s proof_banked requires hardware_green, merged, or released" % package_id)
            if missing:
                errors.append("%s proof_banked but required evidence is incomplete" % package_id)
        if status == "hardware_green" and item.get("proof_banked") is not True:
            errors.append("%s hardware_green requires proof_banked=true" % package_id)
        if status in ("merged", "released") and item.get("implementation_merged") is not True:
            errors.append("%s is %s but implementation_merged is not true" % (package_id, status))

    for package_id, item in packages.items():
        if item.get("status") == "blocked":
            continue
        for dependency in item.get("depends_on", []):
            dependency_item = packages.get(dependency)
            if dependency_item and not dependency_satisfied(dependency_item):
                errors.append(
                    "%s is %s while dependency %s is not satisfied"
                    % (package_id, item.get("status"), dependency)
                )

    priority = ledger.get("execution_priority", [])
    if not isinstance(priority, list) or set(priority) != set(packages):
        errors.append("execution_priority must list every work package exactly once")
    elif len(priority) != len(set(priority)):
        errors.append("execution_priority contains duplicates")

    port_policy = ledger.get("port_policy", {})
    forbidden = {str(value).upper() for value in port_policy.get("forbidden", [])}
    required_forbidden = {"COM%s" % number for number in (8, 11, 29)}
    if not required_forbidden.issubset(forbidden):
        errors.append("port_policy.forbidden is missing a required forbidden port")
    defaults = port_policy.get("defaults", {})
    for environment_name in ("D1L_PORT", "RP2040_PORT", "MESH_PEER_PORT"):
        if environment_name not in defaults:
            errors.append("port_policy.defaults must declare %s" % environment_name)
        elif defaults.get(environment_name) is not None:
            errors.append("%s must not have an operational default" % environment_name)

    capabilities = ledger.get("capabilities", [])
    if not isinstance(capabilities, list):
        errors.append("capabilities must be a list")
    else:
        seen_capabilities = set()
        for index, item in enumerate(capabilities):
            if not isinstance(item, dict):
                errors.append("capabilities[%d] must be an object" % index)
                continue
            capability_id = item.get("id")
            if capability_id in seen_capabilities:
                errors.append("duplicate capability id %s" % capability_id)
            seen_capabilities.add(capability_id)
            if not isinstance(item.get("runtime_available"), bool):
                errors.append("capability %s runtime_available must be boolean" % capability_id)
            if (
                item.get("runtime_available") is False
                and item.get("documentation_status") in WORKING_DOC_STATES
            ):
                errors.append(
                    "capability %s is unavailable but documentation_status is %s"
                    % (capability_id, item.get("documentation_status"))
                )

    gates_list = ledger.get("release_gates", [])
    gates = {}
    if not isinstance(gates_list, list):
        errors.append("release_gates must be a list")
    else:
        for item in gates_list:
            if not isinstance(item, dict) or not isinstance(item.get("id"), str):
                errors.append("each release gate must be an object with an id")
                continue
            gate_id = item["id"]
            if gate_id in gates:
                errors.append("duplicate release gate id %s" % gate_id)
            gates[gate_id] = item
            if item.get("status") not in VALID_STATES:
                errors.append("%s has unknown status %r" % (gate_id, item.get("status")))
        for gate_id, item in gates.items():
            for dependency in item.get("depends_on", []):
                if dependency not in gates:
                    errors.append("%s depends on unknown release gate %s" % (gate_id, dependency))

    return errors


def _git_head(root: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=str(root), text=True
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def _now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def validation_report(ledger: dict, errors: list, repository_commit: str) -> dict:
    runnable = runnable_work_packages(ledger)
    return {
        "schema": 1,
        "artifact_type": "completion_ledger_validation",
        "generated_at": _now(),
        "repository_commit": repository_commit,
        "ledger_main_commit": ledger.get("repository", {}).get("main", {}).get("commit"),
        "passed": not errors,
        "error_count": len(errors),
        "errors": errors,
        "highest_priority_runnable": runnable[0] if runnable else None,
        "runnable_work_packages": runnable,
    }


def render_status(ledger: dict) -> str:
    repository = ledger["repository"]
    packages = ledger["work_packages"]
    package_by_id = {item["id"]: item for item in packages}
    runnable = runnable_work_packages(ledger)
    overall = next(
        (
            package_id
            for package_id in ledger["execution_priority"]
            if package_by_id[package_id]["status"] not in ("merged", "released")
            and not dependency_satisfied(package_by_id[package_id])
        ),
        "none",
    )

    lines = [
        "<!-- Generated by scripts/completion_ledger.py; edit docs/COMPLETION_LEDGER.yaml. -->",
        "# SIGUI Completion Status",
        "",
        "Snapshot: `%s`" % ledger["snapshot_at"],
        "",
        "- Live `main`: `%s`" % repository["main"]["commit"],
        "- Working branch: `%s` from `%s`"
        % (repository["working_branch"]["name"], repository["working_branch"]["base_commit"]),
        "- Integrated candidate: `%s`" % repository["integrated_candidate"]["commit"],
        "- Pinned MeshCore: `%s`" % repository["pinned_meshcore"],
        "- Selected ESP-IDF: `%s`" % repository["selected_esp_idf"],
        "- Release posture: **%s**" % ledger["release_posture"].replace("_", " "),
        "- Highest-priority pending: `%s`" % overall,
        "- Highest-priority runnable now: `%s`" % (runnable[0] if runnable else "none"),
        "",
        "## Work packages",
        "",
        "| Package | Status | Dependency gate | Proof banked | Blockers |",
        "|---|---|---|---:|---|",
    ]
    for package_id in ledger["execution_priority"]:
        item = package_by_id[package_id]
        blockers = ", ".join(item.get("blockers", [])) or "—"
        lines.append(
            "| `%s` %s | `%s` | `%s` | %s | %s |"
            % (
                package_id,
                item["title"],
                item["status"],
                item.get("dependency_gate", "merged"),
                "yes" if item.get("proof_banked") else "no",
                blockers,
            )
        )

    wp01 = package_by_id.get("WP-01")
    if wp01:
        actions = wp01.get("verification", {}).get("actions", {})
        checksums = wp01.get("verification", {}).get("checksums", {})
        physical = wp01.get("verification", {}).get("physical", {})
        wp02_dependency_note = (
            "`WP-02` is unlocked because WP-01 banked its required exact-pair "
            "hardware proof. The implementation may land on a successor SHA while "
            "the evidence remains explicitly bound to its proven source SHA."
            if dependency_satisfied(wp01)
            else "`WP-02` remains blocked until WP-01 banks its required exact-pair "
            "hardware proof."
        )
        lines.extend(
            [
                "",
                "## WP-01 exact-candidate checkpoint",
                "",
                "- Merged implementation SHA: `%s`" % wp01.get("implementation_commit"),
                "- Exact physical evidence SHA: `%s`"
                % wp01.get("evidence_commit", wp01.get("implementation_commit")),
                "- Execution state: **%s** (stage `%s`)"
                % (wp01.get("status"), actions.get("stage", "unknown")),
                "- Host suite: **%s**, %s tests"
                % (
                    wp01.get("verification", {}).get("host", {}).get("status", "unknown"),
                    wp01.get("verification", {}).get("host", {}).get("passed_tests", "unknown"),
                ),
                "- Actions: push `%s` and PR `%s` are **%s**"
                % (actions.get("push_run"), actions.get("pull_request_run"), actions.get("status")),
                "- Downloaded checksums: **%s** across %s manifests"
                % (checksums.get("status"), checksums.get("manifest_count")),
                "- Exact %s/%s pair installed: **%s**"
                % (
                    physical.get("d1l_port", "D1L port"),
                    physical.get("rp2040_port", "RP2040 port"),
                    "yes" if physical.get("exact_pair_installed") else "no",
                ),
                "- Partial physical passes: %s"
                % (", ".join(physical.get("passed", [])) or "none"),
                "- Physical gate: **%s** — %s"
                % (physical.get("status", "unknown"), "; ".join(physical.get("failed", [])) or "no failure recorded"),
                "- WDT/PANIC observed: **%s**"
                % ("yes" if physical.get("wdt_or_panic_observed") else "no"),
                "- Safety flags: `public_rf_tx=%s`, `formats_sd=%s`"
                % (
                    str(physical.get("public_rf_tx")).lower(),
                    str(physical.get("formats_sd")).lower(),
                ),
                "- Physical proof banked: **%s**" % ("yes" if wp01.get("proof_banked") else "no"),
                "- Merge state: **%s**"
                % ("merged" if wp01.get("implementation_merged") else "not merged"),
                "",
                wp02_dependency_note,
            ]
        )

    lines.extend(
        [
            "",
            "## Open blocker receipts",
            "",
            "| Receipt | Work package | Missing input | Required action |",
            "|---|---|---|---|",
        ]
    )
    for blocker in ledger.get("blockers", []):
        if blocker.get("status") != "open":
            continue
        lines.append(
            "| `%s` | `%s` | %s | %s |"
            % (
                blocker["id"],
                blocker["work_package"],
                blocker["missing_input"],
                blocker["operator_action"],
            )
        )

    lines.extend(
        [
            "",
            "## Release gates",
            "",
            "| Gate | Status |",
            "|---|---|",
        ]
    )
    for gate in ledger["release_gates"]:
        lines.append("| `%s` %s | `%s` |" % (gate["id"], gate["name"], gate["status"]))

    lines.extend(
        [
            "",
            "## Safety invariants",
            "",
            "- Firmware builds run only in GitHub Actions; downloaded artifacts require checksum verification.",
            "- `D1L_PORT`, `RP2040_PORT`, and `MESH_PEER_PORT` have no operational defaults.",
            "- Forbidden ports: %s."
            % ", ".join(ledger["port_policy"]["forbidden"]),
            "- SD cards are never formatted on-device.",
            "- Simulation, dry-run, source review, and predecessor-SHA evidence cannot close a physical gate.",
            "",
        ]
    )
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("validate", "generate"))
    parser.add_argument("--ledger", default="docs/COMPLETION_LEDGER.yaml")
    parser.add_argument("--status", default="docs/COMPLETION_STATUS.md")
    parser.add_argument("--out")
    parser.add_argument("--check-generated", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    ledger_path = Path(args.ledger)
    status_path = Path(args.status)
    try:
        ledger = load_ledger(ledger_path)
    except LedgerError as exc:
        print(str(exc))
        return 1

    errors = validate_ledger(ledger)
    expected_status = render_status(ledger) if not errors else None
    if args.command == "generate":
        if errors:
            for error in errors:
                print("ERROR %s" % error)
            return 1
        status_path.write_text(expected_status, encoding="utf-8", newline="\n")
        print("Wrote %s" % status_path)
        return 0

    if args.check_generated:
        actual_status = status_path.read_text(encoding="utf-8") if status_path.exists() else None
        if expected_status is None or actual_status != expected_status:
            errors.append("%s is stale; run completion_ledger.py generate" % status_path)

    root = Path(__file__).resolve().parents[1]
    repository_commit = _git_head(root)
    report = validation_report(ledger, errors, repository_commit)
    out_path = Path(args.out) if args.out else root / "artifacts" / "completion-ledger" / (
        "completion_ledger_validation_%s.json" % repository_commit
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    for error in errors:
        print("ERROR %s" % error)
    print("%s %s" % ("PASS" if not errors else "FAIL", out_path))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
