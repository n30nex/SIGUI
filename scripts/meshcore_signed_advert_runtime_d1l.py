#!/usr/bin/env python3
"""Build and run the bounded pinned-MeshCore signed-advert semantic gate."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import tarfile
import tempfile
import urllib.request
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any, Sequence


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = (
    ROOT / "tests" / "meshcore_signed_advert_runtime" / "manifest.json"
)
HARNESS_PATH = (
    ROOT
    / "tests"
    / "meshcore_signed_advert_runtime"
    / "meshcore_signed_advert_runtime.cpp"
)
RNG_STUB_PATH = (
    ROOT / "tests" / "meshcore_signed_advert_runtime" / "crypto_rng_stub.cpp"
)

MESH_CPP_SOURCES = (
    ROOT / "third_party" / "MeshCore" / "src" / "Identity.cpp",
    ROOT / "third_party" / "MeshCore" / "src" / "Packet.cpp",
    ROOT / "third_party" / "MeshCore" / "src" / "Dispatcher.cpp",
    ROOT / "third_party" / "MeshCore" / "src" / "Mesh.cpp",
    ROOT / "third_party" / "MeshCore" / "src" / "Utils.cpp",
    ROOT
    / "third_party"
    / "MeshCore"
    / "src"
    / "helpers"
    / "AdvertDataHelpers.cpp",
    ROOT
    / "third_party"
    / "MeshCore"
    / "src"
    / "helpers"
    / "TxtDataHelpers.cpp",
    ROOT
    / "third_party"
    / "MeshCore"
    / "src"
    / "helpers"
    / "BaseChatMesh.cpp",
)
ED25519_ROOT = ROOT / "third_party" / "MeshCore" / "lib" / "ed25519"
ED25519_C_SOURCES = tuple(
    ED25519_ROOT / name
    for name in (
        "fe.c",
        "ge.c",
        "sc.c",
        "sha512.c",
        "verify.c",
        "key_exchange.c",
        "keypair.c",
        "sign.c",
    )
)
SHIFT_BASE_EXCEPTION_SOURCES = frozenset(ED25519_C_SOURCES[:3])
AES_SOURCE = (
    ROOT
    / "third_party"
    / "sensecap_indicator_esp32"
    / "components"
    / "LoRaWAN"
    / "soft-se"
    / "aes.c"
)
EXTERNAL_CPP_SOURCES = (
    "Crypto.cpp",
    "Hash.cpp",
    "SHA512.cpp",
    "BigNumberUtil.cpp",
    "Curve25519.cpp",
    "Ed25519.cpp",
)


class GateFailure(RuntimeError):
    """The semantic gate failed closed."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def git_output(*args: str) -> str:
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise GateFailure(f"git command failed: {' '.join(args)}") from exc
    return completed.stdout.strip()


def load_manifest(path: Path = MANIFEST_PATH) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateFailure(f"cannot read signed-advert runtime manifest: {exc}") from exc
    if manifest.get("schema_version") != 1:
        raise GateFailure("signed-advert runtime manifest schema drifted")
    if manifest.get("work_package") != "WP-04":
        raise GateFailure("signed-advert runtime work package drifted")
    if manifest.get("capability") != "identity_signed_advert_semantic_runtime":
        raise GateFailure("signed-advert runtime capability drifted")
    if manifest.get("wp04_closure_eligible") is not False:
        raise GateFailure("signed-advert runtime must not close WP-04")
    if manifest.get("closure_ready") is not False:
        raise GateFailure("signed-advert runtime must remain fail closed")
    if manifest.get("source_hash_mode") != "raw_file_sha256":
        raise GateFailure("signed-advert runtime source hash mode drifted")
    dependency = manifest.get("external_dependency", {})
    if (
        dependency.get("name") != "rweather/Crypto"
        or dependency.get("version") != "0.4.0"
        or dependency.get("registry_version_id") != 43204
        or dependency.get("release_commit")
        != "61e84b220fc8dfe83c2e04716d0fa88cfaddadf6"
        or dependency.get("archive_url")
        != "https://dl.registry.platformio.org/download/rweather/library/Crypto/0.4.0/Crypto-0.4.0.tar.gz"
        or dependency.get("archive_size") != 162696
        or dependency.get("archive_sha256")
        != "1867740aad0d61bdcbac25f6dbc8eefe6eed9e7b37f48d9d0b9d80500ad431e0"
    ):
        raise GateFailure("signed-advert external dependency pin drifted")
    sanitizer = manifest.get("sanitizer_policy", {})
    expected_exceptions = [
        str(path.relative_to(ROOT)).replace("\\", "/")
        for path in ED25519_C_SOURCES[:3]
    ]
    if (
        sanitizer.get("requested") != ["address", "undefined"]
        or sanitizer.get("full_ubsan_clean") is not False
        or sanitizer.get("shift_base_exceptions") != expected_exceptions
        or sanitizer.get("exception_flag") != "-fno-sanitize=shift-base"
        or sanitizer.get("release_blocker")
        != "BLK-WP04-ED25519-SHIFT-UB-20260714"
    ):
        raise GateFailure("signed-advert sanitizer policy drifted")
    return manifest


def _source_groups(manifest: dict[str, Any]) -> Sequence[dict[str, str]]:
    return (
        manifest["upstream"]["sources"],
        manifest["vendored_crypto_sources"],
        manifest["host_sources"],
    )


def verify_repository_sources(
    manifest: dict[str, Any], expected_commit: str | None = None
) -> dict[str, Any]:
    files: dict[str, Any] = {}
    failures: list[str] = []
    seen: set[str] = set()
    root = ROOT.resolve()
    for group in _source_groups(manifest):
        if not isinstance(group, dict):
            raise GateFailure("signed-advert source registry is not an object")
        for relative, expected_hash in group.items():
            if (
                not isinstance(relative, str)
                or "\\" in relative
                or relative in seen
                or not re.fullmatch(r"[A-Za-z0-9_./-]+", relative)
            ):
                raise GateFailure("signed-advert source path registry is invalid")
            seen.add(relative)
            source = (ROOT / relative).resolve()
            try:
                source.relative_to(root)
            except ValueError as exc:
                raise GateFailure(f"source escapes repository root: {relative}") from exc
            actual_hash = sha256_file(source) if source.is_file() else None
            matched = actual_hash == expected_hash
            files[relative] = {
                "expected_sha256": expected_hash,
                "actual_sha256": actual_hash,
                "matched": matched,
            }
            if not matched:
                failures.append(f"source hash mismatch: {relative}")

    upstream = manifest["upstream"]
    upstream_root = ROOT / upstream["path"]
    upstream_head = git_output("-C", str(upstream_root), "rev-parse", "HEAD")
    if upstream_head != upstream["commit"]:
        failures.append("checked-out MeshCore commit does not match runtime manifest")
    gitlink = git_output("ls-tree", "HEAD", "--", upstream["path"])
    match = re.fullmatch(r"160000 commit ([0-9a-f]{40})\t.+", gitlink)
    gitlink_commit = match.group(1) if match else None
    if gitlink_commit != upstream["commit"]:
        failures.append("MeshCore gitlink does not match runtime manifest")
    repository_commit = git_output("rev-parse", "HEAD")
    if expected_commit and repository_commit != expected_commit:
        failures.append(
            f"repository commit {repository_commit} does not match {expected_commit}"
        )
    if failures:
        raise GateFailure("; ".join(failures))
    return {
        "verified": True,
        "repository_commit": repository_commit,
        "expected_repository_commit": expected_commit,
        "upstream_commit": upstream_head,
        "gitlink_commit": gitlink_commit,
        "files": files,
    }


def obtain_external_archive(
    dependency: dict[str, Any], archive_path: Path | None = None
) -> tuple[bytes, dict[str, Any]]:
    if archive_path is not None:
        try:
            archive = archive_path.read_bytes()
        except OSError as exc:
            raise GateFailure(f"cannot read external dependency archive: {exc}") from exc
        source = str(archive_path.resolve())
    else:
        request = urllib.request.Request(
            dependency["archive_url"],
            headers={"User-Agent": "SIGUI-WP04-signed-advert-runtime/1"},
        )
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                archive = response.read(dependency["archive_size"] + 1)
        except OSError as exc:
            raise GateFailure(f"cannot download pinned Crypto archive: {exc}") from exc
        source = dependency["archive_url"]
    actual_hash = sha256_bytes(archive)
    if len(archive) != dependency["archive_size"]:
        raise GateFailure("pinned Crypto archive size mismatch")
    if actual_hash != dependency["archive_sha256"]:
        raise GateFailure("pinned Crypto archive SHA256 mismatch")
    return archive, {
        "verified": True,
        "source": source,
        "url": dependency["archive_url"],
        "size": len(archive),
        "sha256": actual_hash,
        "version": dependency["version"],
        "registry_version_id": dependency["registry_version_id"],
        "release_commit": dependency["release_commit"],
    }


def extract_external_sources(
    archive: bytes, dependency: dict[str, Any], destination: Path
) -> dict[str, Any]:
    expected_sources = dependency["sources"]
    if not isinstance(expected_sources, dict) or not expected_sources:
        raise GateFailure("external source registry is empty")
    extracted: dict[str, Any] = {}
    destination.mkdir(parents=True, exist_ok=True)
    try:
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as bundle:
            members: dict[str, tarfile.TarInfo] = {}
            for member in bundle.getmembers():
                path = PurePosixPath(member.name)
                if path.is_absolute() or ".." in path.parts:
                    raise GateFailure("external archive contains an unsafe path")
                if member.name in members:
                    raise GateFailure("external archive contains duplicate paths")
                members[member.name] = member
            for relative, expected_hash in expected_sources.items():
                path = PurePosixPath(relative)
                if path.is_absolute() or ".." in path.parts or "\\" in relative:
                    raise GateFailure("external source path registry is invalid")
                member = members.get(relative)
                if member is None or not member.isfile():
                    raise GateFailure(f"external archive source is missing: {relative}")
                source = bundle.extractfile(member)
                if source is None:
                    raise GateFailure(f"cannot read external archive source: {relative}")
                data = source.read()
                actual_hash = sha256_bytes(data)
                if actual_hash != expected_hash:
                    raise GateFailure(f"external source hash mismatch: {relative}")
                output = destination.joinpath(*path.parts)
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(data)
                extracted[relative] = {
                    "sha256": actual_hash,
                    "size": len(data),
                }
    except (tarfile.TarError, OSError) as exc:
        raise GateFailure(f"cannot extract pinned Crypto sources: {exc}") from exc
    return {"verified": True, "files": extracted}


def command_plan(
    cc: str,
    cxx: str,
    external_root: str = "$EXTERNAL_ROOT",
    build_root: str = "$BUILD_ROOT",
    *,
    sanitize: bool,
) -> list[list[str]]:
    build = Path(build_root)
    external = Path(external_root)
    include_flags = [
        "-I",
        str(ROOT / "tests" / "meshcore_signed_advert_runtime" / "stubs"),
        "-I",
        str(ROOT / "tests" / "meshcore_oracle" / "stubs"),
        "-I",
        str(external),
        "-I",
        str(ROOT / "third_party" / "MeshCore" / "src"),
        "-I",
        str(ED25519_ROOT),
    ]
    sanitizer_flags = ["-fsanitize=address,undefined"] if sanitize else []
    c_flags = [
        "-std=c11",
        "-O1",
        "-g",
        "-fno-omit-frame-pointer",
        "-ffunction-sections",
        "-fdata-sections",
        *sanitizer_flags,
        "-Wall",
        "-Wextra",
        "-Werror",
    ]
    imported_cxx_flags = [
        "-std=c++17",
        "-O1",
        "-g",
        "-fno-omit-frame-pointer",
        "-ffunction-sections",
        "-fdata-sections",
        *sanitizer_flags,
    ]
    harness_flags = [
        *imported_cxx_flags,
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-unknown-warning-option",
        "-Wno-unused-parameter",
        "-Wno-reorder",
        "-Wno-class-memaccess",
    ]
    commands: list[list[str]] = []
    objects: list[str] = []
    for index, source in enumerate((*ED25519_C_SOURCES, AES_SOURCE)):
        output = str(build / f"c_{index}.o")
        flags = list(c_flags)
        if source in SHIFT_BASE_EXCEPTION_SOURCES and sanitize:
            flags.append("-fno-sanitize=shift-base")
        if source == AES_SOURCE:
            flags.append("-DAES_DEC_PREKEYED")
        commands.append(
            [cc, *flags, *include_flags, "-c", str(source), "-o", output]
        )
        objects.append(output)
    cpp_sources = [
        *MESH_CPP_SOURCES,
        *(external / name for name in EXTERNAL_CPP_SOURCES),
        RNG_STUB_PATH,
        HARNESS_PATH,
    ]
    for index, source in enumerate(cpp_sources):
        output = str(build / f"cpp_{index}.o")
        flags = harness_flags if source in (RNG_STUB_PATH, HARNESS_PATH) else imported_cxx_flags
        commands.append(
            [cxx, *flags, *include_flags, "-c", str(source), "-o", output]
        )
        objects.append(output)
    executable = str(build / "meshcore_signed_advert_runtime")
    if os.name == "nt":
        executable += ".exe"
    commands.append(
        [
            cxx,
            *objects,
            *sanitizer_flags,
            "-Wl,--gc-sections",
            "-o",
            executable,
        ]
    )
    commands.append([executable])
    return commands


def run_command(command: Sequence[str]) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            list(command),
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as exc:
        raise GateFailure(f"cannot execute command: {command[0]}") from exc
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or exc.stdout or "").strip()[-4000:]
        raise GateFailure(f"command failed ({command[0]}): {detail}") from exc


def build_and_run(
    cc: str,
    cxx: str,
    external_root: Path,
    build_root: Path,
    expected_result: dict[str, Any],
    *,
    sanitize: bool,
) -> tuple[dict[str, Any], list[list[str]]]:
    commands = command_plan(
        cc,
        cxx,
        str(external_root),
        str(build_root),
        sanitize=sanitize,
    )
    runtime_output = ""
    for command in commands:
        completed = run_command(command)
        if len(command) == 1:
            runtime_output = completed.stdout.strip()
    try:
        result = json.loads(runtime_output)
    except json.JSONDecodeError as exc:
        raise GateFailure("signed-advert runtime did not emit one JSON result") from exc
    if result != expected_result:
        raise GateFailure("signed-advert runtime result drifted from the manifest")
    return result, commands


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--cxx", default="clang++")
    parser.add_argument("--commit")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--out", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest = load_manifest()
        source_receipt = verify_repository_sources(manifest, args.commit)
        archive, archive_receipt = obtain_external_archive(
            manifest["external_dependency"], args.archive
        )
        with tempfile.TemporaryDirectory(prefix="sigui-signed-advert-") as temporary:
            temporary_root = Path(temporary)
            external_root = temporary_root / "crypto"
            build_root = temporary_root / "build"
            build_root.mkdir()
            external_receipt = extract_external_sources(
                archive, manifest["external_dependency"], external_root
            )
            result, commands = build_and_run(
                args.cc,
                args.cxx,
                external_root,
                build_root,
                manifest["expected_result"],
                sanitize=args.sanitize,
            )
        report = {
            "schema_version": 1,
            "artifact_type": "d1l_meshcore_signed_advert_semantic_runtime",
            "status": "pass",
            "passed": True,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "work_package": "WP-04",
            "capability": manifest["capability"],
            "coverage_boundary": manifest["coverage_boundary"],
            "wp04_closure_eligible": False,
            "closure_ready": False,
            "repository": source_receipt,
            "external_archive": archive_receipt,
            "external_sources": external_receipt,
            "sanitizers_enabled": args.sanitize,
            "sanitizer_policy": manifest["sanitizer_policy"],
            "commands": commands,
            "result": result,
            "assertions": manifest["assertions"],
            "residual_gaps": manifest["residual_gaps"],
        }
        write_report(args.out, report)
        print(json.dumps({"status": "pass", "out": str(args.out)}))
        return 0
    except GateFailure as exc:
        print(f"signed-advert runtime gate failed: {exc}", file=os.sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
