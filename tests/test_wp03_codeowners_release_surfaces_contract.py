from fnmatch import fnmatchcase
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CODEOWNERS = ROOT / ".github" / "CODEOWNERS"
REQUIRED_OWNER = "@n30nex"


def parse_rules() -> dict[str, tuple[str, ...]]:
    rules: dict[str, tuple[str, ...]] = {}
    for raw_line in CODEOWNERS.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        fields = line.split()
        assert len(fields) >= 2, f"owner missing from CODEOWNERS rule: {raw_line!r}"
        pattern, *owners = fields
        assert not pattern.startswith("!"), "CODEOWNERS does not support negation"
        assert "[" not in pattern and "]" not in pattern, (
            "CODEOWNERS does not support character ranges"
        )
        assert pattern not in rules, f"duplicate CODEOWNERS pattern: {pattern}"
        assert all(owner.startswith("@") for owner in owners), (
            f"invalid CODEOWNERS owner for {pattern}: {owners!r}"
        )
        rules[pattern] = tuple(owners)
    return rules


def owners_for(
    relative_path: str, rules: dict[str, tuple[str, ...]]
) -> tuple[str, ...]:
    selected: tuple[str, ...] = ()
    for pattern, owners in rules.items():
        normalized = pattern.removeprefix("/")
        if normalized.endswith("/"):
            matches = relative_path.startswith(normalized)
        else:
            matches = fnmatchcase(relative_path, normalized)
        if matches:
            selected = owners
    return selected


def test_codeowners_rules_are_explicit_and_single_owner():
    assert CODEOWNERS.is_file()
    rules = parse_rules()
    assert rules
    assert all(owners == (REQUIRED_OWNER,) for owners in rules.values())


def test_wp03_release_security_categories_have_dedicated_rules():
    rules = parse_rules()
    required_patterns = {
        # Workflow security and the ownership policy itself.
        "/.github/CODEOWNERS",
        "/.github/workflows/",
        "/.github/dependabot.yml",
        # Locks and immutable build inputs.
        "/.gitattributes",
        "/.gitmodules",
        "/*.lock",
        "/dependencies.lock",
        "/requirements/",
        "/.github/d1l-build-inputs.json",
        "/CMakeLists.txt",
        "/main/CMakeLists.txt",
        "/partitions_d1l.csv",
        "/sdkconfig.defaults",
        "/third_party/",
        "/patches/",
        "/overlays/meshcore_ed25519_defined/",
        # Provenance, SBOM, checksum, package, and comparator surfaces.
        "/docs/BUILD_PROVENANCE_D1L.md",
        "/**/SHA256SUMS.txt",
        "/scripts/artifact_metadata.py",
        "/scripts/*build_inputs*",
        "/scripts/*release*",
        "/scripts/*checksums*",
        "/scripts/package_release_d1l.py",
        "/scripts/compare_release_reproducibility_d1l.py",
        "/scripts/provenance_d1l.py",
        "/scripts/sbom_d1l.py",
        "/scripts/meshcore_signed_advert_runtime_d1l.py",
        "/scripts/validate_ed25519_defined_overlay.py",
        # Future update/security implementation boundaries.
        "/main/update/",
        "/main/security/",
        "/firmware/",
    }
    assert required_patterns <= rules.keys()


def test_current_release_security_files_resolve_to_maintainer():
    rules = parse_rules()
    current_paths = [
        ".github/CODEOWNERS",
        ".github/workflows/d1l-ci.yml",
        ".gitattributes",
        ".gitmodules",
        "dependencies.lock",
        "CMakeLists.txt",
        "main/CMakeLists.txt",
        "partitions_d1l.csv",
        "sdkconfig.defaults",
        "docs/completion/SHA256SUMS.txt",
        "scripts/artifact_metadata.py",
        "scripts/package_release_d1l.py",
        "scripts/release_gate_audit_d1l.py",
        "scripts/verify_checksums.py",
        "scripts/meshcore_signed_advert_runtime_d1l.py",
        "scripts/validate_ed25519_defined_overlay.py",
        "overlays/meshcore_ed25519_defined/fe.c",
    ]
    for relative_path in current_paths:
        assert (ROOT / relative_path).is_file(), (
            f"missing protected release surface: {relative_path}"
        )
        assert owners_for(relative_path, rules) == (REQUIRED_OWNER,), (
            f"release-sensitive path is not owned by {REQUIRED_OWNER}: "
            f"{relative_path}"
        )


def test_planned_wp03_and_security_paths_are_already_covered():
    rules = parse_rules()
    planned_paths = {
        "workflow security": ".github/workflows/release.yml",
        "build-input manifest": ".github/d1l-build-inputs.json",
        "host dependency lock": "requirements/ci-host-windows.txt",
        "Arduino input verifier": "scripts/verify_arduino_build_inputs.py",
        "provenance generator": "scripts/provenance_d1l.py",
        "SBOM generator": "scripts/sbom_d1l.py",
        "checksum verifier": "scripts/verify_checksums.py",
        "release package builder": "scripts/package_release_d1l.py",
        "reproducibility comparator": (
            "scripts/compare_release_reproducibility_d1l.py"
        ),
        "signed-advert runtime gate": "scripts/meshcore_signed_advert_runtime_d1l.py",
        "defined Ed25519 validator": "scripts/validate_ed25519_defined_overlay.py",
        "defined Ed25519 production overlay": "overlays/meshcore_ed25519_defined/ge.c",
        "signed update implementation": "main/update/signed_manifest.c",
        "security implementation": "main/security/signature_policy.c",
        "RP2040 firmware": "firmware/rp2040_sd_bridge/deskos_sd_bridge.ino",
    }
    for category, relative_path in planned_paths.items():
        assert owners_for(relative_path, rules) == (REQUIRED_OWNER,), (
            f"{category} is not protected by CODEOWNERS: {relative_path}"
        )
