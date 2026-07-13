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

`tests/meshcore_oracle/meshcore_oracle.h` defines version 1 of a narrow C ABI
around the pinned upstream `mesh::Packet` envelope reader/writer and the
`AdvertDataBuilder`/`AdvertDataParser` app-data helpers. Its version 4 static
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
sanitized host target and emits
`meshcore_oracle_manifest_<full-commit>.json` beside the existing conformance
report.

This foundation is intentionally not the completed WP-04 oracle. Its boundary
is `pinned_upstream_packet_advert_route_and_ack_frames`; the advert
vectors cover only canonical application fields, not a complete signed Mesh
advert. The pinned repository does not vendor the production
`Ed25519::verify` implementation used by `Identity.cpp`, and signed advert
dispatch remains inside `Mesh`, so `identity_signed_advert` stays pending with
that blocker recorded in the manifest. The route-header capability likewise
does not claim queue timing, route selection, retransmission, or forwarding;
those remain `route_selection_and_forwarding`. Public and DM semantics,
production encryption/decryption, expected-ACK hash derivation, encrypted
ACK+PATH handling, ACK correlation/delivery state, PATH/trace behavior, and
login/admin flows also remain pending, so both `wp04_closure_eligible` and
`closure_ready` remain false. The expected-ACK and ACK+PATH remainder is
explicitly blocked by the unpinned external SHA/AES implementations and missing
mesh-session fixtures; ACK framing must not be cited as evidence for those
semantics. ACK dispatch, correlation, and delivery state remain a separate
pending capability blocked on deterministic Mesh dispatch, packet-manager,
table, and clock fixtures. Those stages require deterministic radio, RNG, RTC,
millisecond-clock, packet manager, mesh-table, contact, and channel fixtures;
they must extend the versioned manifest instead of silently widening what this
artifact claims.

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

- encryption, decryption, MACs, signatures, keys, channel secrets, or identity;
- semantic dispatch for Public, DM, advert, PATH, trace, or general multipart
  traffic. Simple and multipart ACK payload framing is covered, but expected
  hash derivation, encrypted ACK+PATH handling, correlation, and remaining
  Dispatcher behavior are not;
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
