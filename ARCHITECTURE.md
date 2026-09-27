# Longland Ghost Architecture

## Scope

This greenfield prototype implements the defining Phase 1-5 loop: bounded session capture, musical event segmentation, compact descriptors, explainable similarity search, safe recalled-audio rendering, nondestructive age-aware reconstruction, and hereditary session-memory evolution. Archive persistence remains intentionally absent.

## Ownership and threads

The audio callback owns `SessionAudioStore`, the monotonic write cursor, the recall command consumer, and the fixed `GhostVoicePool`. It performs no file I/O, catalog scans, FFT construction, locks, logs, or dynamic allocation. Input is copied into fixed `AnalysisAudioBlock` messages; if that bounded queue is full, analysis is dropped and dry audio continues.

`GhostAnalysisWorker` owns event detection, feature extraction, the live event catalog, similarity evaluation, ranking, cooldown policy, and production of fixed recall commands. It never reads the concurrently written session ring directly. Event audio for analysis arrives as private copied blocks.

The message thread reads atomic/fixed diagnostic snapshots only. It never traverses live DSP containers.

On shutdown the processor stops accepting analysis input, requests and joins the worker, then destroys queues, voices, and storage. A sample-rate change increments the storage epoch and clears event references and voices; old frame positions are never reinterpreted at a new rate.

## Session audio representation

The store is planar, fixed-capacity, and addressed by monotonic `uint64_t` absolute frame positions. Modulo arithmetic is private to the store. A `MemoryRef` contains an ID, epoch, absolute start, and length. It is valid only while the epoch matches and the complete range remains between the oldest retained frame and current write cursor.

Scheduling-time validity is not enough: the voice pool also checks an overwrite lease for the planned traversal and rechecks during playback. A source approaching overwrite enters a bounded release instead of reading reused ring slots. This is especially important for reverse and sub-unity playback rates.

The prototype implements a bounded maximum memory duration rather than silently preallocating five minutes at 96 kHz stereo per instance. Five minutes at that format is about 230 MB; multi-instance behaviour must be measured before raising the cap.

## Event detection

Detection is driven by short/slow RMS envelopes, energy-rise novelty, zero-crossing change, silence boundaries, and a sustained-signal fallback. Boundaries are expressed in absolute frames and do not depend on DAW buffer size. Events have minimum and maximum durations, preroll, and a silence hold. Silence does not create events; sustained material is eventually segmented even without a transient.

The realtime audio callback only queues copied blocks. Detector and descriptor work is worker-side.

## Descriptor schema

Schema 1 contains a 34-float comparison payload:

- 12 L1-normalised chroma bins
- 8 normalised log-frequency band energies
- centroid mean and deviation
- flatness mean and deviation
- positive spectral-flux mean and upper percentile
- RMS mean and dynamic range
- crest factor and harmonicity
- log duration
- stereo width and correlation
- fundamental-confidence value

Optional fundamental Hz, harmonic reliability, and stereo reliability are metadata. An unreliable group is omitted from aggregation rather than filled with a fabricated score.

## Similarity equation

Each descriptor pair produces explicit group scores: harmonic, spectral, transient, dynamics, stereo, and duration. Distribution features use bounded Hellinger/Bhattacharyya-style distances; scalar features use bounded normalised differences. The overall score is a reliability-weighted geometric mean:

```text
similarity = exp(sum(weight[g] * reliability[g] * log(max(score[g], epsilon)))
                 / sum(weight[g] * reliability[g]))
```

The automatable Memory Type choice selects one of three deterministic recognition profiles. Literal weights harmonic identity and duration most strongly; Emotional weights spectral character and dynamics; Dream heavily weights broad spectral overlap. All group scores and effective weights remain published for diagnostics, so a profile changes the explanation rather than hiding it.

Recognition controls the minimum legitimate similarity through a monotonic, profile-specific threshold curve. Literal preserves the established 0.72 default threshold, while Emotional and Dream progressively broaden it to 0.62 and 0.50 at the same control position. Recall controls willingness/frequency only after that gate. Age, imprint, diversity, and recall history may rerank eligible candidates using bounded factors. Randomness may eventually choose among qualified near-best candidates, but never creates eligibility. The core test build uses deterministic selection.

## Recall path

Recall commands contain a source reference, query/event IDs, total similarity and component contributions, direction/rate, gain, and expiry. The audio thread rejects stale epoch, invalid, expired, or lease-inadequate commands.

Up to eight fixed voices use wrap-safe interpolation and raised-cosine attack/release envelopes. Very short events cap fade lengths to avoid overlapping invalid envelopes. Stereo remains stereo; mono mapping is explicit. Voice exhaustion uses a bounded quietest/releasing policy. Output is finite-checked and conservatively soft-contained.

Presence is a smoothed additive wet contribution: the dry signal remains unity. Repeat Volume adds an independent smoothed 0-2x recall gain before the bounded output stage.

Feedback is smoothed and clamped below unity. The softly bounded recalled contribution is mixed into a preallocated capture buffer, and that exact buffer is sent both to analysis and the session store so descriptors and `MemoryRef`s always describe the same audio. Bypass advances voices but records dry only. Feedback can materialize the hereditary descendant metadata described below; canonical source audio is never mutated in place.

Each qualified command also carries normalized source age. `GhostDreamProcessor` is fixed-state and per voice: Dream independently controls deterministic dual-position diffusion, log-scaled spectral softening, and bounded stereo-width motion; Decay is multiplied by a nonlinear normalized-age curve before applying transient/high-frequency loss, position uncertainty, and stereo narrowing. At zero Dream and zero effective Decay the reconstruction path is sample-exact. Source memories are never rewritten. Parameter automation converges with a 30 ms one-pole ramp, and all reads remain clamped to the event's own frame range.

## Entropy and lineage

Every catalog event has a fixed-size `GhostLineage`: immutable root ID, immediate parent ID, generation, and a cumulative pitch/blur/spatial mutation recipe. Generation 0 events establish their own roots. A recall derives one deterministic potential child from the selected parent's complete recipe plus a small Entropy-scaled delta. Pitch is clamped to +/-5 semitones, blur and spatial state to bounded unit ranges, and every per-generation delta has a stricter step limit.

Pitch mutation changes the recall voice's bounded playback rate. Blur remains part of every smoothed Dream target, and spatial mutation biases stereo reconstruction without changing canonical memory audio. The voice pool publishes the dominant rendered child's provenance using fixed state. Only when its effective feedback contribution exceeds a floor is that provenance copied through the analysis FIFO; if the resulting audio forms an event, the worker materializes it with the supplied root, parent, generation, and inherited recipe. Dry events remain Generation 0, bypass cannot create children, and Feedback at zero cannot materialize descendants.

## Host state

The processor accepts matched mono or stereo effect layouts. Stable performance parameter IDs are `memory`, `recall`, `recognition`, `age`, `decay`, `dream`, `entropy`, `presence`, `repeatVolume`, `feedback`, and `memoryType`. Project state contains a schema and parameters only; session audio, volatile events, and lineages are not serialized. A fresh instance begins with fresh session memory.

Archive Recording and Cross-Session are not implemented and therefore cannot silently activate. Their eventual defaults remain off and their worker/filesystem path will be separate from this session engine.

## Future archive seam

Persistent work requires a separate archive worker, versioned transactional metadata, owned fragment audio, bounded cache, corruption isolation, explicit Archive Recording/Cross-Session gates, cancellation-safe offline ingest, and provenance. None of that should be placed on the audio thread or conflated with the live catalog.
