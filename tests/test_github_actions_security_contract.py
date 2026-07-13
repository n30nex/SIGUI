import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_DIR = ROOT / ".github" / "workflows"
USES_LINE_RE = re.compile(r"^\s*(?:-\s*)?uses:\s*([^\s#]+)")
FULL_COMMIT_SHA_RE = re.compile(r"[0-9a-f]{40}")
REVIEWED_ACTION_PINS = {
    "actions/checkout": "9c091bb21b7c1c1d1991bb908d89e4e9dddfe3e0",
    "actions/download-artifact": "37930b1c2abaa49bbe596cd826c3c89aef350131",
    "actions/upload-artifact": "043fb46d1a93c77aae656e7c1c64a875d1fc6a0a",
    "arduino/setup-arduino-cli": "81d310742121c928ea9c8bbd407b4217b432ae02",
}


def workflow_paths() -> list[Path]:
    return sorted((*WORKFLOW_DIR.glob("*.yml"), *WORKFLOW_DIR.glob("*.yaml")))


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
