# Contributing to ideath

Thanks for contributing. `ideath` is a DSP foundation library with strict real-time and audio-quality constraints, so correctness and clarity matter more than feature volume.

## Ground Rules

- Keep the library JUCE-free.
- Preserve real-time safety.
  - No allocation in `process()`
  - No locks in audio-rate paths
  - No exceptions as control flow in DSP code
- Keep parameters in raw physical units.
  - Hz, seconds, dB, normalized audio ranges where appropriate
- Keep public APIs small and explicit.
- Fix audible problems at the root instead of adding host-layer workarounds.

## Development Setup

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

Or:

```bash
make build
make test
```

## Contribution Workflow

1. Open an issue or describe the problem clearly if the change is non-trivial.
2. Keep changes scoped.
3. Add or update tests first when fixing behavior.
4. Run the full test suite before submitting.
5. Update docs when public behavior, build flow, or supported modules change.

## Code Guidelines

- C++17 only.
- Public headers live in `include/ideath/`.
- Implementations live in `src/`.
- Tests live in `tests/test_<Name>.cpp`.
- Avoid `using namespace` in headers.
- Prefer explicit naming over clever abstractions.
- Clamp external inputs in `set<Param>()`.
- Keep phase accumulators wrapped and feedback/stateful code denormal-safe.

## Testing Expectations

Every new primitive or materially changed primitive should have tests for:

- Output range / boundedness
- Expected behavior
- Reset/state behavior
- Edge cases and parameter clamping
- Stability under extreme parameters when relevant

For audible issues, add a regression test whenever possible, even if it must be an offline/proxy test.

## Documentation Expectations

Update the relevant docs when needed:

- `README.md` for public-facing usage and feature surface
- `CLAUDE.md` for project conventions and internal working rules
- `ASSESSMENT.md` when a major maturity gap is closed or a new one is discovered
- `VERSIONING.md` when compatibility or release policy changes

## Pull Request Checklist

- The change is scoped and explained clearly.
- Tests were added or updated where appropriate.
- `ctest --test-dir build --output-on-failure` passes locally.
- Docs were updated if the public or developer-facing contract changed.
- No unrelated cleanup was mixed in.

## Commit Style

- Use concise imperative commit messages.
- Prefer one coherent change per commit.

Examples:

- `Add CMake package export support`
- `Fix sequencer retrigger click in REPL`
- `Add regression test for SVFilter modulation stability`
