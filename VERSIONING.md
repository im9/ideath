# Versioning and Release Policy

`ideath` uses semantic versioning in the form `MAJOR.MINOR.PATCH`.

Current project version is defined in [CMakeLists.txt](/Users/tn/src/libs/ideath/CMakeLists.txt).

## Compatibility Policy

### Major

Increment `MAJOR` for breaking public changes, including:

- Removing or renaming public headers
- Removing or renaming public classes, enums, methods, or namespaces
- Changing public behavior in a way that requires downstream code changes
- Changing the package target contract in a non-compatible way

### Minor

Increment `MINOR` for backward-compatible additions, including:

- New primitives
- New non-breaking API surface
- New opt-in build options
- Internal improvements that preserve downstream compatibility

### Patch

Increment `PATCH` for backward-compatible fixes, including:

- Bug fixes
- Stability fixes
- Audio-quality fixes that do not require API changes
- Build-system and CI fixes
- Documentation-only corrections

## Public API Surface

The compatibility contract applies to:

- Headers under `include/ideath/`
- The CMake package name `ideath`
- The exported CMake target `ideath::ideath`

It does not guarantee stability for:

- Internal implementation details in `src/`
- Test-only code in `tests/`
- REPL internals under `tools/repl/`
- Experimental tooling under `tools/`

### Primitive Stability Levels

Each primitive's public API (`prepare`, `reset`, `set*`, `process`) is categorized:

**Stable** — API will not change without a major version bump:
Oscillator, Biquad, SVFilter, DecayEnvelope, AdsrEnvelope, Noise, Saturation,
Wavetable, BitCrusher, DelayLine, LFO, Portamento, Compressor, PeakLimiter,
Wavefolder, Reverb, HallReverb, UnisonOscillator, FeedbackBuffer

**Maturing** — API is unlikely to change but may receive non-breaking additions:
Voice, Polyphony, FMSynth, ShimmerReverb, CombFilter, TapeDelay, FormantFilter

**Experimental** — API may change in minor releases:
(none currently — new primitives start here until they ship in a product)

## Release Expectations

Before creating a release:

1. Update the version in `CMakeLists.txt` if needed.
2. Run the full test suite.
3. Ensure CI is green on supported platforms.
4. Review whether docs need updates for public-facing changes.
5. Update [CHANGELOG.md](CHANGELOG.md) with notable changes.
6. Summarize notable changes in release notes.

## Initial Stability Note

Until `1.0.0`, minor releases may still include carefully considered breaking changes if they materially improve the library. When that happens, call them out explicitly in release notes.
