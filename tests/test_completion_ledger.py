import copy
from pathlib import Path

from scripts.completion_ledger import (
    dependency_satisfied,
    load_ledger,
    render_status,
    runnable_work_packages,
    validate_ledger,
)


LEDGER_PATH = Path("docs/COMPLETION_LEDGER.yaml")
STATUS_PATH = Path("docs/COMPLETION_STATUS.md")
WP01_COMMIT = "092293f2311a24c9899bc9bf343ab014c4ba0411"


def ledger_copy() -> dict:
    return copy.deepcopy(load_ledger(LEDGER_PATH))


def package(ledger: dict, package_id: str) -> dict:
    return next(item for item in ledger["work_packages"] if item["id"] == package_id)


def bank_wp01_proof(ledger: dict) -> dict:
    item = package(ledger, "WP-01")
    item["status"] = "hardware_green"
    item["implementation_commit"] = WP01_COMMIT
    item["implementation_merged"] = False
    item["proof_banked"] = True
    item["evidence"].extend(
        {
            "filename": filename,
            "kind": "physical",
            "commit": WP01_COMMIT,
            "valid": True,
        }
        for filename in (
            "sd_inserted_stability_092293f_COM12_COM16.json",
            "sd_remove_reinsert_092293f_COM12_COM16.json",
            "retained_reboot_matrix_092293f_COM12.json",
            "storage_active_soak_092293f_COM12_COM16.json",
        )
    )
    return item


def reset_wp02_before_merge(ledger: dict) -> dict:
    item = package(ledger, "WP-02")
    item["status"] = "in_progress"
    item["dependency_gate"] = "merged"
    item["implementation_commit"] = None
    item["implementation_merged"] = False
    return item


def test_repository_ledger_validates_and_status_is_current():
    ledger = load_ledger(LEDGER_PATH)

    assert validate_ledger(ledger) == []
    assert STATUS_PATH.read_text(encoding="utf-8") == render_status(ledger)


def test_current_runnable_selection_advances_past_merged_wp02_implementation():
    ledger = load_ledger(LEDGER_PATH)

    assert runnable_work_packages(ledger)[:2] == ["WP-03", "WP-04"]


def test_implementation_merged_gate_unlocks_dependents_while_proof_remains_open():
    ledger = ledger_copy()
    wp02 = package(ledger, "WP-02")

    assert wp02["status"] == "in_progress"
    assert wp02["dependency_gate"] == "implementation_merged"
    assert wp02["implementation_merged"] is True
    assert wp02["proof_banked"] is False
    assert dependency_satisfied(wp02) is True


def test_implementation_merged_gate_fails_closed_without_merged_implementation():
    ledger = ledger_copy()
    wp02 = package(ledger, "WP-02")
    wp02["implementation_merged"] = False

    assert dependency_satisfied(wp02) is False


def test_execution_blocker_removes_wp01_from_runnable_work():
    ledger = ledger_copy()
    blocker = next(
        item for item in ledger["blockers"] if item["id"] == "BLK-WP01-RETAINED-TIMEOUT-20260712"
    )
    blocker["status"] = "open"
    blocker["blocks_execution"] = True
    item = package(ledger, "WP-01")
    item["status"] = "in_progress"
    item["proof_banked"] = False
    item["blockers"] = [blocker["id"]]
    package(ledger, "WP-02")["status"] = "blocked"

    assert runnable_work_packages(ledger) == []


def test_unknown_state_is_rejected():
    ledger = ledger_copy()
    package(ledger, "WP-00")["status"] = "done"

    assert any("unknown status" in error for error in validate_ledger(ledger))


def test_valid_evidence_must_match_declared_exact_commit():
    ledger = ledger_copy()
    package(ledger, "WP-01")["evidence"][0]["commit"] = (
        "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
    )

    assert any("valid evidence" in error and "expected" in error for error in validate_ledger(ledger))


def test_latest_receipt_must_reference_valid_exact_commit_evidence():
    ledger = ledger_copy()
    package(ledger, "WP-01")["latest_valid_receipt"]["filename"] = "missing.json"

    assert any("latest_valid_receipt is not present" in error for error in validate_ledger(ledger))


def test_merged_item_without_required_evidence_is_rejected():
    ledger = ledger_copy()
    item = package(ledger, "WP-00")
    item["status"] = "merged"
    item["implementation_commit"] = ledger["repository"]["main"]["commit"]
    item["evidence_commit"] = ledger["repository"]["main"]["commit"]
    item["implementation_merged"] = True
    item["evidence"] = []
    item["latest_valid_receipt"] = None

    assert any("WP-00 is merged but lacks evidence" in error for error in validate_ledger(ledger))


def test_wp01_banked_hardware_proof_unlocks_wp02_before_pr80_merge():
    ledger = ledger_copy()
    wp01 = bank_wp01_proof(ledger)
    reset_wp02_before_merge(ledger)

    errors = validate_ledger(ledger)

    assert wp01["implementation_merged"] is False
    assert dependency_satisfied(wp01) is True
    assert runnable_work_packages(ledger)[0] == "WP-02"
    assert not any("WP-02" in error and "dependency WP-01" in error for error in errors)


def test_banked_wp01_status_describes_wp02_as_unlocked():
    ledger = ledger_copy()
    bank_wp01_proof(ledger)
    reset_wp02_before_merge(ledger)

    rendered = render_status(ledger)

    assert "`WP-02` is unlocked" in rendered
    assert "`WP-02` remains blocked" not in rendered
    assert "Highest-priority pending: `WP-02`" in rendered


def test_hardware_green_requires_proof_banked():
    ledger = ledger_copy()
    item = bank_wp01_proof(ledger)
    item["proof_banked"] = False

    assert any("hardware_green requires proof_banked=true" in error for error in validate_ledger(ledger))


def test_operational_port_default_is_rejected():
    ledger = ledger_copy()
    ledger["port_policy"]["defaults"]["MESH_PEER_PORT"] = "COM11"

    assert any("MESH_PEER_PORT must not have an operational default" in error for error in validate_ledger(ledger))


def test_unavailable_capability_cannot_be_documented_as_working():
    ledger = ledger_copy()
    capability = next(item for item in ledger["capabilities"] if item["id"] == "ble_companion")
    capability["documentation_status"] = "working"

    assert any("ble_companion is unavailable" in error for error in validate_ledger(ledger))
