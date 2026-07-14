# D1L Build Provenance Profiles

## SIGUI D1L package v1

Build-type URI:
`https://github.com/n30nex/SIGUI/blob/main/docs/BUILD_PROVENANCE_D1L.md#sigui-d1l-package-v1`

This build type runs `scripts/package_release_d1l.py` from the exact SIGUI source
revision. It copies the already-built ESP32 firmware and optional RP2040 artifacts
into the D1L release layout, generates the SPDX SBOM and provenance statement,
writes the package manifest and README, and finally writes `SHA256SUMS.txt`.

The complete `externalParameters` schema has exactly three string fields:

| Field | Required value |
| --- | --- |
| `sourceRepository` | `https://github.com/n30nex/SIGUI` |
| `sourceRevision` | Exact 40-character lowercase Git commit |
| `releaseProfile` | `d1l` |

Stable package payloads are individual in-toto subjects with SHA-256 digests.
`manifest.json`, `README_RELEASE.md`, `SHA256SUMS.txt`, and the provenance file
itself are excluded to prevent checksum cycles. Resolved dependencies include the
root commit, both submodule gitlinks, the workflow, toolchain/configuration locks,
partition layout, notices, and the release metadata generators.

The generator omits optional invocation timestamps so identical inputs produce
identical JSON. Validate a downloaded statement by supplying the exact source tree,
package directory, and package manifest:

```text
python scripts/provenance_d1l.py --root . --source-sha <sha> \
  --package-dir <package> --package-manifest <package>/manifest.json \
  --validate <package>/provenance_<sha>.json
```

## Local builder v1

Builder URI:
`https://github.com/n30nex/SIGUI/blob/main/docs/BUILD_PROVENANCE_D1L.md#local-builder-v1`

When no complete GitHub Actions run identity is present, the same packaging script
is the self-asserted local builder. GitHub-hosted runs use
`https://github.com/actions/runner/github-hosted` instead.

Both outputs are unsigned in-toto Statements. Checksums can detect accidental
changes against trusted inputs, but they do not authenticate the statement. No
SLSA Build level is claimed. A signed envelope with a trusted signer-builder
binding is a separate release requirement.

## Reproducibility comparison

The MeshCore conformance job publishes a raw
`d1l-meshcore-wire-conformance` Actions receipt containing its execution time,
elapsed fuzz time, and run-local paths. Release packaging validates that fresh
receipt, records its exact SHA-256 identity in `manifest.json`, and packages a
canonical semantic projection under `evidence/`. The projection omits or
normalizes only those volatile receipt fields. The release audit requires the
raw receipt, its manifest binding, and the canonical projection to agree, so
canonical packaging does not replace live-run freshness evidence.

Compare two independently downloaded packages with the fail-closed full release
profile (also the CLI default):

```text
python scripts/compare_release_reproducibility_d1l.py --root . \
  --first-package <run-one-package> --second-package <run-two-package> \
  --source-sha <sha> --profile full-release --out <comparison.json>
```

`full-release` requires all three RP2040 artifact roots, each root checksum
manifest, at least one UF2 per root, and the checksum-bound Arduino build-input
receipt in the production SD-bridge artifact. `esp32-only` is an explicitly
named development profile and cannot accept a package containing RP2040 files.
