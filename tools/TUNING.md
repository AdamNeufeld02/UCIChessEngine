# Eval tuning pipeline — reference

Everything needed to run `engine_tools tune`, validate its output, adopt a
result as the engine's default, and extend the eval function with new terms
so the tuner picks them up too. Source of truth for the tool itself is
`tools/tune.cpp` — if this doc and the code ever disagree, the code wins;
update this file to match.

## Quick reference

```
# Shakeout - confirm the mechanics work, minutes not hours
engine_tools tune --limit 20000 --epochs 10 --check-interval 200

# Hyperparameter calibration on a subset - expect to run this a few times
engine_tools tune --limit 200000 --epochs 20 --lr 0.02
engine_tools tune --limit 200000 --epochs 20 --lr 0.1

# Full run, no --limit, generous budget so early stopping decides
engine_tools tune --epochs 300 --patience 20

# Resume/continue from an already-adopted default instead of hand-set weights
engine_tools tune --weights data/tuned_weights.txt --epochs 300 --patience 20
```

Run `engine_tools tune --help` for the full flag list with current defaults —
this doc explains *what they mean and why*, the `--help` output is the
authoritative list of *what exists*.

## What the pipeline actually does

1. **Load** `data/quiet-labeled.epd` (or whatever `--dataset` points at) — an
   EPD file, `<4-field position> c9 "<result>";` per line.
2. **Clean it**: every FEN is validated through the real `Board::fenToBoard`
   (parse failures are logged and skipped, never crash the run); positions
   are deduped by Zobrist key; positions where the side to move is in check
   are dropped (the "quiet" filter — this is intentionally minimal, see
   *Known limitations* below).
3. **Extract a trace** per surviving position — which `Weights` fields fired,
   and with what signed count — using the *real* eval code path (`accum()`
   hooks inside `evaluatePawnStructure`/`evaluateKingShelter`/
   `evaluatePieceActivity` in `eval.cpp`, plus hand-computed material/PSQT/
   tempo). This is what makes the tuner's model of the eval function
   impossible to silently drift from what the engine actually computes.
4. **Correctness gate**: the first 200 accepted positions are double-checked —
   reconstruct eval from the trace, compare against a direct `evaluate()`
   call, abort the whole run on any mismatch. This also now hard-fails if any
   traced `Score` field isn't registered in `enumerateTunableParams` (see
   *Adding a new eval term* — this is the safety net for a half-wired term).
5. **Split three ways**, not two: `--test` fraction first (**never looked at
   until the very end**), then `--holdout` (used for early-stopping decisions
   during training), then everything remaining becomes training data.
   Reporting holdout loss as the final answer would be mildly optimistic,
   since early stopping already used it to pick a checkpoint; the test
   number is the honest one, precisely because nothing during training ever
   sees it.
6. **Calibrate K** (the sigmoid scale, `eval → win probability`) via a coarse-
   to-fine grid search over the training set, unless `--k` overrides it.
7. **Train** with mini-batch Adam (not plain SGD — parameters span wildly
   different natural scales, from material weights in the hundreds to
   mobility entries in the tens, which a single global step size handles
   poorly). Every `--check-interval` batches, holdout loss is checked; an
   improvement checkpoints immediately and resets patience; `--patience`
   consecutive non-improvements stops the run early.
8. **Report**: a `weights_<timestamp>_final.txt` (the tuned weights, same
   `name value` text format `load_eval_weights_from_file` reads), a
   `diff_<timestamp>.txt` (every parameter that changed, sorted by
   `|Δmg|+|Δeg|` — the top 20 also print to the console), and the untouched
   test-set loss before/after.

## Interpreting the output

- **Train loss dropping, holdout flat or rising**: overfitting. Early
  stopping should catch this on its own; if it's not stopping soon enough,
  lower `--patience` or shrink `--check-interval` so it checks more often.
- **Both flat from the start**: `--lr` too low (or something's actually
  broken — cross-check against the `initial holdout loss` value staying
  suspiciously identical many checks later).
- **Loss oscillating or exploding**: `--lr` too high.
- **Early stopping never triggers, full `--epochs` budget used**: still
  improving when the run ended — raise `--epochs`, or the LR is too
  conservative.
- **Final test loss barely different from initial test loss**: this run
  isn't worth pursuing further validation (tactical suite, SPRT) — stop here,
  it's cheap to notice and expensive to chase further down the pipeline.
- **Diff report full of huge swings in rarely-seen parameters** (e.g. deep
  `safetyTable[]` entries, rank-7 `passedPawn`): expected — these are the
  parameters with the least data support. Not a bug, but a reason to treat
  the *specific* values with more skepticism than the well-supported ones
  (material, common PSQT squares) until per-parameter sample-count tracking
  and regularization exist (see *Known limitations*).

## Validating a result before trusting it

In order of increasing cost — stop as soon as one of these doesn't look
promising, no need to run the more expensive ones:

1. **Test-set loss**, free — already printed at the end of every run.
2. **Tactical suite regression** — load the candidate through
   `setoption name EvalFile value <path to weights_..._final.txt>`, run
   `tools/uci_eval`'s suites (`wac.epd`, `ecm98.epd`, `eigenmann_rapid.epd`,
   `iq2.epd`), compare pass rate to the current baseline.
3. **SPRT** via `testing/run_sprt.ps1`, `EvalFile` pointing "dev" at the
   candidate while "baseline" stays on whatever's currently adopted (hand-set
   or a previously-adopted tuned file). Use `normal` bounds (`elo0=0,
   elo1=5`) — unlike the pawn hash table or other pure refactors, a real
   gain is the actual goal here. Testing this way needs setting `EvalFile`
   on only the "dev" side, which `run_sprt.ps1` doesn't support yet (it
   applies UCI options uniformly to both engines via `-each`) — either
   extend it or invoke `fastchess` directly for this specific comparison.

## Adopting a validated result as the engine's default

`include/engine/default_weights_path.h` holds one constant,
`DEFAULT_WEIGHTS_FILE`, empty by default (hand-set weights only). To adopt:

1. Copy the validated `weights_..._final.txt` to `data/<some name>.txt`, e.g.
   `data/tuned_weights.txt`.
2. Set `DEFAULT_WEIGHTS_FILE = "tuned_weights.txt";` (bare filename).
3. Rebuild `uci_engine`. At startup it now loads that file automatically —
   resolved by searching for a `data/` directory near wherever the
   executable actually lives (`engine::findNearExecutable`, shared with the
   tuner's own dataset-path resolution and the original magic-bitboard-number
   loading), **not** relative to the current working directory. Confirmed
   working launched from a directory with no relationship to the repo at
   all. An absolute path in the constant is also honored as-is.
4. Commit both the new `data/*.txt` file and the changed constant. From here,
   `run_sprt.ps1`'s normal git-ref dev-vs-baseline comparison captures "does
   this tuned baseline beat the previous commit" the same way it captures
   any other code change — no `EvalFile` UCI juggling needed for *that*
   comparison, only for testing a not-yet-adopted candidate (previous
   section).

`DEFAULT_WEIGHTS_FILE` (what the engine loads to play) and `--weights` (what
the tuner starts optimizing from) are independent switches that often end up
pointing at the same file — nothing forces them to match. You can experiment
with tuning from an old checkpoint without touching what's actually deployed.

## Iterating: tuning again from the current default

Once something's been adopted, the next round should start from it, not
from `init_eval_weights_default()`'s original hand guesses:

```
engine_tools tune --weights data/tuned_weights.txt --epochs 300 --patience 20
```

`--weights` overlays that file's values on top of the hand-set defaults
before training starts — anything it doesn't mention keeps its hand-set
value rather than starting from zero/garbage, same partial-load-safe
behavior as `EvalFile`. This is also the point where reintroducing
regularization (anchored to *this* file, not the original guess) becomes
worth building — see *Known limitations*.

## Adding a new eval term

**Step 1 — decide where it lives.** Does it fit as a per-piece/per-pattern
bonus inside `evaluatePawnStructure`, `evaluateKingShelter`, or
`evaluatePieceActivity` (`src/engine/eval.cpp`)? Most standard terms do
("Case A" below). Something like `tempo` — a flat, whole-position constant
not tied to walking individual pieces — doesn't ("Case B").

**Step 2 — add the field.** In `Weights` (`include/engine/eval.h`), add e.g.
`Score outpost;` (or an array — see `passedPawn[8]` for rank-indexed,
`kingOpenFile[2][2]` for 2D).

**Step 3 — register the name.** In `enumerateTunableParams` (`eval.cpp`), add
`addScore("outpost", w.outpost);` alongside the existing calls. This is what
gives it a `.mg`/`.eg` entry in the weights file format and makes it visible
to the tuner. **Forgetting this step is now a hard error** — the tuner
aborts with a clear message rather than silently training without the term —
but only once a position actually exercises it, which may not be during the
first 200 gate positions if the term is rare.

**Step 4 — give it a default.** In `init_eval_weights_default()` (`eval.cpp`),
add `W.outpost = Score{15, 10};` (or your best guess) alongside the other
hand-set values.

**Step 5A — Case A** (fits an existing traced function): write the scoring
logic using `accum()` instead of `score +=`:
```cpp
if (isOutpost) accum(score, W.outpost, 1);
```
**Zero changes to `tune.cpp` needed.** `collectColourTrace` already runs all
three traced functions with tracing on, so any `accum()` call inside them is
automatically captured, named, and tunable — the same way `isolatedPawn` or
`knightMobility` already work.

**Step 5B — Case B** (doesn't fit those functions): touch both files — add
the real term wherever it belongs in `evaluate()` (like `tempo` is added
directly in the final mg/eg blend), *and* hand-add a matching line in
`tune.cpp`'s `collectTrace` (mirroring `agg[&W.tempo] += 1;`), since there's
no `accum()` call for the tracer to intercept.

**Watch for the "index selector" trap.** If a new term is used to *select*
something (an index into another table) rather than *contribute* to score
directly — the same role `attackerWeight` plays for `safetyTable` — it
cannot be gradient-tuned by this scheme. Only call `accum()` on the table
being indexed *into*, never on whatever computes the index. See
`evaluatePieceActivity`'s comment for the worked example.

**Step 6 — sanity check.** Run the tuner on a small `--limit`. The
correctness gate will catch a wiring mistake immediately rather than
silently training on incomplete/wrong data.

## Known limitations (not bugs, just not built yet)

- **`attackerWeight`** (8 small ints feeding `evaluatePieceActivity`'s
  `safetyTable` index) is never gradient-tuned — see the "index selector"
  note above. Would need a separate small coordinate-descent side-loop
  (nudge each by ±1, keep if population loss improves) if you want it tuned.
- **No regularization yet.** Round 1 has no prior run to anchor to; once
  something's been adopted (see *Iterating* above), anchoring subsequent
  runs to that file — not the original hand guess — becomes worthwhile.
- **No per-parameter sample-count tracking.** Would pair naturally with
  regularization, and is independently useful as a "trust this value less"
  diagnostic even without it.
- **Quiet filter is minimal** — just "not in check," trusting the dataset's
  own quiescence claim rather than independently verifying it (e.g. via a
  qsearch pass confirming no capture improves on standing pat).
- **Single-source dataset.** No blending with self-play games from
  `testing/results/*.pgn` yet — a documented fallback if the public dataset
  ever looks insufficient.
- **`run_sprt.ps1` can't set a UCI option on only one engine** — blocks
  fully-automated SPRT of a not-yet-adopted `EvalFile` candidate against the
  current default; the git-ref-based comparison for *already-adopted*
  results doesn't need this.
- **Single-threaded.** Fine at current dataset sizes; if a full run ever
  becomes uncomfortably slow, the sparse per-batch gradient accumulation is
  the obvious place to parallelize.

## Where things live

| Thing | Path |
|---|---|
| Tuner source | `tools/tune.cpp` |
| Dataset | `data/quiet-labeled.epd` (or `--dataset`) |
| Checkpoints + diff reports | `tools/tune_output/` (or `--outdir`) |
| Engine's default-weights switch | `include/engine/default_weights_path.h` |
| Weights file format read/write | `load_eval_weights_from_file` / `save_eval_weights_to_file`, `src/engine/eval.cpp` |
| Named parameter enumeration | `enumerateTunableParams`, `src/engine/eval.cpp` |
| Trace hook | `accum()`, `src/engine/eval.cpp` |
| Path resolution (shared) | `include/engine/exe_path.h` |
| Tactical suites | `tools/uci_eval/suites/*.epd` |
| SPRT harness | `testing/run_sprt.ps1`, `testing/README.md` |
