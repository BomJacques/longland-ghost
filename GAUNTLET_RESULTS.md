# Gauntlet Results

This file records executed evidence only. Planned tests are not marked as passing.

## Environment

- Greenfield workspace; no repository or reusable implementation was supplied.
- Windows, Visual Studio C++ toolchain and bundled CMake/Ninja available.
- JUCE dependency pinned by the project build.

## Executed tests

Release build completed with the Visual Studio C++ toolchain, Ninja, CMake, and pinned JUCE 9.0.0. Final Phase 5 CTest result: 6/6 passed in 2.57 seconds.

The full pipeline test proves that a detected repeated event generates a similarity-qualified, normalized-age-tagged command referencing real historical audio, which the recall voice renders audibly. The current suite also verifies profile weighting/threshold relationships, Repeat Volume mute/boost behavior, bounded feedback recapture, bypass suppression, sample-exact literal reconstruction, Dream divergence, age-linked Decay, hereditary Entropy mutation, audible multi-generation divergence, and feedback child materialization.

## Required proof matrix

| Claim | Objective gate | Status |
|---|---|---|
| Silence is ignored | 180 seconds of synthetic silence produces zero events | passed |
| Recognition is real | identical and related harmony score above unrelated/noise; full repeated-event pipeline recalls source 1 | passed |
| Buffer references are safe | wrap, stale-epoch, incomplete-reference, interpolation-edge, and overwrite-lease tests | passed |
| Recall edges are click-safe | natural and forced-release endpoints plus deferred voice stealing | core tests passed; listening test pending |
| Dry and bypass safety | Presence with no voices and bypass remain sample-exact dry; bypass advances internal state | passed |
| Noise restraint | nine seconds of stationary broadband noise produces events but no recall command | passed |
| Memory Types are real | Literal favors pitch identity; Emotional/Dream increase broad spectral/dynamic weighting with monotonic thresholds | passed |
| Repeat Volume | 0x mutes recall and 2x materially boosts it through the smoothed bounded output path | passed |
| Feedback containment | hostile values clamp to 0.85; recalled audio is recaptured with matching frame references and remains bounded | passed |
| Dream reconstruction | high Dream materially diverges from literal playback while remaining finite and bounded | passed |
| Age-linked Decay | zero-age material remains exact; old material loses transient/high-frequency detail at high Decay | passed |
| Reconstruction smoothing | Dream/Decay state converges sample-smoothly without a first-sample parameter jump | passed |
| Entropy inheritance | child root/parent/generation and mutation derive deterministically from the complete parent state | passed |
| Lineage containment | 128 hostile generations remain within pitch/blur/spatial and playback-rate bounds | passed |
| Audible evolution | a deterministic twelve-generation descendant materially diverges from Generation 0 | passed |
| Descendant materialization | feedback provenance survives render/capture/analysis and becomes event lineage metadata | passed |
| Host wrapper builds | Windows VST3 Release target | passed |

## Known scope gaps

- No persistent Ghost Archive, Cross-Session recall, Archive Ingest, Stranger, or archive corruption/migration path.
- No persistent Entropy lineage across DAW sessions; current memory families are deliberately volatile Session data.
- No full Memory Field; the editor is a functional generic parameter editor only.
- No pluginval, DAW torture, sanitizer, multi-hour soak, multi-instance RAM, or subjective listening session has been run yet.
- No claim of commercial release readiness is made by this Phase 1-5 prototype.
