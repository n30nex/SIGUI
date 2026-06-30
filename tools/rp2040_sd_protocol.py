#!/usr/bin/env python3
"""Reference line-protocol simulator for the D1L RP2040 SD bridge."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, replace


STATUS_REQUEST = "DESKOS_SD_STATUS"
STATUS_REPLY = "DESKOS_SD_STATUS"
FORMAT_REQUEST = "DESKOS_SD_FORMAT"
FORMAT_REPLY = "DESKOS_SD_FORMAT"
FORMAT_CONFIRMATION = "FORMAT-DESKOS-SD"
STATUS_FIELDS = (
    "state",
    "present",
    "mounted",
    "deskos",
    "fs",
    "format_required",
    "format_supported",
    "capacity_kb",
    "free_kb",
    "note",
)


@dataclass(frozen=True)
class SdScenario:
    state: str
    present: bool
    mounted: bool
    deskos: bool
    fs: str
    format_required: bool
    format_supported: bool
    capacity_kb: int
    free_kb: int
    note: str


SCENARIOS: dict[str, SdScenario] = {
    "no-card": SdScenario(
        state="no_card",
        present=False,
        mounted=False,
        deskos=False,
        fs="none",
        format_required=False,
        format_supported=False,
        capacity_kb=0,
        free_kb=0,
        note="no_card",
    ),
    "ready": SdScenario(
        state="ready",
        present=True,
        mounted=True,
        deskos=True,
        fs="fat32",
        format_required=False,
        format_supported=True,
        capacity_kb=31166976,
        free_kb=31100000,
        note="ready",
    ),
    "format-required": SdScenario(
        state="setup_required",
        present=True,
        mounted=False,
        deskos=False,
        fs="unknown",
        format_required=True,
        format_supported=True,
        capacity_kb=31166976,
        free_kb=0,
        note="format_required",
    ),
    "root-missing": SdScenario(
        state="setup_required",
        present=True,
        mounted=True,
        deskos=False,
        fs="fat32",
        format_required=False,
        format_supported=True,
        capacity_kb=31166976,
        free_kb=31090000,
        note="deskos_root_missing",
    ),
}


def bool_token(value: bool) -> str:
    return "1" if value else "0"


def status_line(scenario: SdScenario, prefix: str = STATUS_REPLY) -> str:
    return (
        f"{prefix} state={scenario.state}"
        f" present={bool_token(scenario.present)}"
        f" mounted={bool_token(scenario.mounted)}"
        f" deskos={bool_token(scenario.deskos)}"
        f" fs={scenario.fs}"
        f" format_required={bool_token(scenario.format_required)}"
        f" format_supported={bool_token(scenario.format_supported)}"
        f" capacity_kb={scenario.capacity_kb}"
        f" free_kb={scenario.free_kb}"
        f" note={scenario.note}"
    )


def formatted_scenario(scenario: SdScenario) -> SdScenario:
    if not scenario.present:
        return replace(scenario, state="no_card", note="no_card")
    if not scenario.format_supported:
        return replace(scenario, state="unsupported", note="format_unsupported")
    return replace(
        scenario,
        state="ready",
        mounted=True,
        deskos=True,
        fs="fat32",
        format_required=False,
        free_kb=scenario.capacity_kb,
        note="format_complete",
    )


def reply_for_request(request: str, scenario: SdScenario) -> str:
    request = request.strip()
    if request == STATUS_REQUEST:
        return status_line(scenario)
    if request.startswith(FORMAT_REQUEST + " "):
        phrase = request[len(FORMAT_REQUEST) + 1 :].strip()
        if phrase != FORMAT_CONFIRMATION:
            refused = replace(scenario, state="confirmation_required", note="confirmation_required")
            return status_line(refused, FORMAT_REPLY)
        return status_line(formatted_scenario(scenario), FORMAT_REPLY)
    raise ValueError(f"unsupported request: {request}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), default="ready")
    parser.add_argument("--request", default=STATUS_REQUEST)
    parser.add_argument("--list-scenarios", action="store_true")
    args = parser.parse_args()

    if args.list_scenarios:
        for name in sorted(SCENARIOS):
            print(name)
        return 0

    print(reply_for_request(args.request, SCENARIOS[args.scenario]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
