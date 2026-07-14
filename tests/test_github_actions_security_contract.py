import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_DIR = ROOT / ".github" / "workflows"
BUILD_INPUTS_PATH = ROOT / ".github" / "d1l-build-inputs.json"
USES_LINE_RE = re.compile(r"^\s*(?:-\s*)?uses:\s*([^\s#]+)")
FULL_COMMIT_SHA_RE = re.compile(r"[0-9a-f]{40}")
SHA256_DIGEST_RE = re.compile(r"sha256:[0-9a-f]{64}")
ESP_IDF_INDEX_DIGEST = (
    "sha256:b9f2d6ea1c19e0c9f7959bdb74a9e3c775642f9d0f3b841937c5fa3363db892b"
)
ESP_IDF_IMAGE = f"espressif/idf:v5.5.4@{ESP_IDF_INDEX_DIGEST}"
REVIEWED_ACTION_PINS = {
    "actions/checkout": "9c091bb21b7c1c1d1991bb908d89e4e9dddfe3e0",
    "actions/download-artifact": "37930b1c2abaa49bbe596cd826c3c89aef350131",
    "actions/setup-python": "ece7cb06caefa5fff74198d8649806c4678c61a1",
    "actions/upload-artifact": "043fb46d1a93c77aae656e7c1c64a875d1fc6a0a",
    "arduino/setup-arduino-cli": "81d310742121c928ea9c8bbd407b4217b432ae02",
}
EXPECTED_HOST_PACKAGE_LOCK = {
    "pip": (
        "26.1.2",
        "382ff9f685ee3bc25864f820aa50505825f10f5458ffff07e30a6d96e5715cab",
    ),
    "pytest": (
        "9.1.1",
        "37a86b45efb9a47a61a36449063e8e18d0cab3161329fc099eb21783169c4f0c",
    ),
    "pyserial": (
        "3.5",
        "c4451db6ba391ca6ca299fb3ec7bae67a5c55dde170964c7a14ceefec02f2cf0",
    ),
    "Pillow": (
        "12.3.0",
        "1cca606cd25738df4ed873d5ad46bbdb3d83b5cbca291f6b4ff13a4df6b0bbe8",
    ),
    "colorama": (
        "0.4.6",
        "4f1d9991f5acc0ca119f9d443620b77f9d6b33703e51011c16baf57afb285fc6",
    ),
    "iniconfig": (
        "2.3.0",
        "f631c04d2c48c52b84d0d0549c99ff3859c98df65b3101406327ecc7d53fbf12",
    ),
    "packaging": (
        "26.2",
        "5fc45236b9446107ff2415ce77c807cee2862cb6fac22b8a73826d0693b0980e",
    ),
    "pluggy": (
        "1.6.0",
        "e920276dd6813095e9377c0bc5566d94c932c33b27a3e3945d8389c374dd4746",
    ),
    "Pygments": (
        "2.20.0",
        "81a9e26dd42fd28a23a2d169d86d7ac03b46e2f8b59ed4698fb4785f946d0176",
    ),
}


def workflow_paths() -> list[Path]:
    return sorted((*WORKFLOW_DIR.glob("*.yml"), *WORKFLOW_DIR.glob("*.yaml")))


def test_cross_platform_build_input_bytes_are_pinned_to_lf() -> None:
    attributes = (ROOT / ".gitattributes").read_text(encoding="ascii").splitlines()

    assert ".github/d1l-build-inputs.json text eol=lf" in attributes
    assert "requirements/ci-host-windows.txt text eol=lf" in attributes
    assert b"\r\n" not in BUILD_INPUTS_PATH.read_bytes()


def top_level_permissions(text: str) -> dict[str, str] | None:
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if line != "permissions:":
            continue
        permissions: dict[str, str] = {}
        for candidate in lines[index + 1 :]:
            if candidate and not candidate.startswith((" ", "\t")):
                break
            match = re.fullmatch(r"  ([a-z-]+):\s*([a-z-]+)\s*", candidate)
            if match:
                permissions[match.group(1)] = match.group(2)
        return permissions
    return None


def test_all_remote_actions_use_immutable_full_commit_shas() -> None:
    workflows = workflow_paths()
    assert workflows, "no GitHub Actions workflows found"

    remote_uses: list[tuple[Path, int, str]] = []
    failures: list[str] = []
    for workflow in workflows:
        for line_number, line in enumerate(
            workflow.read_text(encoding="utf-8").splitlines(), start=1
        ):
            match = USES_LINE_RE.match(line)
            if match is None:
                continue
            target = match.group(1)
            if target.startswith("./"):
                continue
            remote_uses.append((workflow, line_number, target))
            _, separator, ref = target.rpartition("@")
            if separator != "@" or FULL_COMMIT_SHA_RE.fullmatch(ref) is None:
                failures.append(f"{workflow.relative_to(ROOT)}:{line_number}: {target}")

    assert remote_uses, "no remote action references found"
    assert not failures, "remote actions must use full commit SHAs:\n" + "\n".join(failures)


def test_remote_actions_match_the_reviewed_upstream_pins() -> None:
    observed: dict[str, set[str]] = {}
    for workflow in workflow_paths():
        for line in workflow.read_text(encoding="utf-8").splitlines():
            match = USES_LINE_RE.match(line)
            if match is None or match.group(1).startswith("./"):
                continue
            action, separator, ref = match.group(1).rpartition("@")
            assert separator == "@"
            observed.setdefault(action, set()).add(ref)

    assert observed == {
        action: {commit} for action, commit in REVIEWED_ACTION_PINS.items()
    }


def test_workflows_default_to_read_only_repository_contents() -> None:
    failures: list[str] = []
    for workflow in workflow_paths():
        permissions = top_level_permissions(workflow.read_text(encoding="utf-8"))
        if permissions != {"contents": "read"}:
            failures.append(
                f"{workflow.relative_to(ROOT)}: expected top-level "
                f"permissions contents: read, got {permissions!r}"
            )

    assert not failures, "\n".join(failures)


def test_esp_idf_container_and_recorded_build_inputs_are_digest_pinned() -> None:
    build_inputs = json.loads(BUILD_INPUTS_PATH.read_text(encoding="utf-8"))
    container = build_inputs["esp_idf"]["container"]

    assert build_inputs["schema"] == 1
    assert build_inputs["kind"] == "d1l_build_inputs"
    assert build_inputs["esp_idf"]["version"] == "v5.5.4"
    assert container == {
        "registry": "docker.io",
        "repository": "espressif/idf",
        "tag": "v5.5.4",
        "reference": ESP_IDF_IMAGE,
        "index_digest": ESP_IDF_INDEX_DIGEST,
        "media_type": "application/vnd.oci.image.index.v1+json",
        "registry_manifest_url": (
            "https://registry-1.docker.io/v2/espressif/idf/manifests/v5.5.4"
        ),
        "platforms": {
            "linux/amd64": (
                "sha256:116f0526dfc87e764785370e59b88822e02cf4f9e1edd953cad5ed2d02672023"
            ),
            "linux/arm64": (
                "sha256:f3bc9daebfd8cb4e0afbdced7caac4431c7a862326a5e5587d65c31439549bb5"
            ),
        },
    }
    assert SHA256_DIGEST_RE.fullmatch(container["index_digest"])
    assert all(SHA256_DIGEST_RE.fullmatch(value) for value in container["platforms"].values())

    workflow = (WORKFLOW_DIR / "d1l-ci.yml").read_text(encoding="utf-8")
    assert workflow.count(f"container: {ESP_IDF_IMAGE}") == 1
    assert f"'{ESP_IDF_IMAGE}' > artifacts/idf-migration/container-image.txt" in workflow
    assert "cp .github/d1l-build-inputs.json artifacts/idf-migration/build-inputs.json" in workflow


def test_windows_host_python_tools_are_version_and_hash_locked() -> None:
    build_inputs = json.loads(BUILD_INPUTS_PATH.read_text(encoding="utf-8"))
    host_python = build_inputs["host_python"]
    requirements = host_python["requirements"]
    requirements_path = ROOT / requirements["path"]
    requirements_bytes = requirements_path.read_bytes()
    requirements_text = requirements_bytes.decode("utf-8")

    assert host_python["version"] == "3.13.6"
    assert host_python["architecture"] == "x64"
    assert host_python["setup_action"] == {
        "repository": "actions/setup-python",
        "version": "v6.3.0",
        "commit": REVIEWED_ACTION_PINS["actions/setup-python"],
    }
    assert requirements["sha256"] == hashlib.sha256(requirements_bytes).hexdigest()
    assert requirements["index_url"] == "https://pypi.org/simple"
    assert requirements["only_binary"] is True
    assert requirements["require_hashes"] is True
    assert "--index-url https://pypi.org/simple" in requirements_text
    assert "--only-binary :all:" in requirements_text
    assert "--require-hashes" in requirements_text

    entries = re.findall(
        r"(?m)^([A-Za-z0-9_.-]+)==([^\s\\]+) \\\n"
        r"\s+--hash=sha256:([0-9a-f]{64})$",
        requirements_text,
    )
    assert {name: (version, digest) for name, version, digest in entries} == (
        EXPECTED_HOST_PACKAGE_LOCK
    )
    assert requirements["packages"] == {
        name: version for name, (version, _) in EXPECTED_HOST_PACKAGE_LOCK.items()
    }

    workflow = (WORKFLOW_DIR / "d1l-ci.yml").read_text(encoding="utf-8")
    assert (
        "uses: actions/setup-python@"
        f"{REVIEWED_ACTION_PINS['actions/setup-python']} # v6.3.0"
    ) in workflow
    assert 'python-version: "3.13.6"' in workflow
    assert "architecture: x64" in workflow
    assert "python -m pip install --disable-pip-version-check --require-hashes -r $requirementsPath" in workflow
    assert "python -m pip install --upgrade" not in workflow
    assert "Copy-Item -LiteralPath $requirementsPath" in workflow
    assert 'artifacts/build-inputs/SHA256SUMS.txt" -Encoding ascii' in workflow
