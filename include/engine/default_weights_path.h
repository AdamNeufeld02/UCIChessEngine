#pragma once

namespace engine {

// Filename of a tuner-produced weights file (see `engine_tools tune`) to load
// automatically when the engine starts, on top of the hand-set defaults from
// init_eval_weights_default() - no UCI "setoption name EvalFile" needed.
//
// Resolved by searching for a "data" directory near the running executable
// (same convention as data/rook_magics.txt in bitboards.cpp), not relative to
// the current working directory - so this works the same way whether you
// launch the engine from a terminal, a GUI, or a script, regardless of where
// that happens to leave the working directory. Place the file at
// data/<this filename> anywhere above the executable in the directory tree
// (a build output folder still under the repo works fine).
//
// To make a tuned result the engine's default: copy that weights file to
// data/<name>, set this constant to <name>, and rebuild. An absolute path
// here is also honored as-is, bypassing the search.
//
// Empty string = keep using the hand-set defaults only.
constexpr const char* DEFAULT_WEIGHTS_FILE = "tuned_weights_first_draft.txt";

}
