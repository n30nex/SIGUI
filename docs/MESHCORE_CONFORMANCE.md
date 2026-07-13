# MeshCore Conformance Boundary

Issue #65 is the fail-closed tracker for MeshCore protocol conformance. The
first CI slice is intentionally limited to the packet wire envelope. Its
machine-readable result must report:

- `coverage_level="wire_envelope_only"`
- `closure_ready=false`

A green result is required before the firmware job may run, but it is not
eligible to close issue #65 or to qualify the firmware for release.

## Pinned Oracle

The structural oracle is the recursively checked-out MeshCore submodule at
`third_party/MeshCore`, currently pinned by the repository gitlink to
`e8d3c53ba1ea863937081cd0caad759b832f3028`. CI compares the original D1L C
wire codec with the packet-envelope implementation at that exact pin. It does
not compare against a moving branch or a separately installed MeshCore copy.

`tests/meshcore_conformance/manifest.json` locks the upstream commit and source
hashes together with the vector and fuzz contract. Because Git may check text
out with CRLF on Windows, tracked text is hashed in its canonical LF form; all
other byte changes still invalidate the pin. The runner must fail closed if the
gitlink, checked-out submodule, source hashes, or manifest disagree.

Any MeshCore gitlink change invalidates the prior result. A pin update requires
an intentional review of the adapter, vector matrix, malformed cases, and this
boundary before new evidence can be accepted.

The upstream native-test AES and SHA mocks are deliberately excluded as
cryptographic oracles because they do not implement production cryptography.

## WP-04 Oracle ABI Foundation

`tests/meshcore_oracle/meshcore_oracle.h` defines version 2 of a narrow C ABI
around the pinned upstream `mesh::Packet` envelope reader/writer and the
`AdvertDataBuilder`/`AdvertDataParser` app-data helpers. Its version 8 static
`manifest.json` binds that interface, the exact upstream commit, every source
used by the target, and all deterministic vectors by canonical-LF SHA-256. The
packet capability retains four round-trip and five reject vectors. The advert
field capability adds four canonical round trips and eleven invalid-boundary
vectors covering type, location, feature fields, name, truncation, and
reject-without-output-mutation behavior. Seven direct, flood, transport, and
zero-hop preparation round trips plus ten rejects are golden-bound to the
pinned `Mesh.cpp` header/path/priority rules. Non-transport results explicitly
clear ignored transport fields so every accepted C-ABI result has a canonical
wire round trip. Five simple/multipart ACK framing round trips plus seventeen
reject vectors are also golden-bound to `Mesh::createAck` and
`Mesh::createMultiAck`. They cover the payload type, multipart subtype,
remaining-count nibble, the dispatcher's four-byte minimum ACK body, payload
limits, version gate, deterministic wire round trip, and
reject-without-output-mutation behavior. ACK constructors return a canonical
pre-route layout; vectors apply the pinned zero-hop route preparation before
encoding, so that intermediate header is never presented as production wire
fidelity. The `meshcore-conformance` job builds and runs this as a separate
sanitized host target. Six initial outbound TRACE round trips plus nineteen
reject vectors cover the pinned nine-byte tag/auth/flags prefix, flags-zero
contract, zero-to-63 one-byte route hashes appended to the payload, direct
route, zero outer path, priority five, fixed little-endian bytes on the
supported targets, and reject-without-output-mutation behavior. TRACE creation
is likewise pre-route and is never encoded before direct preparation. The job
also compiles the real C Ed25519 verifier vendored at the pinned MeshCore
gitlink. Three fixed production-layout signed-advert vectors cover empty,
named, and maximum-size app data; one independent RFC 8032 empty-message
known-answer vector checks the verifier directly. Eleven signed-advert rejects
cover null or oversized input, changed public key, timestamp, app data or
signature, and a signature whose scalar is increased by the Ed25519 group
order, plus the pinned verifier's identity-public-key/identity-R/zero-S
message-independent forgery. Four accepted point vectors and seven rejected
point vectors cover the fixed-seed and RFC public keys/signature-R values,
null input, non-canonical field encoding, and representative order-one,
order-two, and order-four encodings. The separate verifier KAT accounts for
one valid and three negative or
canonicality-regression cases; it is not counted as an advert semantic vector
or as a round trip. The production receive path and oracle share a strict
`S < L` guard plus canonical-encoding and low-order checks for the advert
public key and signature R before the pinned verifier. These close the
verifier's otherwise malleable `S + L` acceptance and the deterministic
identity-point forgery. Valid vectors run repeatedly under ASan and UBSan.
Seed bytes `00` through `1f` regenerate the keypair and all three fixed
signatures on every vector run. The source hashes pin the verifier's transitive
C headers and sources, the keypair/signer recipe, `advert_data.h`, the shared
canonical scalar/point guard, and the D1L CMake/service binding. No crypto mock or
invented stub is used. The job emits
`meshcore_oracle_manifest_<full-commit>.json` beside the existing conformance
report.

`tests/meshcore_oracle/coverage_manifest.json` is the machine-readable WP-04
boundary receipt. It accounts for all nine roadmap surfaces, every oracle
capability, the current unresolved capability IDs, and the reviewed
upstream-commit/corpus-version pair. The main oracle manifest pins this policy
file by canonical-LF SHA-256. The conformance runner derives all numeric
`D1L_MESHCORE_PAYLOAD_*` declarations from `main/mesh/meshcore_wire.h` (while
explicitly excluding the `D1L_MESHCORE_PAYLOAD_VER_1` version constant) and
requires exact equality with both the policy registry and the six-type wire
vector matrix. Consequently a new local payload type, a duplicate code, a
missing roadmap surface, or an unknown capability fails before compilation.
Envelope-vector coverage remains distinct from semantic coverage: the current
six local types all have envelope vectors, while the policy reports one fully
implemented roadmap surface, three partial surfaces, and five blocked surfaces.
The exact packet registry and blocker receipts are copied into
`meshcore_oracle_manifest_<full-commit>.json`; `wp04_acceptance_ready` and
`closure_ready` remain false.

This foundation is intentionally not the completed WP-04 oracle. Its boundary
is
`pinned_upstream_packet_advert_route_ack_trace_and_strict_signed_advert_verification`.
The new `signed_advert_verification` capability proves only D1L's bounded
message layout (`public_key || timestamp_wire_bytes || app_data`) and the real
vendored C verifier. Upstream `Identity.cpp` deliberately delegates to a
separate external `Ed25519::verify` implementation whose `Ed25519.h` is absent
from the pinned gitlink, and signed-advert dispatch, replay/timestamp policy, contact
mutation, and duplicate suppression remain inside `Mesh`. Therefore
`identity_signed_advert` stays pending under
`BLK-WP04-IDENTITY-DISPATCH-20260713`, which pins `Identity.cpp` and
`Dispatcher.cpp` and names the missing external verifier plus deterministic
packet-manager, table, contact, replay, and clock fixtures. That blocker does
not prevent the strict-point slice or other independent oracle work;
the new primitive must not be cited as upstream Identity parity or dispatch
closure. The route-header capability likewise
does not claim queue timing, route selection, retransmission, or forwarding;
those remain `route_selection_and_forwarding`. Public and DM semantics,
production encryption/decryption, expected-ACK hash derivation, encrypted
ACK+PATH handling, ACK correlation/delivery state, PATH-return behavior, TRACE
forwarding/path discovery, and login/admin flows also remain pending, so both
`wp04_closure_eligible` and `closure_ready` remain false. The expected-ACK and
ACK+PATH remainder is
explicitly blocked by the unpinned external SHA/AES implementations and missing
mesh-session fixtures; ACK framing must not be cited as evidence for those
semantics. ACK dispatch, correlation, and delivery state remain a separate
pending capability blocked on deterministic Mesh dispatch, packet-manager,
table, and clock fixtures. Path-return creation is separately blocked by the
unpinned AES/SHA implementation plus RNG, identity, and mesh-session fixtures.
TRACE framing does not authenticate `auth_code` or prove discovery; forwarding
still requires identity matching, duplicate tables, radio SNR, scheduling, and
clock fixtures. Those stages require deterministic radio, RNG, RTC,
millisecond-clock, packet manager, mesh-table, contact, and channel fixtures;
they must extend the versioned manifest instead of silently widening what this
artifact claims.

The three previously implicit gaps now have explicit fail-closed receipts.
DM encrypt/decrypt requires the unpinned AES, SHA, and Ed25519 implementations
plus identity and mesh-session fixtures. Route selection/forwarding requires a
deterministic dispatcher, packet manager, mesh tables, radio, RNG, and clocks.
Login/request/response/admin requires the same production crypto boundary plus
identity and deterministic admin-session fixtures. These receipts describe
missing oracle prerequisites; they are not evidence that those semantics ran.

## What This Slice Covers

The `meshcore-conformance` Actions job runs on `ubuntu-24.04` with the pinned
`clang-18`/`clang++-18` packages. The runner builds the local wire codec and
the pinned upstream packet-envelope code into a structural vector harness with
AddressSanitizer and UndefinedBehaviorSanitizer. It separately builds the local
wire decoder as the libFuzzer target, then:

1. exercises valid packet envelopes in both directions;
2. checks route type, payload type/version, transport prefixes, path hash
   widths/counts, and payload boundaries, including the upstream 184-byte
   maximum packet payload;
3. rejects truncated, overlong, reserved, and internally inconsistent local
   receive inputs; and
4. runs 100,000 deterministic local-parser fuzz inputs with seed `13746277`
   (`0xD1C065`).

The valid matrix may carry bytes labelled as text, ACK, advert, group text,
PATH, or multipart packets. In this slice those labels prove only that the
packet-type byte and envelope survive the round trip; they do not prove the
meaning, authentication, or state transition for any packet type.

The pinned `Packet::readFrom` envelope path and
`d1l_meshcore_wire_decode()` intentionally expose all four two-bit header
version values so the structural oracle can compare packet bytes without
pretending to be the Dispatcher. The separate
`d1l_meshcore_wire_decode_v1()` boundary accepts only value zero
(`PAYLOAD_VER_1`) and leaves its output unchanged for malformed or future
versions. The fixed harness and local-parser fuzz target exercise both
properties. All five production receive handlers call the v1 boundary before
crypto or retained-store mutation. This closes version gating only; the
artifact must not claim the remaining Dispatcher semantics are covered.

The job writes
`artifacts/meshcore-conformance/meshcore_conformance_<full-commit>.json` and
uploads it as `d1l-meshcore-wire-conformance`. Evidence is acceptable only
when it identifies the tested project commit and upstream gitlink, retains the
seed/run/vector/malformed counts plus observed fuzz duration, reports zero
sanitizer or canary failures, and keeps the two boundary fields above
unchanged. Any libFuzzer crash/timeout input is written beside the JSON under
the uploaded `artifacts/meshcore-conformance` tree rather than discarded with
the temporary build directory.

## Explicit Non-Coverage

This bounded gate makes no claim about:

- encryption, decryption, MACs, signing/key generation, channel secrets, or
  full identity semantics. Only the bounded D1L signed-advert message layout
  and pinned C Ed25519 verification primitive with canonical scalar/point and
  low-order rejection described above are covered;
- semantic dispatch for Public, DM, advert, PATH, trace, or general multipart
  traffic. Simple/multipart ACK and initial flags-zero TRACE payload framing
  are covered, but expected hash derivation, encrypted ACK+PATH handling, TRACE
  forwarding/SNR/path discovery, correlation, and remaining Dispatcher
  behavior are not;
- ACK timing, retry/delivery state, duplicate or replay control, packet
  lifetime, route selection/fallback, or self-message behavior;
- persisted/retained state, schema migration, reboot recovery, or write
  durability;
- radio behavior, interoperability with an official client or second radio,
  or any hardware path; or
- the complete declared MeshCore 1.0 surface or release readiness.

Accordingly, a passing artifact must still say `closure_ready=false`. Full
issue #65 closure requires later semantic, production-cryptography,
duplicate/replay/lifetime, retained-state, and real-peer coverage against the
entire declared surface. Those later stages must not reinterpret this artifact
as evidence they ran.

## CI Reproduction Contract

The authoritative run is GitHub Actions, not a local firmware build:

```bash
python ./scripts/meshcore_conformance_d1l.py \
  --cc clang-18 \
  --cxx clang++-18 \
  --commit "$GITHUB_SHA" \
  --seed 13746277 \
  --runs 100000 \
  --out "artifacts/meshcore-conformance/meshcore_conformance_${GITHUB_SHA}.json"
```

The firmware job depends on this job succeeding. That dependency prevents a
known structural regression from producing a release package; it does not turn
the structural check into a full protocol or production release gate.

The package step copies the exact current-commit JSON and records its size,
SHA-256, source commit, generation/expiry time, boundary, and non-closure flags.
The release audit also requires an exact clean package source tree, an explicit
selected Actions run ID, and that run's exact-commit host-check success marker.
Mismatched, cross-run, red-host-check, dirty, tampered, path-escaping,
out-of-range, expired, or closure-claiming structural evidence fails closed.
