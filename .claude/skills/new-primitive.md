---
description: Scaffold a new DSP primitive — header, test, implementation, CMakeLists, docs
user_invocable: true
---

# New Primitive Scaffold

Create the boilerplate for a new ideath DSP primitive. The user provides the primitive name and a short description.

## Usage

`/new-primitive <Name> — <one-line description>`

Example: `/new-primitive TapeDelay — wow/flutter LFO + LP/HP feedback coloring + saturation`

## Instructions

1. Parse the primitive name and description from the argument. If missing, ask the user.

2. Read `CLAUDE.md` to review the API conventions (prepare/reset/set/process pattern), testing conventions, and the conventions section (parameter clamping, denormal protection, phase wrapping).

3. Create **three files** following the project conventions:

### `include/ideath/<Name>.h`
- `#pragma once`, `namespace ideath {}`
- Class with: `prepare(float sampleRate)`, `reset()`, relevant `set<Param>()` setters, `process(float)` returning `float`
- Default-constructible with sensible defaults
- Brief doc comment at the top

### `tests/test_<Name>.cpp`
- Include Catch2 and the header
- Helper functions (makeSine, rms, processBuffer) as needed
- Test cases tagged with `[<lowercase_name>]`:
  - Default state is valid / produces finite output
  - `reset()` clears state
  - Output range bounds check
  - Expected behavior (at least one meaningful signal test)
  - Parameter clamping at extremes
- Tests should FAIL initially (implementation is a stub)

### `src/<Name>.cpp`
- Stub implementation — `prepare()` calls `reset()`, `reset()` zeroes state, `process()` returns 0.0f
- Include `<algorithm>` and `<cmath>`
- Apply conventions: parameter clamping in setters, denormal protection if feedback state exists, phase wrapping if phase accumulator exists

4. Add to `CMakeLists.txt`:
   - Add `src/<Name>.cpp` to the library `target_sources`
   - Add `tests/test_<Name>.cpp` to the test executable

5. Add to docs:
   - `CLAUDE.md` primitives list: `- **<Name>** — <description>`
   - `README.md` primitives table: `| **<Name>** | <description> |`

6. Run `make build` to verify it compiles. Tests should FAIL at this point (TDD red phase).

7. Tell the user: "Scaffold ready. Tests are failing (TDD red). Implement `src/<Name>.cpp` next?"
