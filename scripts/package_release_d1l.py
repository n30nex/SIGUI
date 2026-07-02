#!/usr/bin/env python3
"""Package MeshCore DeskOS D1L firmware artifacts for release handoff."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path


PROJECT = "MeshCore DeskOS D1L"
DEFAULT_FLASH_SIZE = 8 * 1024 * 1024
FLASH_BAUD = 460800
EXPECTED_BSP_PATCH = Path("patches/sensecap_indicator_touch_fix.patch")
EXPECTED_BSP_SUBMODULE = Path("third_party/sensecap_indicator_esp32")


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


def expected_bsp_patch_applied(root: Path) -> bool:
    root = root.resolve()
    patch = root / EXPECTED_BSP_PATCH
    submodule = root / EXPECTED_BSP_SUBMODULE
    if not patch.exists() or not submodule.exists():
        return False
    return command_succeeds(
        submodule,
        ["git", "apply", "--unidiff-zero", "--reverse", "--check", str(patch)],
    )


def clean_release_status_entries(root: Path, status: str) -> tuple[list[str], list[str]]:
    entries = [line for line in status.splitlines() if line.strip()]
    if not entries:
        return [], []

    expected_submodule = EXPECTED_BSP_SUBMODULE.as_posix()
    expected_entries = [line for line in entries if status_path(line) == expected_submodule]
    other_entries = [line for line in entries if status_path(line) != expected_submodule]
    if expected_entries and expected_bsp_patch_applied(root):
        return other_entries, [EXPECTED_BSP_PATCH.as_posix()]

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
    for path in sorted(package_dir.rglob("*")):
        if not path.is_file() or path.name == "SHA256SUMS.txt":
            continue
        rel = path.relative_to(package_dir).as_posix()
        rows.append(f"{sha256_file(path)}  ./{rel}")
    (package_dir / "SHA256SUMS.txt").write_text("\n".join(rows) + "\n", encoding="ascii")


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
    readme.write_text(
        f"""# {PROJECT} Release Package

Package: `{package_name}`

Git commit: `{manifest['git'].get('commit') or 'unknown'}`

## Contents

- `firmware/` contains the bootloader, partition table, app binary, and `flasher_args.json`.
- `update/meshcore_deskos_d1l-app.bin` is the application image for future OTA/update flows.
- `full-flash/meshcore_deskos_d1l-full-8mb.bin` is an 8MB factory/recovery image padded with `0xff`.
- `notices/` contains the project license, third-party notices, source audit notes, and attributions for public distribution.
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


def create_release_package(root: Path, build_dir: Path, out_dir: Path, package_name: str, full_size: int) -> dict:
    flasher_args = load_flasher_args(build_dir)
    package_dir = out_dir / package_name
    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.mkdir(parents=True, exist_ok=True)

    firmware_dir = package_dir / "firmware"
    entries = copy_flash_files(build_dir, firmware_dir, flasher_args)
    app = app_entry(entries)
    update_image = copy_update_image(package_dir, firmware_dir, app)
    full_image = write_full_flash_image(build_dir, package_dir, flasher_args, full_size)
    debug_files = copy_optional_debug_files(build_dir, package_dir)
    notice_files = copy_notice_files(root, package_dir)
    scripts = write_flash_scripts(package_dir, entries, flasher_args, full_image)

    manifest = {
        "schema": 1,
        "project": PROJECT,
        "package": package_name,
        "created_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "git": git_info(root),
        "source_build_dir": str(build_dir),
        "flash_settings": flasher_args.get("flash_settings", {}),
        "flash_files": entries,
        "update_image": update_image,
        "full_flash_image": full_image,
        "debug_files": debug_files,
        "notice_files": notice_files,
        "scripts": scripts,
        "notes": [
            "Project flash scripts require D1L_PORT or an explicit -Port.",
            "Full 8MB flash script requires a typed confirmation because it can overwrite persisted state.",
            "Flash backup was skipped in current bring-up only when the operator explicitly requested that.",
        ],
    }
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

    manifest = create_release_package(root, build_dir, out_dir, package_name, args.full_size)
    print(json.dumps(manifest))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
