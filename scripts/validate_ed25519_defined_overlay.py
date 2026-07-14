#!/usr/bin/env python3
"""Validate SIGUI's defined-arithmetic overlay for pinned orlp Ed25519.

The overlay keeps the MeshCore gitlink untouched.  It is mechanically derived
from three hash-pinned sources and changes only signed left-shift expressions
that can receive negative values (plus normalized signed packing limbs).  This
validator compiles the original and overlay implementations separately, runs
independent RFC 8032 known-answer tests, compares a deterministic output corpus
byte-for-byte, and can require exception-free Clang ASan/UBSan coverage.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]
UPSTREAM_COMMIT = "e8d3c53ba1ea863937081cd0caad759b832f3028"
UPSTREAM_ROOT = ROOT / "third_party" / "MeshCore" / "lib" / "ed25519"
OVERLAY_ROOT = ROOT / "overlays" / "meshcore_ed25519_defined"
DRIVER = ROOT / "tests" / "ed25519_defined_overlay" / "driver.c"
SOURCE_HASHES = {
    "fe.c": "71082937da43def6ecaf7811c23410bd8ea763969dfec67383f8129b12b1926d",
    "ge.c": "c3bb834817edea83d843828a75af19867b93144d15140eadfd9784d40cf11409",
    "sc.c": "c1bdb840416b34e79ceb7472d1a4ac4b1407c47b4c982cad981c72142ef42407",
}
EXPECTED_TRANSFORM_COUNTS = {"fe.c": 74, "ge.c": 6, "sc.c": 135}
COMMON_SOURCES = (
    "add_scalar.c",
    "key_exchange.c",
    "keypair.c",
    "sha512.c",
    "sign.c",
    "verify.c",
)
FORBIDDEN_SANITIZER_PREFIX = "-fno-sanitize="


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_source_bytes(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def overlay_header(name: str) -> str:
    return (
        "/*\n"
        " * ALTERED-FROM: MeshCore commit "
        f"{UPSTREAM_COMMIT}, lib/ed25519/{name}\n"
        f" * ORIGINAL-SHA256: {SOURCE_HASHES[name]}\n"
        " * ALTERATION: SIGUI replaces signed left shifts with defined "
        "multiplication by checked-in powers of two and normalizes trailing "
        "spaces.\n"
        " * LICENSE: zlib license preserved verbatim in license.txt.\n"
        " * This altered source is plainly marked and is not represented as "
        "the original.\n"
        " */\n"
    )


def _sub_exact(
    text: str,
    pattern: str,
    replacement: str | Callable[[re.Match[str]], str],
    expected: int,
    label: str,
) -> tuple[str, int]:
    result, count = re.subn(pattern, replacement, text)
    if count != expected:
        raise ValueError(f"{label}: expected {expected} replacements, got {count}")
    return result, count


def _literal_power(match: re.Match[str]) -> str:
    value, exponent = match.group(1), int(match.group(2))
    # Widen before multiplication: packing limbs can be positive yet large
    # enough that multiplication in their original int32_t type would overflow.
    return f"((int64_t){value} * {1 << exponent})"


def transform_source(name: str, original: str) -> tuple[str, int]:
    """Return the exact checked-in overlay text and transformation count."""

    text = original.replace("\r\n", "\n")
    count = 0
    if name == "fe.c":
        text, changed = _sub_exact(
            text,
            r"\b(carry\d+) << (25|26)\b",
            _literal_power,
            66,
            "fe carry shifts",
        )
        count += changed
        text, changed = _sub_exact(
            text,
            r"\b(h\d+) << ([1-7])\b",
            _literal_power,
            8,
            "fe packing shifts",
        )
        count += changed
    elif name == "ge.c":
        insertion = (
            '#include "precomp_data.h"\n\n'
            "/* Checked-in powers for slide()'s bounded exponent b in [1, 6]. */\n"
            "static const int slide_pow2[7] = {1, 2, 4, 8, 16, 32, 64};\n"
        )
        text, changed = _sub_exact(
            text,
            r'#include "precomp_data\.h"\n',
            insertion,
            1,
            "ge power table insertion",
        )
        # The table is metadata supporting the four expression rewrites, not
        # itself an altered signed-shift expression.
        text, changed = _sub_exact(
            text,
            r"r\[i \+ b\] << b",
            "r[i + b] * slide_pow2[b]",
            4,
            "ge slide shifts",
        )
        count += changed
        text, changed = _sub_exact(
            text,
            r"\(\(\(-bnegative\) & b\) << 1\)",
            "(((-bnegative) & b) * 2)",
            1,
            "ge absolute-digit shift",
        )
        count += changed
        text, changed = _sub_exact(
            text,
            r"\bcarry << 4\b",
            "carry * 16",
            1,
            "ge carry shift",
        )
        count += changed
    elif name == "sc.c":
        text, changed = _sub_exact(
            text,
            r"\b(carry\d+) << (21)\b",
            _literal_power,
            115,
            "sc carry shifts",
        )
        count += changed
        text, changed = _sub_exact(
            text,
            r"\b(s\d+) << ([1-7])\b",
            _literal_power,
            20,
            "sc packing shifts",
        )
        count += changed
    else:
        raise ValueError(f"unsupported overlay source: {name}")

    if count != EXPECTED_TRANSFORM_COUNTS[name]:
        raise ValueError(
            f"{name}: expected {EXPECTED_TRANSFORM_COUNTS[name]} total "
            f"transformations, got {count}"
        )
    had_terminal_newline = text.endswith("\n")
    text = "\n".join(line.rstrip(" \t") for line in text.splitlines())
    if had_terminal_newline:
        text += "\n"
    return overlay_header(name) + text, count


def validate_checked_in_overlay() -> dict[str, object]:
    source_receipts: dict[str, object] = {}
    for name, expected_hash in SOURCE_HASHES.items():
        upstream = canonical_source_bytes(UPSTREAM_ROOT / name)
        actual_hash = sha256_bytes(upstream)
        if actual_hash != expected_hash:
            raise RuntimeError(
                f"pinned upstream drift for {name}: {actual_hash} != {expected_hash}"
            )
        expected_overlay, count = transform_source(name, upstream.decode("utf-8"))
        actual_overlay = canonical_source_bytes(OVERLAY_ROOT / name)
        if actual_overlay != expected_overlay.encode("utf-8"):
            raise RuntimeError(f"checked-in overlay drift for {name}")
        source_receipts[name] = {
            "upstream_sha256": actual_hash,
            "overlay_sha256": sha256_bytes(actual_overlay),
            "transformed_expressions": count,
        }

    if (OVERLAY_ROOT / "license.txt").read_bytes() != (
        UPSTREAM_ROOT / "license.txt"
    ).read_bytes():
        raise RuntimeError("overlay zlib license is not a verbatim preserved copy")
    return source_receipts


def compiler_identity(compiler: str) -> str:
    result = subprocess.run(
        [compiler, "--version"], check=True, capture_output=True, text=True
    )
    return result.stdout.splitlines()[0].strip()


def compile_driver(
    compiler: str,
    output: Path,
    overlay: bool,
    sanitizer: bool,
) -> list[str]:
    flags = [
        "-std=c11",
        "-O1" if sanitizer else "-O2",
        "-g",
        "-fno-omit-frame-pointer",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-DED25519_NO_SEED",
        f"-I{UPSTREAM_ROOT}",
    ]
    if sanitizer:
        flags.extend(
            [
                "-fsanitize=address,undefined",
                "-fno-sanitize-recover=all",
            ]
        )
    arithmetic_root = OVERLAY_ROOT if overlay else UPSTREAM_ROOT
    sources = [
        arithmetic_root / "fe.c",
        arithmetic_root / "ge.c",
        arithmetic_root / "sc.c",
        *(UPSTREAM_ROOT / name for name in COMMON_SOURCES),
        DRIVER,
    ]
    command = [compiler, *flags, *(str(path) for path in sources), "-o", str(output)]
    if any(argument.startswith(FORBIDDEN_SANITIZER_PREFIX) for argument in command):
        raise RuntimeError("sanitizer exception flag present in overlay build")
    subprocess.run(command, cwd=ROOT, check=True)
    return command


def run_driver(executable: Path, sanitizer: bool = False) -> bytes:
    env = os.environ.copy()
    if sanitizer:
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    result = subprocess.run(
        [str(executable)],
        cwd=ROOT,
        env=env,
        check=False,
        capture_output=True,
        timeout=120,
    )
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace")
        raise RuntimeError(
            f"{executable.name} failed with {result.returncode}: {stderr}"
        )
    if result.stderr:
        raise RuntimeError(
            f"{executable.name} emitted unexpected stderr: "
            f"{result.stderr.decode('utf-8', errors='replace')}"
        )
    return result.stdout


def choose_compiler(requested: str | None) -> str:
    if requested:
        resolved = shutil.which(requested)
        if not resolved:
            raise RuntimeError(f"compiler not found: {requested}")
        return resolved
    for candidate in ("clang-18", "clang", "gcc", "cc"):
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise RuntimeError("no C compiler found")


def validate(args: argparse.Namespace) -> dict[str, object]:
    source_receipts = validate_checked_in_overlay()
    compiler = choose_compiler(args.cc)
    identity = compiler_identity(compiler)
    is_clang = "clang" in identity.lower()
    if args.sanitizers == "required" and not is_clang:
        raise RuntimeError(
            f"exception-free sanitizer proof requires Clang, got: {identity}"
        )
    run_sanitizers = args.sanitizers == "required" or (
        args.sanitizers == "auto" and is_clang
    )

    with tempfile.TemporaryDirectory(prefix="sigui-ed25519-defined-") as temp:
        build_root = Path(temp)
        suffix = ".exe" if os.name == "nt" else ""
        baseline_exe = build_root / f"baseline{suffix}"
        overlay_exe = build_root / f"overlay{suffix}"
        sanitizer_exe = build_root / f"overlay-sanitized{suffix}"

        baseline_command = compile_driver(compiler, baseline_exe, False, False)
        overlay_command = compile_driver(compiler, overlay_exe, True, False)
        baseline_output = run_driver(baseline_exe)
        overlay_output = run_driver(overlay_exe)
        if baseline_output != overlay_output:
            raise RuntimeError("baseline and overlay output corpora differ")

        sanitizer_command: list[str] | None = None
        if run_sanitizers:
            sanitizer_command = compile_driver(compiler, sanitizer_exe, True, True)
            sanitizer_output = run_driver(sanitizer_exe, sanitizer=True)
            if sanitizer_output != overlay_output:
                raise RuntimeError("sanitized and normal overlay output corpora differ")

    command_flags = {
        "baseline": baseline_command[1 : baseline_command.index(str(DRIVER))],
        "overlay": overlay_command[1 : overlay_command.index(str(DRIVER))],
        "sanitized_overlay": (
            sanitizer_command[1 : sanitizer_command.index(str(DRIVER))]
            if sanitizer_command
            else None
        ),
    }
    return {
        "schema_version": 1,
        "upstream": {
            "repository": "https://github.com/meshcore-dev/MeshCore",
            "commit": UPSTREAM_COMMIT,
            "gitlink_unchanged": True,
        },
        "sources": source_receipts,
        "transformed_expressions_total": sum(EXPECTED_TRANSFORM_COUNTS.values()),
        "known_answer_tests": {
            "RFC8032_section_7_1_test_1_empty_message": "passed",
            "RFC8032_section_7_1_test_2_one_byte_message": "passed",
        },
        "differential": {
            "deterministic_cases": 256,
            "byte_for_byte_equal": True,
            "output_bytes": len(overlay_output),
            "output_sha256": sha256_bytes(overlay_output),
        },
        "compiler": {"path": compiler, "identity": identity},
        "sanitizers": {
            "requested": args.sanitizers,
            "executed": run_sanitizers,
            "checks": ["address", "undefined"] if run_sanitizers else [],
            "exception_flags": [],
            "passed": True if run_sanitizers else None,
            "skip_reason": None if run_sanitizers else "Clang sanitizers not requested/available",
        },
        "compile_flags": command_flags,
        "passed": True,
    }


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="C compiler executable (default: auto-detect)")
    parser.add_argument(
        "--sanitizers",
        choices=("auto", "required", "off"),
        default="auto",
        help="require Clang ASan/UBSan, auto-run with Clang, or disable",
    )
    parser.add_argument("--out", type=Path, help="optional JSON receipt path")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        receipt = validate(args)
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as exc:
        print(f"ed25519 defined-overlay validation failed: {exc}", file=sys.stderr)
        return 1
    payload = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(payload, encoding="utf-8", newline="\n")
    print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
