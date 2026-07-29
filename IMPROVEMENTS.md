# Engine Improvement TODO

Findings from a review of `search.cpp`, `eval.cpp`, `moveselector.cpp`, `transpostable.cpp`, `threads.cpp`, `engine.cpp`, and `uci.cpp` (2026-07-28).

## Bugs / correctness issues

- [ ] **Time management ignores `movestogo`.** `Engine::calculateLimits` ([engine.cpp:46](src/engine/engine.cpp#L46)) always does `T_move = myTime/20 + myInc/2`; `limits.movestogo` (parsed in [uci.cpp:136](src/engine/uci.cpp#L136)) is never read. Wrong allocation under "N moves in M minutes" controls.
- [ ] **No move-overhead buffer** before the hard time limit — risk of flagging under GUI/OS latency.
- [ ] **`Threads` not exposed as a UCI option** ([uci.cpp:207](src/engine/uci.cpp#L207) commented out) and hardcoded to `setThreads(16)` ([uci.cpp:29](src/engine/uci.cpp#L29)) regardless of actual hardware.
- [ ] **No check extensions.** `givesCheck` only shrinks the LMR reduction ([search.cpp:359](src/engine/search.cpp#L359)); depth is never extended when in check.
- [ ] **No mate-distance pruning.**
- [ ] **Root node never probes/writes the TT** (`rootSearch` has no `tt.probe`/`ttWriter.write` calls at all) — no sharing with helper threads, no persistence between iterations.
- [ ] **Dead field**: `SearchStack::didNull` ([search.h:34](include/engine/search.h#L34)) is declared but never read or written — looks like an abandoned double-null-move guard.

## Missing standard techniques

- [ ] **Pawn hash table.** `evaluatePawnStructure`/`evaluateKingShelter` ([eval.cpp:426](src/engine/eval.cpp#L426)) recompute from scratch every node with no caching.
- [ ] **Late-move / move-count pruning** for quiets beyond the narrow futility-pruning case.
- [ ] **Internal iterative reduction** when a node has no TT move.
- [ ] **Singular extensions.**
- [ ] **Automated eval tuning** (Texel/SPSA) — `load_eval_weights_from_file` ([eval.cpp:421](src/engine/eval.cpp#L421)) is a stub that always returns `false`; all ~15 term groups are still hand-set Pesto-derived guesses, despite `tools/uci_eval` already existing for suite scoring.
- [ ] **Syzygy tablebase support.**
- [ ] **NNUE-style eval** to replace/augment the hand-crafted PSQT eval — biggest expected Elo/effort payoff long-term; requires an incremental accumulator in make/undo, self-play data generation, and a small trainer.
- [ ] **Local SPRT self-play harness** (cutechess-cli / fastchess) — nothing in-repo currently validates that a change gains Elo rather than "feels right"; important to have *before* landing the pruning/extension changes above, since margins there can just as easily lose Elo if mistuned.

## Suggested order

1. Quick wins: check extensions, mate-distance pruning, honor `movestogo` + move overhead, expose `Threads` UCI option (default `hardware_concurrency()`), pawn hash table, root TT usage.
2. Medium: late-move pruning, internal iterative reduction, wire a Texel/SPSA tuner against existing `tools/uci_eval` suites.
3. Bigger swings: singular extensions, NNUE eval, Syzygy tablebases, SPRT self-play harness.
