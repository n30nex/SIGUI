from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def _function_body(source: str, signature: str, next_signature: str) -> str:
    return source[source.index(signature) : source.index(next_signature)]


def test_settings_sanitizer_fails_closed_without_erasing_key_evidence():
    source = (ROOT / "main/app/settings_model.c").read_text(encoding="utf-8")
    body = _function_body(
        source,
        "void d1l_settings_sanitize",
        "d1l_identity_state_t d1l_settings_identity_state",
    )

    assert "d1l_settings_identity_state(settings)" in body
    assert "settings->identity_ready = false" in body
    assert "memset(settings->identity_public_key" not in body
    assert "memset(settings->identity_private_key" not in body


def test_identity_classifier_is_a_production_component_primitive():
    header = (ROOT / "main/app/identity_state.h").read_text(encoding="utf-8")
    source = (ROOT / "main/app/settings_model.c").read_text(encoding="utf-8")
    cmake = (ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")

    assert "D1L_IDENTITY_STATE_ABSENT" in header
    assert "D1L_IDENTITY_STATE_CONSISTENT" in header
    assert "D1L_IDENTITY_STATE_INCONSISTENT" in header
    assert "ed25519_derive_pub(derived_public, private_key)" in source
    assert "(private_key[0] & 0x07U) != 0U" in source
    assert "(private_key[31] & 0xc0U) != 0x40U" in source
    assert "identity_bytes_uniform(" in source
    assert "&private_key[32]" in source
    assert "memcmp(" in source
    assert '"app/settings_model.c"' in cmake
    assert '"app/identity_state.c"' not in cmake
