# Changelog

All notable changes to `ideath` should be documented in this file.

The format is based on Keep a Changelog, adapted for the project's current stage.
`ideath` follows semantic versioning as described in [VERSIONING.md](VERSIONING.md).

## [Unreleased]

### Changed

- `BowedString` rebuilt as a two-segment waveguide (bow splits the string into
  bridge and nut segments, Smith / STK bowed-string layout) with a bow-table
  junction. The old single loop applied the friction map once per period and
  period-doubled: at the pressures where it began to speak ≈ 100 % of the
  energy sat on (n+½)·f0 (an octave low), and it went chaotic above them. The
  new model sounds f0 in Helmholtz motion. Public setters unchanged; semantics:
  pressure is now bow force relative to bow speed, position is the bow's
  distance from the bridge (clamped to 0.02-0.45, was 0.02-0.5), damping maps
  to a 10 s → 1 s ring-out (was 10 s → 0.1 s) plus a bridge low-pass.
  Output ceiling ±3.0 (was ±2.0). `kFrictionK` / `kFrictionScale` removed;
  `setBowVelocity(0)` with pressure no longer self-excites
- `KarplusStrong` tuning: the line delay is one period minus the in-loop
  low-pass's phase delay at f0, recomputed on pitch and damping changes. The
  loop was a whole LP phase delay too long and rang flat (−10 c at C4 /
  −38 c at C6 with damping 0.5)

### Added

- `BowedString::minimumPressure()` (and a static overload taking sample rate,
  frequency, position, damping) — the Schelleng minimum pressure, from the
  linearised loop's characteristic equation
- `KarplusStrong::getLineDelaySamples()`

- `GranularProcessor` additions, all bit-identical to the previous behaviour
  at their defaults: `setSos(amount)` overdubs instead of overwriting (1 keeps
  the buffer and holds the write head, same as freeze);
  `setPlaybackDirection(reverse)` launches backward grains, which fade through
  the write-head seam; `setSeed(seed)` sets the RNG seed that `reset()` returns
  to. REPL: `granular sos`, `granular reverse`, `granular seed`
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
- `BowedString` — Bowed-string physical model: continuous-excitation
  sibling of `KarplusStrong`. Two-segment digital waveguide split at the
  bow (bridge segment ending in −g·LP, nut segment ending in −1, loop tuned
  to one period) with Smith's bow-table junction, so the string moves in
  Helmholtz motion at f0. Pressure (relative to bow speed), position
  (0.02-0.45 from the bridge), damping (ring-out 10 s → 1 s, darker bridge),
  bow velocity, and `minimumPressure()` for the Schelleng threshold (see
  Changed above for the rebuild from the first single-loop version)
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
- `HallReverb::setDamp` uses the canonical Freeverb damping scale (0–0.4,
  was 0–0.12) so damp 1 audibly darkens the tail; damp 0 is bit-identical
- `ShimmerReverb::setSize` now sets the decay time (nominal T60 0.5 s →
  ≈ 30 s, exponential in size); `setShimmer` is now the equal-power blend
  of unshifted and octave-up feedback instead of a feedback-gain offset
- `ShimmerReverb::setDamp` is linear in dB of per-pass attenuation at
  4 kHz (0–12 dB), removing the cliff in the last 10 % of the range

### Fixed

- `ShimmerReverb` modulated allpasses wrapped their read position past the
  64-sample headroom for size > 0.32 (LFO excursion reaches 147 samples)
- `ShimmerReverb` modulated allpasses used `y = w[n−D] − g·x`, which is not
  allpass (peaks ≈ +1.5 dB per stage); now the textbook Schroeder form
- `ShimmerReverb` octave-up shifter is preceded by an anti-alias lowpass
  (sr/8) so content above sr/4 cannot fold back onto itself and make the
  loop gain exceed 1
- `Reverb` / `HallReverb` freeze now mutes the comb input (canonical
  Freeverb); input during freeze previously accumulated in the lossless loop
- `ShimmerReverb::reset()` restores the allpass LFO phases, so output after
  reset matches a freshly prepared instance
- `ShimmerReverb` allpass LFO depth scales with sample rate (≈ 1.1 ms at
  modScale 1); it was fixed at 49 samples, halving the modulation at 96 kHz
  and wrapping the read pointer below ~11 kHz

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
