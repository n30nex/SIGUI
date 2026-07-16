from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def test_production_families_share_the_executable_lifetime_boundary():
    lifetime = read("main/mesh/meshcore_lifetime.h")
    node_store = read("main/mesh/node_store.c")
    path_state = read("main/mesh/meshcore_path_state.c")
    packet_hash = read("main/mesh/meshcore_packet_hash.c")

    assert "D1L_MESHCORE_CONTACT_REACHABLE_MAX_AGE_MS" in lifetime
    assert "(uint32_t)(now_ms - observed_at_ms) <= max_age_ms" in lifetime
    assert "candidate_timestamp > retained_timestamp" in lifetime
    assert "current_index + 1U == capacity" in lifetime

    assert "d1l_meshcore_lifetime_contact_reachable(" in node_store
    assert "d1l_meshcore_lifetime_advert_is_strictly_newer(" in node_store
    assert "d1l_meshcore_lifetime_age_current_u32(" in path_state
    assert "d1l_meshcore_lifetime_packet_fifo_next(" in packet_hash


def test_identity_and_replay_truth_do_not_expire_with_ui_reachability():
    lifetime = read("main/mesh/meshcore_lifetime.h")
    contact_header = read("main/mesh/contact_store.h")
    node_store = read("main/mesh/node_store.c")

    assert "Canonical retained contacts do not expire" in lifetime
    assert "d1l_contact_store_delete" in contact_header
    assert "d1l_meshcore_lifetime_contact_reachable(" in node_store
    assert "d1l_meshcore_lifetime_advert_is_strictly_newer(" in node_store
    assert "advert_timestamp = 0" not in node_store
