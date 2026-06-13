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
- `BowedString` — Friction-driven physical model: continuous-excitation
  sibling of `KarplusStrong`. Single waveguide delay loop with an
  analytical slip-stick friction curve
  `f = pressure × scale × v_rel × exp(-k|v_rel|)` (peak normalised
  to 1.0 so the friction reaches the negative-slope regime that drives
  self-oscillation), `tanh` saturator on the loop input, one-pole LP
  in the feedback path. Bow position implemented as a second tap into
  the same delay loop (output = `mainTap − pickupTap`) giving the
  pickup-position comb: `position=0.5` notches even harmonics,
  `position → 0.02` pushes notches above the audio band. `Damping`
  interpolates loop decay between 10 s (drone) and 0.1 s (snappy) at
  fixed compensation against LP magnitude loss at the fundamental,
  same closed-form mapping as `KarplusStrong::setDecay`
- `LowPassGate` — Buchla 292-style vactrol Low-Pass Gate. Single
  `trigger(velocity)` fires a ~1 ms exponential attack + a damping-
  controlled exponential fall (80 ms ↔ 600 ms log-linear) that drives
  BOTH a VCA and a VCF (Biquad LP). Filter cutoff is an exponential
  interpolation in Hz between `kClosedCutoff = 50 Hz` at envelope=0 and
  a `brightness`-controlled peak (50 Hz → 6 kHz) at envelope=1. Takes
  an external `process(carrier)` — caller supplies the tonal source
- `LowPassGateVoice` — Bundles a morphing saw↔square `Oscillator`
  with a `LowPassGate` for the slothrop Ping engine. Maps directly to
  the 3-knob Tone / Damping / Brightness UI

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
