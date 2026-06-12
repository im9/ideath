# Changelog

All notable changes to `ideath` should be documented in this file.

The format is based on Keep a Changelog, adapted for the project's current stage.
`ideath` follows semantic versioning as described in [VERSIONING.md](VERSIONING.md).

## [Unreleased]

### Added

- CMake package export and install support for `find_package(ideath CONFIG REQUIRED)`
- Exported CMake target namespace `ideath::ideath`
- GitHub Actions CI for Linux and macOS build/test/install/package-consumer verification
- Contributor guide in [CONTRIBUTING.md](CONTRIBUTING.md)
- Versioning and release policy in [VERSIONING.md](VERSIONING.md)
- Assessment document in [ASSESSMENT.md](ASSESSMENT.md)
- Catch2 benchmark executable and `make bench` workflow for primitive and reference-chain timing
- New primitives: `TapeDelay`, `CombFilter`, and `FormantFilter`
- REPL commands for `TapeDelay` and `CombFilter`
- `AREnvelope` — attack/sustain/release envelope for slow per-layer fades
  (added alongside `DecayEnvelope` and `AdsrEnvelope` in `ideath/Envelope.h`)
- `UnisonOscillator` analog drift: `setDriftAmount(cents)` and
  `setDriftRate(hz)`. Each voice gets an independent slow sine LFO with
  per-voice rate jitter so voices wander uncorrelated. Default amount is
  `0.0` cents — pre-existing call sites are bit-identical
  (`getVoiceDriftCents()` exposed for tests/debug)
- `HarmonicOscillator` — Plaits-style additive engine with up to 32
  harmonically-related sine partials. High-level Plaits-style
  `setBands(low, mid, high, shape)` mapping (LOW = partials 1..3,
  MID = partials 4..7, HIGH = partials 8..32, `shape` linearly tapers
  within each band) plus low-level `setPartialAmplitude(idx, amp)` for
  per-partial modulation. Partials above `sampleRate × 0.45` are
  silently muted to prevent aliasing fold. Phase init is deterministic
  via a fixed xorshift32 seed so renders are bit-exact reproducible

### Changed

- README usage examples now prefer `ideath::ideath`
- README now links to project governance and assessment docs
- REPL signal-chain documentation now reflects the current processing order

## Release Notes Template

Use this structure for future releases:

```md
## [X.Y.Z] - YYYY-MM-DD

### Added
- ...

### Changed
- ...

### Fixed
- ...

### Removed
- ...
```
