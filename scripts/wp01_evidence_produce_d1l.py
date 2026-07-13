#!/usr/bin/env python3
"""Produce source-bound canonical evidence for autonomous WP-01 gates."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

try:
    from wp01_evidence_sources_d1l import (
        build_retained_reboot_matrix_artifact,
        build_sd_inserted_stability_artifact,
        build_storage_active_soak_artifact,
    )
except ImportError:  # pragma: no cover - package import path used by pytest
    from scripts.wp01_evidence_sources_d1l import (
        build_retained_reboot_matrix_artifact,
        build_sd_inserted_stability_artifact,
        build_storage_active_soak_artifact,
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--kind",
        required=True,
        choices=(
            "sd_inserted_stability",
            "retained_reboot_matrix",
            "storage_active_soak",
        ),
    )
    parser.add_argument("--root", default=".")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--github-actions-run", required=True)
    parser.add_argument("--d1l-port", required=True)
    parser.add_argument("--rp2040-port", required=True)
    parser.add_argument("--exact-pair-provenance", required=True)
    parser.add_argument("--source", action="append", required=True)
    parser.add_argument("--out")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = Path(args.root).resolve()
    provenance_path = Path(args.exact_pair_provenance).resolve()
    sources = [Path(value).resolve() for value in args.source]
    common = {
        "root": root,
        "commit": args.commit,
        "github_actions_run": args.github_actions_run,
        "d1l_port": args.d1l_port,
        "rp2040_port": args.rp2040_port,
    }
    if args.kind == "sd_inserted_stability":
        if len(sources) != 1:
            raise SystemExit("sd_inserted_stability requires exactly one --source")
        report = build_sd_inserted_stability_artifact(
            sources[0], provenance_path, **common
        )
        default_name = (
            f"sd_inserted_stability_{args.commit[:7]}_"
            f"{args.d1l_port.upper()}_{args.rp2040_port.upper()}.json"
        )
    elif args.kind == "retained_reboot_matrix":
        report = build_retained_reboot_matrix_artifact(
            sources, provenance_path, **common
        )
        default_name = (
            f"retained_reboot_matrix_{args.commit[:7]}_"
            f"{args.d1l_port.upper()}.json"
        )
    else:
        if len(sources) != 1:
            raise SystemExit("storage_active_soak requires exactly one --source")
        report = build_storage_active_soak_artifact(
            sources[0], provenance_path, **common
        )
        default_name = (
            f"storage_active_soak_{args.commit[:7]}_"
            f"{args.d1l_port.upper()}_{args.rp2040_port.upper()}.json"
        )
    out = (
        Path(args.out).resolve()
        if args.out
        else root / "artifacts" / "hardware" / "wp01" / default_name
    )
    try:
        out.relative_to(root)
    except ValueError as exc:
        raise SystemExit("output must remain within repository root") from exc
    protected_inputs = {provenance_path, *sources}
    if out in protected_inputs:
        raise SystemExit("output must not overwrite a source or provenance receipt")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps({"ok": report.get("ok"), "kind": args.kind, "out": str(out)}))
    return 0 if report.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
