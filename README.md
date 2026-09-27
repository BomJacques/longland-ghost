# Longland Ghost

Longland Ghost is an experimental JUCE audio effect whose core recall decisions are driven by measured similarity between the present and previously captured musical events.

This repository is a greenfield Phase 1-5 engineering prototype. It targets Windows VST3 and intentionally prioritises session memory, recognition, click-safe recall, controlled reconstruction, and hereditary evolution over archive persistence or decorative UI.

## What is implemented

- bounded, preallocated stereo session audio ring with monotonic frame references and epoch invalidation
- worker-thread event detection and descriptor analysis fed by a bounded SPSC copy queue
- versioned 34-value descriptor with explainable harmonic, spectral, transient, dynamics, stereo, and duration scores
- deterministic similarity ranking; recognition gates eligibility before any recall-willingness decision
- Literal, Emotional, and Dream recognition profiles with explainable category weights and profile-specific thresholds
- fixed eight-voice recall pool with wrap-safe interpolation, raised-cosine fades, source lease checks, and finite-value guards
- nondestructive Dream/Decay reconstruction: Dream adds deterministic position diffusion, spectral softening, and stereo motion; Decay scales information loss by the selected memory's measured age
- hereditary Entropy lineages with root/parent/generation metadata, deterministic bounded pitch/blur/spatial mutation, and feedback-only descendant materialization
- smoothed Repeat Volume and bounded Feedback recapture; feedback enters the same audio/analysis memory path and is suppressed while bypassed
- JUCE VST3 wrapper with stable automatable IDs, parameter smoothing, state persistence, and a deliberately basic editor
- deterministic console tests for silence, objective similarity, buffer wrapping, stale references, interpolation, voice limits, literal-path preservation, age-linked degradation, Dream abstraction, parameter smoothing, inherited mutation, 128-generation containment, and feedback descendant materialization

Persistent Archive, Archive Ingest, Deja Vu prediction, and the full Memory Field remain outside this proof. Entropy lineage currently lives only in volatile Session memory; archive persistence for memory families remains future work. See `ROADMAP.md` for the committed scope/privacy design.

## Configure and build

JUCE is pinned by CMake FetchContent. A first configure therefore needs network access.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target GhostTests LonglandGhost_VST3
ctest --test-dir build --output-on-failure
```

On Windows, run these commands from a Visual Studio developer shell. See `ARCHITECTURE.md` for the realtime contract and `GAUNTLET_RESULTS.md` for evidence and known gaps.

## Licensing note

The prototype consumes JUCE under the terms selected by the product owner. Before commercial distribution, choose and document an appropriate JUCE licence and complete signing/installer/host-validation work.
