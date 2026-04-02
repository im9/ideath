---
description: Full DSP library design audit — checks all primitives for real-time safety, denormals, numerical stability, and API consistency
user_invocable: true
---

# DSP Library Audit

Perform a comprehensive design audit of the iDEATH DSP library. Use the Explore agent to read ALL source files and report issues.

## Instructions

1. Launch an Explore agent (subagent_type=Explore, thoroughness=very thorough) with the following prompt:

> Thoroughly audit the iDEATH DSP library. Read ALL files in `include/ideath/*.h` and `src/*.cpp`.
>
> Check for these categories:
>
> 1. **Real-time safety**: allocation, exceptions, locks, or I/O in process() paths
> 2. **Denormal floats**: exponential decays approaching zero without flush-to-zero guard (causes CPU spikes on x86)
> 3. **Numerical stability**: division by zero, NaN propagation, filter coefficients at extreme frequencies (near Nyquist)
> 4. **State management**: reset() not clearing all state, prepare() not reinitializing
> 5. **API inconsistency**: primitives not following prepare/reset/set/process pattern
> 6. **Parameter range**: missing clamping on critical params (filter freq vs Nyquist, feedback >= 1.0, Q extremes)
> 7. **Audio artifacts**: clicks from state discontinuities, DC offset accumulation, feedback instability
>
> For each finding report:
> - File path and line number
> - Severity: critical / moderate / minor
> - Description of the issue
> - Suggested fix
>
> Be thorough — read every file. Focus on real issues, not style.

2. Present the results to the user as a table grouped by severity (critical first).

3. Ask whether to fix any of the issues found.
