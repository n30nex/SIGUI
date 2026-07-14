import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from scripts import meshcore_conformance_d1l as conformance


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "meshcore_conformance_d1l.py"
ORACLE_ROOT = ROOT / "tests" / "meshcore_oracle"
MANIFEST = ORACLE_ROOT / "manifest.json"
COVERAGE_MANIFEST = ORACLE_ROOT / "coverage_manifest.json"
UPSTREAM_COMMIT = "e8d3c53ba1ea863937081cd0caad759b832f3028"
BOUNDARY = (
    "pinned_upstream_packet_advert_group_dm_expected_ack_path_return_"
    "route_codes_ack_trace_and_signed_advert_creation_strict_verification_"
    "and_anonymous_login_request_crypto"
)


def canonical_lf_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def git_head() -> str:
    return subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def test_oracle_manifest_is_exactly_pinned_and_fail_closed():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    assert manifest["schema_version"] == 1
    assert manifest["corpus_version"] == 14
    assert manifest["abi_version"] == 2
    assert manifest["coverage_boundary"] == BOUNDARY
    assert manifest["wp04_closure_eligible"] is False
    assert manifest["upstream"]["commit"] == UPSTREAM_COMMIT
    assert manifest["interface"] == {
        "language": "c_abi",
        "header": "tests/meshcore_oracle/meshcore_oracle.h",
        "implementation": "tests/meshcore_oracle/meshcore_oracle.cpp",
        "upstream_types": [
            "mesh::Packet",
            "mesh::Utils",
            "AdvertDataBuilder",
            "AdvertDataParser",
        ],
        "reject_preserves_output": True,
        "crypto_available": False,
        "group_crypto_available": True,
        "group_crypto_scope": (
            "aes128_ecb_zero_padding_truncated_hmac_sha256_and_"
            "basechatmesh_setchannel_hash_only"
        ),
        "dm_crypto_available": True,
        "dm_crypto_scope": (
            "basechatmesh_plain_text_layout_and_mesh_datagram_crypto_with_"
            "caller_supplied_hashes_and_shared_secret_only"
        ),
        "expected_ack_path_available": True,
        "expected_ack_path_scope": (
            "plain_dm_expected_hash_all_lengths_defined_ack_bodies_and_"
            "authenticated_ack_path_with_caller_supplied_identity_hash_"
            "secret_and_rng_inputs_only"
        ),
        "path_return_route_codes_available": True,
        "path_return_route_codes_scope": (
            "general_authenticated_path_return_extra_and_no_extra_uniqueness_"
            "framing_with_caller_supplied_hash_secret_rng_and_route_inputs_"
            "only_no_route_selection_dispatch_forwarding_or_rf"
        ),
        "signed_advert_ed25519_available": True,
        "signed_advert_scope": (
            "d1l_production_message_layout_strict_points_and_ed25519_verification_only_no_mesh_dispatch"
        ),
        "signed_advert_packet_creation_available": True,
        "signed_advert_packet_creation_scope": (
            "deterministic_seed_keypair_pre_route_creation_and_authenticated_"
            "parse_only_no_identity_verifier_dispatch_replay_contact_or_rf"
        ),
        "anonymous_login_request_available": True,
        "anonymous_login_request_scope": (
            "canonical_room_nonroom_login_request_create_authenticated_parse_"
            "with_caller_supplied_hash_public_key_secret_time_and_room_mode_"
            "only_no_key_exchange_authorization_replay_response_session_"
            "dispatch_route_or_rf"
        ),
        "canonical_advert_data": True,
        "route_header_scope": "non_trace_direct_flood_and_zero_hop_headers",
        "ack_frame_scope": "simple_and_multipart_payload_framing_only",
        "trace_frame_scope": (
            "initial_outbound_direct_flags_zero_trace_framing_only"
        ),
        "golden_vector_sources": {
            "direct_flood_headers": "third_party/MeshCore/src/Mesh.cpp",
            "ack_frames": "third_party/MeshCore/src/Mesh.cpp",
            "trace_source_frames": "third_party/MeshCore/src/Mesh.cpp",
            "signed_advert_message_layout": "third_party/MeshCore/src/Mesh.cpp",
            "signed_advert_fixed_seed_keypair": (
                "third_party/MeshCore/lib/ed25519/keypair.c"
            ),
            "signed_advert_fixed_seed_signer": (
                "third_party/MeshCore/lib/ed25519/sign.c"
            ),
            "signed_advert_verifier": (
                "third_party/MeshCore/lib/ed25519/verify.c"
            ),
            "signed_advert_point_decoder": (
                "third_party/MeshCore/lib/ed25519/ge.c"
            ),
            "signed_advert_independent_kat": "RFC 8032 section 7.1 TEST 1",
            "anonymous_login_plaintext": (
                "third_party/MeshCore/src/helpers/BaseChatMesh.cpp"
            ),
            "anonymous_login_datagram": "third_party/MeshCore/src/Mesh.cpp",
            "anonymous_login_crypto": "third_party/MeshCore/src/Utils.cpp",
            "anonymous_login_independent_golden": (
                "two exact payloads and six-vector matrix from independent "
                "AES-128 ECB and HMAC-SHA-256 generation"
            ),
            "public_group_channel_hash": (
                "third_party/MeshCore/src/helpers/BaseChatMesh.cpp"
            ),
            "public_group_datagram": "third_party/MeshCore/src/Mesh.cpp",
            "public_group_encrypt_mac_parse": (
                "third_party/MeshCore/src/Utils.cpp"
            ),
            "public_group_aes": (
                "third_party/sensecap_indicator_esp32/components/LoRaWAN/"
                "soft-se/aes.c"
            ),
            "public_group_sha_hmac": "tests/meshcore_oracle/stubs/SHA256.h",
            "public_group_independent_kats": (
                "FIPS 180-4 SHA-256 abc; RFC 4231 HMAC test 1; "
                "FIPS 197 AES-128 cipher example"
            ),
            "dm_datagram": "third_party/MeshCore/src/Mesh.cpp",
            "dm_plain_text_layout": (
                "third_party/MeshCore/src/helpers/BaseChatMesh.cpp"
            ),
            "dm_encrypt_mac_parse": "third_party/MeshCore/src/Utils.cpp",
            "dm_independent_golden": (
                "attempt 0 and 255 exact payloads; attempt matrix and maximum "
                "plus exact-block payload SHA-256 digests"
            ),
            "expected_ack_hash": (
                "third_party/MeshCore/src/helpers/BaseChatMesh.cpp"
            ),
            "ack_path_datagram": "third_party/MeshCore/src/Mesh.cpp",
            "ack_path_encrypt_mac_parse": "third_party/MeshCore/src/Utils.cpp",
            "ack_path_independent_golden": (
                "four exact ACK bodies; exact-block expected hash; zero-path "
                "exact payload; four-path payload matrix SHA-256"
            ),
            "path_return_datagram": "third_party/MeshCore/src/Mesh.cpp",
            "path_return_encrypt_mac_parse": "third_party/MeshCore/src/Utils.cpp",
            "path_return_route_codes": "third_party/MeshCore/src/Mesh.cpp",
            "path_return_independent_golden": (
                "zero-path exact payload; general extra and no-extra uniqueness "
                "matrix SHA-256; direct flood and transport-code route matrix"
            ),
        },
    }
    assert manifest["determinism"]["signed_advert_seed_hex"] == (
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f"
    )
    assert manifest["determinism"]["signed_advert_generation_recipe"] == (
        "ed25519_create_keypair_seed_then_ed25519_sign_"
        "public_key_timestamp_wire_bytes_app_data"
    )
    assert manifest["determinism"]["anonymous_login_recipe"] == (
        "basechatmesh_timestamp_optional_room_sync_password_then_mesh_"
        "anonymous_datagram_aes128_hmac_with_caller_supplied_outer_and_secret"
    )
    assert manifest["determinism"]["anonymous_login_matrix"] == (
        "nonroom_empty_exact12_max15_and_room_empty_exact8_max15"
    )
    assert manifest["determinism"]["independent_verifier_kat"] == (
        "RFC 8032 section 7.1 TEST 1 empty message"
    )
    assert manifest["determinism"]["canonical_s_regression"] == (
        "RFC 8032 TEST 1 signature scalar plus Ed25519 group order L"
    )
    assert manifest["determinism"]["identity_point_forgery_regression"] == (
        "identity public key plus identity R and zero S accepted by the pinned "
        "verifier before strict point validation"
    )
    assert manifest["determinism"]["public_group_recipe"] == (
        "basechatmesh_setchannel_padded16_or_full32_secret_sha256_hash_then_"
        "utils_aes128_ecb_zero_pad_and_two_byte_hmac_sha256"
    )
    assert manifest["determinism"]["dm_recipe"] == (
        "basechatmesh_timestamp_low_attempt_text_optional_full_attempt_then_"
        "mesh_hash_prefix_and_utils_aes128_hmac"
    )
    assert manifest["determinism"]["dm_attempt_matrix"] == "0_through_255"
    assert manifest["determinism"]["dm_exact_block_matrix"] == (
        "normal_text_lengths_11_through_155_step_16"
    )
    assert manifest["determinism"]["expected_ack_recipe"] == (
        "basechatmesh_timestamp_low_attempt_text_sender_public_key_sha256_"
        "truncated4_plus_optional_full_attempt_and_caller_rng_byte"
    )
    assert manifest["determinism"]["ack_path_recipe"] == (
        "mesh_path_return_encoded_path_ack_extra_then_utils_aes128_hmac"
    )
    assert manifest["determinism"]["ack_path_encodings"] == [
        "zero",
        "one_byte",
        "two_byte",
        "three_byte",
    ]
    assert manifest["determinism"]["path_return_recipe"] == (
        "mesh_createpathreturn_general_extra_or_caller_rng_uniqueness_then_"
        "utils_aes128_hmac"
    )
    assert manifest["determinism"]["path_return_encodings"] == [
        "zero",
        "one_byte",
        "two_byte_maximum",
        "three_byte_maximum",
    ]
    assert manifest["determinism"]["path_return_route_matrix"] == (
        "flood_transport_flood_direct_zero_hop_and_transport_zero_hop"
    )
    assert manifest["determinism"]["undefined_full_ack_body_boundary"] == (
        "normal_dm_5_plus_text_len_exact_aes_block_expected_hash_only"
    )
    assert manifest["vectors"] == {
        "roundtrip": 317,
        "valid": 20,
        "invalid": 261,
        "semantic": 582,
        "total": 598,
        "packet_envelope": {
            "roundtrip": 4,
            "invalid": 5,
            "semantic": 0,
            "total": 9,
        },
        "advert_data_fields": {
            "roundtrip": 4,
            "invalid": 11,
            "semantic": 15,
            "total": 15,
        },
        "signed_advert_packet_creation": {
            "roundtrip": 3,
            "invalid": 23,
            "semantic": 26,
            "total": 26,
        },
        "signed_advert_verification": {
            "valid": 3,
            "invalid": 11,
            "semantic": 14,
            "total": 14,
        },
        "ed25519_verifier_kat": {
            "valid": 1,
            "invalid": 3,
            "semantic": 0,
            "total": 4,
        },
        "ed25519_point_validation": {
            "valid": 4,
            "invalid": 7,
            "semantic": 11,
            "total": 11,
        },
        "crypto_adapter_kat": {
            "valid": 3,
            "invalid": 0,
            "semantic": 0,
            "total": 3,
        },
        "public_group_packets": {
            "roundtrip": 4,
            "invalid": 21,
            "semantic": 25,
            "total": 25,
        },
        "anonymous_login_request_packets": {
            "roundtrip": 6,
            "invalid": 35,
            "semantic": 41,
            "total": 41,
        },
        "dm_encrypt_decrypt": {
            "roundtrip": 268,
            "invalid": 29,
            "semantic": 297,
            "total": 297,
        },
        "expected_ack_hash_and_ack_path": {
            "roundtrip": 4,
            "valid": 9,
            "invalid": 35,
            "semantic": 48,
            "total": 48,
        },
        "path_return_route_codes": {
            "roundtrip": 6,
            "invalid": 35,
            "semantic": 41,
            "total": 41,
        },
        "direct_flood_headers": {
            "roundtrip": 7,
            "invalid": 10,
            "semantic": 17,
            "total": 17,
        },
        "ack_frames": {
            "roundtrip": 5,
            "invalid": 17,
            "semantic": 22,
            "total": 22,
        },
        "trace_source_frames": {
            "roundtrip": 6,
            "invalid": 19,
            "semantic": 25,
            "total": 25,
        },
    }
    assert manifest["capabilities"][0] == {
        "id": "packet_envelope",
        "status": "implemented",
        "owner": "pinned_upstream",
        "semantic": False,
    }
    assert manifest["capabilities"][1] == {
        "id": "advert_data_fields",
        "status": "implemented",
        "owner": "pinned_upstream",
        "semantic": True,
    }
    assert manifest["capabilities"][2] == {
        "id": "signed_advert_verification",
        "status": "implemented",
        "owner": "pinned_d1l_production_ed25519",
        "semantic": True,
        "scope": "message_layout_and_signature_verification_only_no_mesh_dispatch",
    }
    assert manifest["capabilities"][3] == {
        "id": "ed25519_point_validation",
        "status": "implemented",
        "owner": "pinned_d1l_production_ed25519",
        "semantic": True,
        "scope": (
            "canonical_encoding_and_low_order_rejection_for_advert_"
            "public_key_and_signature_r"
        ),
    }
    assert manifest["capabilities"][4] == {
        "id": "direct_flood_headers",
        "status": "implemented",
        "owner": "pinned_source_golden_vectors",
        "semantic": True,
        "scope": "non_trace_direct_flood_and_zero_hop_headers",
    }
    assert manifest["capabilities"][5] == {
        "id": "ack_frames",
        "status": "implemented",
        "owner": "pinned_source_golden_vectors",
        "semantic": True,
        "scope": "simple_and_multipart_payload_framing_only",
    }
    assert manifest["capabilities"][6] == {
        "id": "trace_source_frames",
        "status": "implemented",
        "owner": "pinned_source_golden_vectors",
        "semantic": True,
        "scope": "initial_outbound_direct_flags_zero_trace_framing_only",
    }
    assert manifest["capabilities"][7] == {
        "id": "identity_signed_advert",
        "status": "pending",
        "owner": "unassigned",
        "blocked_by": "external_unpinned_ed25519_verifier_and_mesh_dispatch_fixture",
        "blocked_scope": (
            "upstream_identity_verifier_parity_and_mesh_dispatch_replay_contact_semantics"
        ),
        "blocker_receipt": {
            "id": "BLK-WP04-IDENTITY-DISPATCH-20260713",
            "status": "open",
            "observed_at": "2026-07-13",
            "pinned_sources": {
                "third_party/MeshCore/src/Identity.cpp": (
                    "21e7b533928f2d7dd5b016c048b5c4971db6d475077c793947c17574dd057bd0"
                ),
                "third_party/MeshCore/src/Dispatcher.cpp": (
                    "30b8e49a94a281e7d0c60c037d3e68d3d8882b4c82c6c98b15df981a2ce0116e"
                ),
            },
            "missing_inputs": [
                "Identity.cpp delegates verification to Ed25519.h, which is absent from the pinned MeshCore gitlink",
                "deterministic Dispatcher packet-manager, mesh-table, contact, replay, and clock fixtures",
            ],
            "blocks_execution": False,
            "unblocked_slice": "signed_advert_packet_creation",
        },
    }
    public_group = manifest["capabilities"][8]
    assert public_group["id"] == "public_group_packets"
    assert public_group["status"] == "implemented"
    assert public_group["owner"] == "pinned_upstream_utils_vendored_aes_host_sha"
    assert public_group["semantic"] is True
    assert public_group["implementation_receipt"]["id"] == (
        "RCPT-WP04-PUBLIC-GROUP-20260713"
    )
    assert public_group["implementation_receipt"]["vectors"] == {
        "roundtrip": 4,
        "invalid": 21,
        "crypto_adapter_kat": 3,
    }
    dm = manifest["capabilities"][9]
    assert dm["id"] == "dm_encrypt_decrypt"
    assert dm["status"] == "implemented"
    assert dm["owner"] == (
        "pinned_mesh_datagram_basechat_layout_vendored_aes_host_sha"
    )
    assert dm["semantic"] is True
    assert dm["implementation_receipt"]["id"] == (
        "RCPT-WP04-DM-CRYPTO-20260713"
    )
    assert dm["implementation_receipt"]["attempt_matrix"] == "0_through_255"
    assert dm["implementation_receipt"]["vectors"] == {
        "roundtrip": 268,
        "invalid": 29,
    }
    expected_ack = manifest["capabilities"][10]
    assert expected_ack["id"] == "expected_ack_hash_and_ack_path"
    assert expected_ack["status"] == "implemented"
    assert expected_ack["owner"] == (
        "pinned_basechat_ack_hash_mesh_ack_path_vendored_crypto"
    )
    assert expected_ack["semantic"] is True
    assert expected_ack["implementation_receipt"]["id"] == (
        "RCPT-WP04-EXPECTED-ACK-PATH-20260713"
    )
    assert expected_ack["implementation_receipt"]["return_path_encodings"] == [
        "zero",
        "one_byte",
        "two_byte",
        "three_byte",
    ]
    assert expected_ack["implementation_receipt"]["vectors"] == {
        "roundtrip": 4,
        "valid": 9,
        "invalid": 35,
    }
    assert expected_ack["implementation_receipt"][
        "undefined_upstream_boundary"
    ] == (
        "normal_dm_5_plus_text_len_exact_aes_block_has_deterministic_"
        "expected_hash_but_uninitialized_full_ack_body_byte"
    )
    path_return = manifest["capabilities"][13]
    assert path_return["id"] == "path_return_route_codes"
    assert path_return["status"] == "implemented"
    assert path_return["owner"] == (
        "pinned_mesh_path_return_utils_and_route_golden_vectors"
    )
    assert path_return["semantic"] is True
    assert path_return["implementation_receipt"]["id"] == (
        "RCPT-WP04-PATH-RETURN-ROUTE-CODES-20260713"
    )
    assert path_return["implementation_receipt"]["return_path_encodings"] == [
        "zero",
        "one_byte",
        "two_byte_maximum",
        "three_byte_maximum",
    ]
    assert path_return["implementation_receipt"]["route_matrix"] == [
        "flood",
        "transport_flood",
        "direct",
        "zero_hop",
        "transport_zero_hop",
    ]
    assert path_return["implementation_receipt"]["vectors"] == {
        "roundtrip": 6,
        "invalid": 35,
    }
    assert all(
        capability["status"] == "pending"
        for capability in manifest["capabilities"][7:]
        if capability["id"]
        not in {
            "public_group_packets",
            "dm_encrypt_decrypt",
            "expected_ack_hash_and_ack_path",
            "path_return_route_codes",
            "signed_advert_packet_creation",
            "anonymous_login_request_packets",
        }
    )
    assert [
        capability["id"] for capability in manifest["capabilities"][7:]
    ] == [
        "identity_signed_advert",
        "public_group_packets",
        "dm_encrypt_decrypt",
        "expected_ack_hash_and_ack_path",
        "ack_dispatch_correlation_and_delivery",
        "route_selection_and_forwarding",
        "path_return_route_codes",
        "trace_forwarding_and_path_discovery",
        "login_request_response_admin",
        "signed_advert_packet_creation",
        "anonymous_login_request_packets",
    ]
    signed_advert_packets = manifest["capabilities"][-2]
    assert signed_advert_packets["id"] == "signed_advert_packet_creation"
    assert signed_advert_packets["status"] == "implemented"
    assert signed_advert_packets["owner"] == (
        "pinned_mesh_createadvert_vendored_ed25519"
    )
    assert signed_advert_packets["implementation_receipt"]["vectors"] == {
        "roundtrip": 3,
        "invalid": 23,
    }
    anonymous_login = manifest["capabilities"][-1]
    assert anonymous_login["id"] == "anonymous_login_request_packets"
    assert anonymous_login["status"] == "implemented"
    assert anonymous_login["owner"] == (
        "pinned_basechat_anon_datagram_vendored_crypto"
    )
    assert anonymous_login["implementation_receipt"]["vectors"] == {
        "roundtrip": 6,
        "invalid": 35,
    }
    pending = {
        capability["id"]: capability
        for capability in manifest["capabilities"]
        if capability["status"] == "pending"
    }
    assert pending["ack_dispatch_correlation_and_delivery"]["blocked_by"] == (
        "deterministic_mesh_dispatch_packet_manager_tables_and_clock_fixtures"
    )
    assert pending["trace_forwarding_and_path_discovery"]["blocked_by"] == (
        "deterministic_identity_mesh_tables_radio_snr_and_clock_fixtures"
    )
    assert pending["route_selection_and_forwarding"]["blocked_by"] == (
        "deterministic_mesh_dispatch_packet_manager_tables_radio_rng_and_clock_fixtures"
    )
    assert pending["login_request_response_admin"]["blocked_by"] == (
        "identity_key_exchange_signature_and_deterministic_admin_session_fixtures"
    )

    for relative, expected in {
        **manifest["upstream"]["sources"],
        **manifest["production_binding_sources"],
        **manifest["vendored_crypto_sources"],
        **manifest["oracle_sources"],
    }.items():
        assert canonical_lf_sha256(ROOT / relative) == expected


def test_oracle_coverage_manifest_accounts_for_every_required_surface():
    wire_manifest = conformance.load_manifest()
    oracle_manifest = conformance.load_oracle_manifest()
    coverage = conformance.load_oracle_coverage_manifest(
        oracle_manifest, wire_manifest
    )
    summary = conformance.summarize_oracle_coverage(coverage, oracle_manifest)

    assert coverage == json.loads(COVERAGE_MANIFEST.read_text(encoding="utf-8"))
    assert summary["validated"] is True
    assert summary["wp04_closure_eligible"] is False
    assert summary["closure_ready"] is False
    assert summary["unsupported_closure_rejected"] is True
    assert summary["required_surface_count"] == 9
    assert summary["implemented_surface_count"] == 6
    assert summary["partial_surface_count"] == 3
    assert summary["blocked_surface_count"] == 0
    assert summary["local_packet_type_count"] == 6
    assert summary["wire_vector_covered_packet_type_count"] == 6
    assert summary["unknown_packet_type_policy"] == "fail_closed"
    assert len(summary["blocker_receipts"]) == 5
    assert len(summary["unresolved_capabilities"]) == 5
    assert len(summary["local_packet_types"]) == 6
    identity_receipt = next(
        item
        for item in summary["blocker_receipts"]
        if item["capability"] == "identity_signed_advert"
    )["receipt"]
    assert identity_receipt["id"] == "BLK-WP04-IDENTITY-DISPATCH-20260713"
    assert identity_receipt["blocks_execution"] is False
    assert identity_receipt["unblocked_slice"] == "signed_advert_packet_creation"


@pytest.mark.parametrize(
    ("declaration", "error"),
    [
        (
            "#define D1L_MESHCORE_PAYLOAD_UNREVIEWED 0x0BU",
            "local packet type changed without a coverage vector",
        ),
        (
            "#define D1L_MESHCORE_PAYLOAD_UNREVIEWED 0x0BU // unreviewed",
            "local MeshCore packet type value syntax is unsupported",
        ),
        (
            "#define D1L_MESHCORE_PAYLOAD_UNREVIEWED (0x0BU)",
            "local MeshCore packet type value syntax is unsupported",
        ),
        (
            "#define D1L_MESHCORE_PAYLOAD_VER_UNREVIEWED 0x0BU",
            "local packet type changed without a coverage vector",
        ),
    ],
)
def test_local_packet_type_addition_without_vector_fails_closed(
    tmp_path, declaration, error
):
    wire_manifest = conformance.load_manifest()
    oracle_manifest = conformance.load_oracle_manifest()
    header = ROOT / "main" / "mesh" / "meshcore_wire.h"
    changed_header = tmp_path / "meshcore_wire.h"
    changed_header.write_text(
        header.read_text(encoding="utf-8")
        + f"\n{declaration}\n",
        encoding="utf-8",
    )

    with pytest.raises(
        conformance.GateFailure,
        match=error,
    ):
        conformance.load_oracle_coverage_manifest(
            oracle_manifest,
            wire_manifest,
            packet_type_source=changed_header,
        )


def test_duplicate_local_packet_type_code_fails_closed(tmp_path):
    wire_manifest = conformance.load_manifest()
    oracle_manifest = conformance.load_oracle_manifest()
    header = ROOT / "main" / "mesh" / "meshcore_wire.h"
    changed_header = tmp_path / "meshcore_wire.h"
    changed_header.write_text(
        header.read_text(encoding="utf-8")
        + "\n#define D1L_MESHCORE_PAYLOAD_UNREVIEWED 0x0AU\n",
        encoding="utf-8",
    )

    with pytest.raises(
        conformance.GateFailure,
        match="local MeshCore packet type codes must be unique",
    ):
        conformance.load_oracle_coverage_manifest(
            oracle_manifest,
            wire_manifest,
            packet_type_source=changed_header,
        )


@pytest.mark.parametrize(
    ("mutation", "error"),
    [
        ("missing_surface", "oracle required-surface mapping drifted"),
        ("unknown_capability", "oracle required-surface mapping drifted"),
        ("valid_surface_swap", "oracle required-surface mapping drifted"),
        (
            "valid_packet_swap",
            "local packet type semantic coverage mapping drifted",
        ),
    ],
)
def test_oracle_coverage_registry_mutations_fail_closed(tmp_path, mutation, error):
    wire_manifest = conformance.load_manifest()
    oracle_manifest = conformance.load_oracle_manifest()
    coverage = json.loads(COVERAGE_MANIFEST.read_text(encoding="utf-8"))
    if mutation == "missing_surface":
        coverage["required_surfaces"].pop()
    elif mutation == "unknown_capability":
        coverage["required_surfaces"][0]["capabilities"].append(
            "unreviewed_capability"
        )
    elif mutation == "valid_surface_swap":
        public_capabilities = coverage["required_surfaces"][1]["capabilities"]
        dm_capabilities = coverage["required_surfaces"][2]["capabilities"]
        coverage["required_surfaces"][1]["capabilities"] = dm_capabilities
        coverage["required_surfaces"][2]["capabilities"] = public_capabilities
    else:
        packet_types = coverage["local_packet_type_policy"]["packet_types"]
        text_capabilities = packet_types[0]["semantic_capabilities"]
        group_capabilities = packet_types[3]["semantic_capabilities"]
        packet_types[0]["semantic_capabilities"] = group_capabilities
        packet_types[3]["semantic_capabilities"] = text_capabilities
    changed_coverage = tmp_path / "coverage_manifest.json"
    changed_coverage.write_text(
        json.dumps(coverage, indent=2) + "\n", encoding="utf-8"
    )

    with pytest.raises(conformance.GateFailure, match=error):
        conformance.load_oracle_coverage_manifest(
            oracle_manifest,
            wire_manifest,
            coverage_manifest_path=changed_coverage,
        )


def test_oracle_c_abi_wraps_pinned_protocol_helpers_and_production_s_guard():
    header = (ORACLE_ROOT / "meshcore_oracle.h").read_text(encoding="utf-8")
    adapter = (ORACLE_ROOT / "meshcore_oracle.cpp").read_text(encoding="utf-8")
    service = (ROOT / "main" / "mesh" / "meshcore_service.c").read_text(
        encoding="utf-8"
    )
    canonical_guard = (
        ROOT / "main" / "mesh" / "ed25519_canonical.h"
    ).read_text(encoding="utf-8")

    assert '"e8d3c53ba1ea863937081cd0caad759b832f3028"' in header
    assert 'extern "C"' in header
    assert "mesh::Packet upstream" in adapter
    assert "upstream.readFrom" in adapter
    assert "upstream.writeTo" in adapter
    assert "structurally_safe_to_read" in adapter
    assert "structurally_safe_to_write" in adapter
    assert "AdvertDataBuilder upstream" in adapter
    assert "AdvertDataParser upstream" in adapter
    assert "advert_layout_is_canonical" in adapter
    assert "d1l_meshcore_oracle_advert_data_decode" in header
    assert "d1l_meshcore_oracle_advert_data_encode" in header
    assert "d1l_meshcore_oracle_verify_signed_advert" in header
    assert "d1l_meshcore_oracle_create_signed_advert_packet" in header
    assert "d1l_meshcore_oracle_parse_signed_advert_packet" in header
    assert "d1l_meshcore_oracle_create_login_request_packet" in header
    assert "d1l_meshcore_oracle_parse_login_request_packet" in header
    assert "d1l_meshcore_oracle_group_channel_hash" in header
    assert "d1l_meshcore_oracle_create_group_packet" in header
    assert "d1l_meshcore_oracle_parse_group_packet" in header
    assert "d1l_meshcore_oracle_create_dm_packet" in header
    assert "d1l_meshcore_oracle_parse_dm_packet" in header
    assert "D1L_MESHCORE_ORACLE_MAX_DM_TEXT_BYTES" in header
    assert "d1l_meshcore_oracle_dm_expected_ack_hash" in header
    assert "d1l_meshcore_oracle_dm_expected_ack" in header
    assert "d1l_meshcore_oracle_create_dm_ack_path_packet" in header
    assert "d1l_meshcore_oracle_parse_dm_ack_path_packet" in header
    assert "D1L_MESHCORE_ORACLE_DM_ACK_BYTES" in header
    assert "d1l_meshcore_oracle_create_path_return_extra_packet" in header
    assert "d1l_meshcore_oracle_create_path_return_unique_packet" in header
    assert "d1l_meshcore_oracle_parse_path_return_extra_packet" in header
    assert "d1l_meshcore_oracle_parse_path_return_unique_packet" in header
    assert "D1L_MESHCORE_ORACLE_MAX_PATH_RETURN_EXTRA_BYTES" in header
    assert "ed25519_verify" in adapter
    assert "ed25519_create_keypair" in adapter
    assert "ed25519_sign" in adapter
    assert '#include "mesh/ed25519_canonical.h"' in adapter
    assert '#include "mesh/ed25519_canonical.h"' in service
    assert "d1l_ed25519_signature_s_is_canonical(signature)" in adapter
    assert "d1l_ed25519_signature_s_is_canonical(signature)" in service
    assert "d1l_ed25519_encoded_point_is_strict(public_key)" in adapter
    assert "d1l_ed25519_encoded_point_is_strict(pub_key)" in service
    assert "d1l_ed25519_encoded_point_is_strict(signature)" in adapter
    assert "d1l_ed25519_encoded_point_is_strict(signature)" in service
    assert "ge_frombytes_negate_vartime" in canonical_guard
    assert "ge_p3_dbl" in canonical_guard
    assert "multiplied_by_cofactor" in canonical_guard
    assert adapter.index("d1l_ed25519_signature_s_is_canonical(signature)") < (
        adapter.index("ed25519_verify(signature")
    )
    assert service.index("d1l_ed25519_signature_s_is_canonical(signature)") < (
        service.index("ed25519_verify(signature")
    )
    assert "D1L_ADVERT_DATA_MAX_LEN" in adapter
    assert "S >= L" in canonical_guard
    assert "d1l_meshcore_oracle_prepare_flood" in header
    assert "d1l_meshcore_oracle_prepare_direct" in header
    assert "d1l_meshcore_oracle_prepare_zero_hop" in header
    assert "d1l_meshcore_oracle_create_ack" in header
    assert "d1l_meshcore_oracle_create_multi_ack" in header
    assert "d1l_meshcore_oracle_parse_ack" in header
    assert "d1l_meshcore_oracle_create_trace" in header
    assert "d1l_meshcore_oracle_prepare_trace_direct" in header
    assert "d1l_meshcore_oracle_parse_trace_source" in header
    assert "mesh::Packet::copyPath" in adapter
    assert "PAYLOAD_TYPE_TRACE" in adapter
    assert "Identity" not in adapter
    assert "Dispatcher" not in adapter


def test_conformance_plan_builds_the_oracle_as_a_separate_target():
    commands = conformance.command_plan("clang", "clang++")
    flattened = [" ".join(command) for command in commands]

    assert any("meshcore_oracle.cpp" in command for command in flattened)
    assert any("meshcore_oracle_vectors.cpp" in command for command in flattened)
    assert any("AdvertDataHelpers.cpp" in command for command in flattened)
    assert any("meshcore_advert_data.o" in command for command in flattened)
    assert any("soft-se\\aes.c" in command or "soft-se/aes.c" in command for command in flattened)
    assert any("Utils.cpp" in command for command in flattened)
    assert any("meshcore_oracle_aes.o" in command for command in flattened)
    assert any("meshcore_oracle_utils.o" in command for command in flattened)
    packet_commands = [
        command.replace("\\", "/")
        for command in flattened
        if "src\\Packet.cpp" in command or "src/Packet.cpp" in command
    ]
    assert len(packet_commands) == 2
    assert any(
        "meshcore_conformance/stubs" in command
        and "meshcore_packet.o" in command
        for command in packet_commands
    )
    assert any(
        "meshcore_oracle/stubs" in command
        and "meshcore_oracle_packet.o" in command
        for command in packet_commands
    )
    for source in conformance.ED25519_ORACLE_SOURCES:
        source_path = str(source)
        source_commands = [
            command for command in flattened if source_path in command
        ]
        assert len(source_commands) == 1
        assert "-fsanitize=address,undefined" in source_commands[0]
        assert f"meshcore_ed25519_{source.stem}.o" in source_commands[0]
    oracle_link = next(
        command
        for command in flattened
        if command.endswith("meshcore_oracle_vectors")
    )
    assert "meshcore_oracle_packet.o" in oracle_link
    assert "meshcore_packet.o" not in oracle_link.replace(
        "meshcore_oracle_packet.o", ""
    )
    for source in conformance.ED25519_ORACLE_SOURCES:
        assert f"meshcore_ed25519_{source.stem}.o" in oracle_link
    assert any(command.endswith("meshcore_oracle_vectors") for command in flattened)
    assert all("main/mesh/meshcore_service" not in command for command in flattened)


def test_dry_run_writes_a_versioned_fail_closed_oracle_artifact(tmp_path):
    output = tmp_path / "meshcore_conformance.json"
    oracle_output = tmp_path / f"meshcore_oracle_manifest_{git_head()}.json"
    env = os.environ.copy()
    env.pop("D1L_MESHCORE_CONFORMANCE_CI", None)
    result = subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            "--commit",
            git_head(),
            "--out",
            str(output),
            "--oracle-out",
            str(oracle_output),
            "--dry-run",
        ],
        cwd=ROOT,
        env=env,
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0, result.stderr
    report = json.loads(output.read_text(encoding="utf-8"))
    artifact = json.loads(oracle_output.read_text(encoding="utf-8"))
    assert report["oracle"]["artifact_path"] == str(oracle_output)
    assert report["oracle"]["source_verification"]["verified"] is True
    assert report["oracle"]["result"] is None
    assert artifact["artifact_type"] == "meshcore_oracle_manifest"
    assert artifact["status"] == "dry_run"
    assert artifact["passed"] is False
    assert artifact["execution_complete"] is False
    assert artifact["coverage_boundary"] == BOUNDARY
    assert artifact["wp04_closure_eligible"] is False
    assert artifact["closure_ready"] is False
    assert artifact["wp04_acceptance_ready"] is False
    assert artifact["corpus_version"] == 14
    assert artifact["coverage_policy"]["validated"] is True
    assert artifact["coverage_policy"]["unsupported_closure_rejected"] is True
    assert artifact["coverage_policy"]["local_packet_type_count"] == 6
    assert artifact["repository_commit"] == git_head()
    assert artifact["upstream_commit"] == UPSTREAM_COMMIT
    assert artifact["oracle_result"] is None
    assert len(artifact["pending_capabilities"]) == 5
    assert "packet_envelope" not in artifact["pending_capabilities"]
    assert "advert_data_fields" not in artifact["pending_capabilities"]
    assert "signed_advert_packet_creation" not in artifact["pending_capabilities"]
    assert "anonymous_login_request_packets" not in artifact["pending_capabilities"]
    assert "signed_advert_verification" not in artifact["pending_capabilities"]
    assert "ed25519_point_validation" not in artifact["pending_capabilities"]
    assert "public_group_packets" not in artifact["pending_capabilities"]
    assert "dm_encrypt_decrypt" not in artifact["pending_capabilities"]
    assert "expected_ack_hash_and_ack_path" not in artifact["pending_capabilities"]
    assert "path_return_route_codes" not in artifact["pending_capabilities"]
    assert "direct_flood_headers" not in artifact["pending_capabilities"]
    assert "ack_frames" not in artifact["pending_capabilities"]
    assert "trace_source_frames" not in artifact["pending_capabilities"]


def test_oracle_vectors_compile_and_run_deterministically(tmp_path):
    compiler_pair = next(
        (
            (cxx, cc)
            for cxx, cc in (
                (shutil.which("g++"), shutil.which("gcc")),
                (shutil.which("clang++"), shutil.which("clang")),
            )
            if cxx is not None and cc is not None
        ),
        None,
    )
    if compiler_pair is None:
        pytest.skip("no matching host C/C++ compiler pair available")
    compiler, c_compiler = compiler_pair
    executable = tmp_path / (
        "meshcore_oracle_vectors.exe" if os.name == "nt" else "meshcore_oracle_vectors"
    )
    aes_object = tmp_path / "meshcore_oracle_aes.o"
    ed25519_objects = []
    subprocess.run(
        [
            c_compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            "-I",
            str(conformance.ED25519_ROOT),
            "-x",
            "c",
            "-fsyntax-only",
            "-",
        ],
        cwd=ROOT,
        input=(
            '#include "mesh/ed25519_canonical.h"\n'
            "int main(void) { unsigned char signature[64] = {0}; "
            "return d1l_ed25519_signature_s_is_canonical(signature) ? 0 : 1; }\n"
        ),
        check=True,
        capture_output=True,
        text=True,
    )
    for source in conformance.ED25519_ORACLE_SOURCES:
        object_path = tmp_path / f"meshcore_ed25519_{source.stem}.o"
        subprocess.run(
            [
                c_compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(conformance.ED25519_ROOT),
                "-c",
                str(source),
                "-o",
                str(object_path),
            ],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        ed25519_objects.append(str(object_path))
    subprocess.run(
        [
            c_compiler,
            "-std=c11",
            "-O1",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-D__CORTEX_M=0",
            "-DAES_DEC_PREKEYED=1",
            "-I",
            str(conformance.ORACLE_AES_ROOT),
            "-c",
            str(conformance.ORACLE_AES_SOURCE),
            "-o",
            str(aes_object),
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    command = [
        compiler,
        "-std=c++17",
        "-O1",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ORACLE_ROOT),
        "-I",
        str(ORACLE_ROOT / "stubs"),
        "-I",
        str(ROOT / "main"),
        "-I",
        str(conformance.ED25519_ROOT),
        "-isystem",
        str(ROOT / "third_party" / "MeshCore" / "src"),
        str(ROOT / "third_party" / "MeshCore" / "src" / "Packet.cpp"),
        str(
            ROOT
            / "third_party"
            / "MeshCore"
            / "src"
            / "helpers"
            / "AdvertDataHelpers.cpp"
        ),
        str(conformance.ORACLE_UTILS_SOURCE),
        str(ORACLE_ROOT / "meshcore_oracle.cpp"),
        str(ORACLE_ROOT / "meshcore_oracle_vectors.cpp"),
        str(aes_object),
        *ed25519_objects,
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)

    first = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    second = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
    )
    assert first.stdout == second.stdout
    assert first.stderr == second.stderr == ""
    assert json.loads(first.stdout) == {
        "passed": True,
        "coverage_boundary": BOUNDARY,
        "wp04_closure_eligible": False,
        "abi_version": 2,
        "upstream_commit": UPSTREAM_COMMIT,
        "vectors": {
            "roundtrip": 317,
            "valid": 20,
            "invalid": 261,
            "semantic": 582,
            "total": 598,
            "packet_envelope": {
                "roundtrip": 4,
                "invalid": 5,
                "semantic": 0,
                "total": 9,
            },
            "advert_data_fields": {
                "roundtrip": 4,
                "invalid": 11,
                "semantic": 15,
                "total": 15,
            },
            "signed_advert_packet_creation": {
                "roundtrip": 3,
                "invalid": 23,
                "semantic": 26,
                "total": 26,
            },
            "signed_advert_verification": {
                "valid": 3,
                "invalid": 11,
                "semantic": 14,
                "total": 14,
            },
            "ed25519_verifier_kat": {
                "valid": 1,
                "invalid": 3,
                "semantic": 0,
                "total": 4,
            },
            "ed25519_point_validation": {
                "valid": 4,
                "invalid": 7,
                "semantic": 11,
                "total": 11,
            },
            "crypto_adapter_kat": {
                "valid": 3,
                "invalid": 0,
                "semantic": 0,
                "total": 3,
            },
            "public_group_packets": {
                "roundtrip": 4,
                "invalid": 21,
                "semantic": 25,
                "total": 25,
            },
            "anonymous_login_request_packets": {
                "roundtrip": 6,
                "invalid": 35,
                "semantic": 41,
                "total": 41,
            },
            "dm_encrypt_decrypt": {
                "roundtrip": 268,
                "invalid": 29,
                "semantic": 297,
                "total": 297,
            },
            "expected_ack_hash_and_ack_path": {
                "roundtrip": 4,
                "valid": 9,
                "invalid": 35,
                "semantic": 48,
                "total": 48,
            },
            "path_return_route_codes": {
                "roundtrip": 6,
                "invalid": 35,
                "semantic": 41,
                "total": 41,
            },
            "direct_flood_headers": {
                "roundtrip": 7,
                "invalid": 10,
                "semantic": 17,
                "total": 17,
            },
            "ack_frames": {
                "roundtrip": 5,
                "invalid": 17,
                "semantic": 22,
                "total": 22,
            },
            "trace_source_frames": {
                "roundtrip": 6,
                "invalid": 19,
                "semantic": 25,
                "total": 25,
            },
        },
        "capabilities": {
            "packet_envelope": True,
            "advert_data_fields": True,
            "signed_advert_packet_creation": True,
            "signed_advert_verification": True,
            "ed25519_point_validation": True,
            "public_group_packets": True,
            "anonymous_login_request_packets": True,
            "dm_encrypt_decrypt": True,
            "expected_ack_hash_and_ack_path": True,
            "path_return_route_codes": True,
            "direct_flood_headers": True,
            "ack_frames": True,
            "trace_source_frames": True,
        },
        "failures": 0,
    }
