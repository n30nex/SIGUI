# MeshCore Ed25519 defined-arithmetic overlay

This directory is a SIGUI-local, source-level remediation overlay for the
orlp Ed25519 implementation pinned by the unchanged MeshCore gitlink at commit
`e8d3c53ba1ea863937081cd0caad759b832f3028`.

The original `fe.c`, `ge.c`, and `sc.c` contain signed left shifts whose left
operand can be negative. C makes those shifts undefined. The overlay replaces
those operations with multiplication by checked-in powers of two. It also
uses multiplication for the normalized signed limbs used while packing bytes,
so the overlay has one mechanically auditable rule for signed values. The four
variable-exponent operations in `ge.c::slide()` use a bounded table containing
the exact powers 1 through 64.

The deterministic materialization also removes inherited trailing spaces; it
does not otherwise reformat the pinned sources.

No MeshCore submodule bytes or gitlink are changed. Each altered file records
its exact upstream commit and SHA-256. `license.txt` is the upstream zlib
license copied verbatim, satisfying its altered-source marking requirement.

Run a portable differential check with:

```text
python scripts/validate_ed25519_defined_overlay.py --sanitizers off
```

The release-grade host proof requires Clang and enables ASan plus the UBSan
`undefined` group without source exceptions:

```text
python scripts/validate_ed25519_defined_overlay.py --sanitizers required
```

The validator fails closed on upstream or overlay drift, runs RFC 8032 section
7.1 tests 1 and 2, compares 256 deterministic baseline/overlay cases
byte-for-byte, and requires identical output from the sanitized overlay build.
