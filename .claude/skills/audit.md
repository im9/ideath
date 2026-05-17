---
description: Comprehensive DSP library audit — local quality + reference fidelity + cross-cutting consistency. Runs parallel Explore agents and synthesises a prioritised findings table.
user_invocable: true
---

# DSP Library Audit

Full design audit of the iDEATH DSP library.  This skill exists because a previous narrower audit (real-time safety / denormals / numerical stability only) silently missed real bugs:

- **Reverb** copied `fixedgain = 0.015f` from canonical Freeverb but dropped `scalewet = 3.0f`, leaving the wet bus at ~1/3 the expected level.
- **ShimmerReverb** pitch shifter computed `delay = phase * N` (delay grows) when its own comment correctly said "Implemented as decreasing delay" — the read pointer was stationary, no octave-up content was produced.
- **Voice** kept the legacy Env→Filter chain order even though commit 3b939e7 reordered the REPL to Osc→Filter→Env to fix retrigger clicks.  Plugins built on Voice (rather than the REPL) hit the exact bug that commit claimed to fix.
- **AdsrEnvelope** `setCurve` shaped Attack and Release but missed Retrigger; a noteOn during a curved release jumped from `releaseStartLevel * pow(level/releaseStartLevel, exp)` straight back up to bare `level`.
- **FMSynth** algorithms 5 and 6 were byte-identical with only a stale comment differentiating them — users got 7 distinct FM routings instead of the documented 8.

Every category below has a concrete bug it would have caught.  When auditing, treat the bug examples as templates for what to look for.

## How to run

Spawn the following agents in parallel via the Agent tool (`subagent_type=Explore`).  Each per-group agent gets the full category list.  The cross-cutting agent has its own briefing.

| Agent | Files |
|---|---|
| **Group A — Filters & oscillators** | Biquad, SVFilter, CombFilter, FormantFilter, Oscillator, UnisonOscillator, Wavetable |
| **Group B — Envelopes & modulation** | DecayEnvelope, AdsrEnvelope, AREnvelope, LFO, Portamento |
| **Group C — Delay-based effects** | DelayLine, TapeDelay, FeedbackBuffer, BitCrusher |
| **Group D — Dynamics & shapers** | Compressor, PeakLimiter, Saturation, Wavefolder, Noise, BandlimitedNoise |
| **Group E — Reverbs** | Reverb, HallReverb, ShimmerReverb |
| **Group F — Composite** | Voice, Polyphony, FMSynth |
| **Cross-cutting** | `git log --grep="fix:"`, CLAUDE.md backlog, REPL ↔ Voice consistency |

For each agent, brief it with the categories below as a checklist, the bug examples as templates, and the expected output format.  Tell it to be **conservative**: false positives waste review time more than false negatives (we can re-audit).

## Categories

### Local quality (per-primitive)

1. **Real-time safety** — no allocation, exceptions, locks, or I/O in any `process()` path.  Allocation is allowed only in `prepare()`.
2. **Denormal protection** — `+ 1e-25f` DC injection on linear feedback state (filters, delay buffers); flush-to-zero threshold on exponential decays (envelopes, compressor envelopes).  Required by repo convention (CLAUDE.md).
3. **Numerical stability** — division by zero, NaN propagation, `log(0)`, `pow(neg, frac)`, filter coefs at extreme frequencies near Nyquist, `tanh(huge)` for runaway drives.
4. **State management** — `reset()` clears every member; `prepare()` reinitialises every coefficient that depends on sample rate; LFO phases / delay indices restored by reset (not just buffers).
5. **API consistency** — every primitive follows `prepare / reset / set<Param> / process` and is default-constructible with a 44100 Hz fallback.
6. **Parameter clamping** — every public setter clamps inputs *before* storing or computing coefficients.  Frequencies to `[minHz, sr*0.45]`, Q/resonance to `[floor, ceiling]`, times to `[small positive, max]`.

### Reference fidelity

7. **Reference completeness** — when code cites a canonical reference (`// canonical Freeverb`, `// RBJ cookbook`, `// Cytomic TPT`, `// DX7-style`, `// YM2612`), verify ALL the defining constants and structures are present, not just some of them.  Use WebFetch sparingly to confirm the canonical values if you're not sure.
   - Bug template: Reverb's `kInputGain = 0.015f` matched canonical Freeverb's `fixedgain` exactly, but the matching `scalewet = 3.0f` output scale was silently absent.  Tests only checked bounds and energy, never absolute wet level, so the mismatch slipped through.

8. **Comment vs implementation agreement** — when a function/inline comment promises specific behaviour or describes a specific algorithm, verify the code actually does it.  Read the comment, then trace the code as if you didn't trust either, and check they agree.
   - Bug template: ShimmerReverb's pitch shifter had a correct comment ("Implemented as decreasing delay: phase advances at (ratio-1)/windowSize") but the very next line computed `delay = phase * kWindowSize`, which makes delay *grow* by 1/sample.  The read position never moved.  The comment had the right algorithm; the implementation got the sign wrong.

### Coverage / completeness

9. **Enum / algorithm distinctness** — for any switch/case dispatch over an enum or algorithm index, verify each branch is functionally distinct.  Read every case body and check that no two are byte-identical.  Where the enum exposes `kNum<X>` size, verify that count actually matches the documented intent.
   - Bug template: `FMSynth::process` handled 8 algorithms in a switch, but case 5 and case 6 contained byte-identical routing (`o4=op4.tick(0); o3=op3.tick(o4); o2=op2.tick(0); o1=op1.tick(0); out=o3+o2+o1`).  Case 6's only difference was a stale comment promising "OP1 with feedback emphasis" that the code never delivered.

10. **State × Modifier matrix coverage** — for state machines (e.g. AdsrEnvelope's `Stage`, LFO's `Waveform`), when a new modifier (`curve`, `shape`, `quantize`) is added, verify it is honoured in *every reachable state*, not just the obvious ones.  Build the matrix mentally: `for each state × for each new modifier` — does the test suite or the code path exercise that cell?
    - Bug template: `AdsrEnvelope::setCurve` was added with shaping branches for Attack and Release but Retrigger was forgotten.  When a noteOn fired during a curved release the output reverted from the release-curve formula back to bare `level_`, producing a jump of ~0.16 — exactly the click the retrigger fade exists to suppress.

### Cross-cutting (whole-codebase view, run as a separate agent)

11. **Cross-cutting fix coherence** — When `git log --grep="fix:"` or CLAUDE.md documents a fix (e.g. "reordered the signal chain to Osc → Filter → Env", "replaced Biquad with SVFilter"), verify EVERY file that implements the same concept reflects that fix.  CLAUDE.md may list a known issue as "fixed" but only one of two implementing files might have actually been updated.
    - Bug template: commit 3b939e7 reordered the REPL `tools/repl/AudioEngine.cpp` signal chain to Osc → Filter → Env and added `AdsrEnvelope::Stage::Retrigger` to fix sequencer click bugs.  CLAUDE.md and the commit both presented this as "the fix".  But `src/Voice.cpp` — which is the library's own VCO/VCF/VCA bundle and the natural building block for plugins — was never updated, so it still ran Env → Filter and still produced the click.  The audit must cross-check: "if the REPL has it, does Voice have it?"

12. **Test coverage gap detection** — flag primitives whose tests only check bounds / non-zero / silence-in-silence-out without verifying actual semantics (does the filter actually reduce HF? does the delay actually delay by N samples? does the pitch shifter actually shift pitch?).  These primitives are the next bugs waiting to happen.

## Per-agent briefing template

Use this as the prompt body for each per-group agent (substitute `<GROUP>` and the file list):

```
You are auditing DSP code in the ideath library (the current repository root)
for hidden correctness bugs.  Read but do not modify any files.

Files in your group: <list>

For each file, evaluate against ALL 12 categories in the audit skill at
.claude/skills/audit.md.  The category descriptions in that skill include
bug templates — treat them as patterns to search for.

Be conservative: only report a "Definite bug" when you can show evidence
concretely (file:line, traced logic, why the code is wrong).  If you can't
tell, put it in "Suspicious".

Report in this format:

## Definite bugs
- [path:line] short title
  Category: <number>
  Evidence: <concrete trace>
  Why wrong: <explanation>
  Suggested fix: <one line>

## Suspicious / probable bugs
- [path:line] short title
  Category: <number>
  What looks off: <description>
  How to verify: <step>

## Test coverage gaps
- [primitive] what semantics are NOT tested

## Reviewed and clean
- [primitive] one-line confirmation

Report under ~600 words.
```

For the cross-cutting agent, the briefing additionally asks it to:
- Run `git log --oneline --grep="fix:"` and read the top ~30 commits
- Read CLAUDE.md "Known issues" / "Backlog" sections
- For each fix that touches a "concept" (signal chain order, filter type, click suppression, denormal protection), list every file in the repo that implements that concept and check whether the fix is reflected
- Pay special attention to REPL ↔ Voice ↔ tests/test_SeqClick.cpp consistency

## After the agents return

1. **Verify each "Definite bug" yourself** before reporting to the user.  Sub-agents do make mistakes — in a previous run one agent confidently claimed `Compressor`'s attack/release coefficients were reversed, but the math was the standard `coeff = exp(-1/(τ·sr))` form.  Open the cited file, trace the logic, confirm.
2. Write up a single prioritised findings table.  Group by severity (Definite > Suspicious > Test gap > Clean).
3. For each Definite bug, propose a concrete one-line fix.
4. Ask the user which to fix.  Do not start fixing without confirmation unless you've been told to operate autonomously.
