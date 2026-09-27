# Longland Ghost Roadmap

This document fixes the intended semantics for persistent scope and Deja Vu. Session Entropy is now implemented; persistent memory-family storage remains future work.

## Memory scope

- **SESSION** searches only volatile memories captured by this plugin instance since construction or reset. It never reads disk and remains the default.
- **PROJECT** searches SESSION plus stored memories bound to an explicit `projectNamespaceUuid`. A VST3 cannot portably discover a DAW project identity, so users create or join a Project Memory namespace. An unbound PROJECT selection falls back visibly to SESSION.
- **ARCHIVE** searches SESSION plus all eligible persistent namespaces.

`CROSS-SESSION` is the independent read gate for any older runtime session. With it off, PROJECT and ARCHIVE collapse to SESSION.

`ARCHIVE RECORDING` is write consent only. It defaults off, is not host-automatable, and must be re-armed at runtime rather than silently restored on. It gates automatic archival and `REMEMBER`; it does not control reading existing memories.

`DISTANCE` biases ranking after similarity eligibility. It never makes an unrelated foreign memory eligible. Selection order is:

1. Scope, Cross-Session, Stranger, schema, availability, and corruption filters.
2. Literal/Emotional/Dream similarity.
3. Recognition threshold.
4. `similarity x imprint x age x scopeWeight x diversity` ranking.
5. Recall willingness and cooldown.

`STRANGER` excludes current-session candidates without falling back. `REMEMBER` promotes the most recently completed event, or latches the next event, only while Archive Recording is armed.

## Persistent repository

The planned local archive uses a versioned SQLite/WAL metadata index and immutable content-addressed FLAC fragments under `%LOCALAPPDATA%\Longland Audio\Ghost\Archive\v1`. It stores project/session UUIDs, descriptors, provenance, imprint, source blob, lineage root/parent/generation, and mutation recipe.

Writes occur on an archive worker through staging-file, flush, atomic-rename, then metadata-transaction steps. The audio callback never opens files, takes archive locks, encodes audio, or scans the persistent catalog. Persistent playback uses a fixed decoded-audio cache with pinned generation-checked handles; a missed prefetch deadline skips recall rather than blocking or playing late.

Deletion removes metadata references transactionally, then garbage-collects unreferenced blobs. With Archive Recording off, the archive opens read-only and does not create, checkpoint, touch, or update storage.

## Entropy

Canonical memories remain immutable. The Phase 5 session engine now gives a materialized child:

- lineage root and parent IDs
- generation number
- deterministic bounded mutation recipe
- inherited parent mutation plus a small Entropy-scaled delta

Feedback determines whether recalled material becomes a catalogued child; Entropy controls mutation magnitude. Potential child state exists while a recall voice renders, but the session catalog increments a lineage only when feedback-bearing audio forms a detected event. Pitch-rate, blur, and spatial mutation are real-time-safe and the feedback capture is reanalysed by the existing worker. Canonical parents remain eligible so a lineage cannot destroy its source. Persisting these families requires the future Archive schema.

## Deja Vu

The analysis worker will cluster completed descriptors into recurring event tokens and learn a bounded variable-order transition/timing model. After patterns such as `A -> B -> C` recur, it can predict the next token and expected onset.

Predictions use the same scope, privacy, and Recognition gates as reactive recall. A fixed audio-thread scheduler receives a `startAtStreamFrame`; persistent audio must be prefetched and pinned before that deadline. Predictions are cancelled on seek, stop, epoch reset, discontinuity, or material tempo change. Late predictions are skipped. The feature adds no lookahead latency: it predicts from prior recurrence rather than seeing future input.

## Implementation order

1. Pure scope/eligibility policy and explicit project-namespace state.
2. Transactional local archive plus corruption, consent, cancellation, and multi-instance tests.
3. Persistent descriptor snapshots and bounded decoded-audio cache.
4. Scope/provenance diagnostics, Remember, Stranger, and Archive Manager.
5. Persist existing Entropy roots, parents, generations, and recipes in the Archive.
6. Deja Vu transition model and future-start scheduler.
