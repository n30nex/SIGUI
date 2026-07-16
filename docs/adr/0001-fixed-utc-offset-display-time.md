# ADR 0001: Fixed UTC offset for display time

- Status: Accepted for the pre-1.0 firmware
- Date: 2026-07-15
- Scope: WP-12 display-time setting and conversion

## Context

SIGUI needs a persisted timezone setting, but the release roadmap does not yet
choose an IANA timezone UX or bundle a timezone database. Protocol timestamps,
certificate checks, and ordering already use centralized UTC/monotonic clocks
and must not inherit presentation policy.

## Decision

The default is UTC. A user may opt into one validated fixed offset from
`UTC-12:00` through `UTC+14:00`, including minute offsets. The firmware renders
the setting explicitly as `UTC`, `UTC-HH:MM`, or `UTC+HH:MM` and applies it only
when formatting display time. Approximate retained time keeps a `~` marker.

This model does not claim an IANA zone and does not adjust for daylight saving
time automatically. The console and Display UI disclose that boundary.

## Consequences

- Protocol allocation, certificate validity, and stored wall-clock evidence
  remain UTC and cannot move because the display offset changes.
- Invalid schema or offset fields sanitize independently to UTC, preventing a
  bad display setting from causing a boot loop.
- A later IANA-zone implementation can add a new setting schema and migrate the
  fixed offset without reinterpreting existing values.
