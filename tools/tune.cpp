// Texel-style tuner for engine::Weights.
//
// Pipeline: load an EPD dataset (quiet-labeled.epd format: 4-field position +
// `c9 "<result>";`), validate/dedupe/filter it, extract a sparse feature
// trace per position via the SAME code path evaluate() uses (see accum() in
// eval.cpp - this is what guarantees the trace can't silently drift from what
// the engine actually computes), verify that guarantee numerically against a
// sample of positions, calibrate the sigmoid scale K, then run mini-batch SGD
// with early stopping against a held-out split, checkpointing the weights
// file whenever holdout loss improves.
#include "engine/board.h"
#include "engine/eval.h"
#include "engine/bitboards.h"
#include "engine/hashing.h"
#include "engine/exe_path.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tune_cli {

using namespace engine;

// ---------------------------------------------------------------------------
// CLI options
// ---------------------------------------------------------------------------

struct Options {
    // Bare filename by default - resolved by searching for a "data" directory
    // near the running executable (see engine::findNearExecutable) unless
    // --dataset is passed explicitly, since CWD at launch isn't reliable
    // (confirmed: running the built exe directly from its own folder has no
    // "data" subdirectory of its own).
    std::string dataset = "quiet-labeled.epd";
    // Resolved relative to the repo root (see engine::findRepoRoot) unless
    // --outdir is passed explicitly - same CWD-reliability reasoning as
    // dataset above (confirmed: running from build/release/Release put
    // checkpoints in build/release/Release/tools/tune_output instead of the
    // repo's actual tools/tune_output).
    std::string outDir = "tools/tune_output";
    std::string initWeights; // empty = start from init_eval_weights_default()
    double holdoutFrac = 0.2; // used for early-stopping decisions during training
    double testFrac = 0.1;    // untouched until the very end - the honest final number
    double lr = 0.05; // Adam step size - not comparable to a plain-SGD lr
    int batchSize = 256;
    int maxEpochs = 200;
    int patience = 15;
    int checkIntervalBatches = 2000;
    uint64_t seed = 1;
    long long limit = -1;    // -1 = no limit, else cap dataset size (for a fast smoke test)
    double kOverride = -1.0; // <0 = auto-calibrate
};

static bool parseArgs(int argc, char** argv, Options& opt, bool& datasetExplicit, bool& outDirExplicit) {
    datasetExplicit = false;
    outDirExplicit = false;
    for (int i = 0; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* flag) -> std::optional<std::string> {
            if (a == flag && i + 1 < argc) return std::string(argv[++i]);
            return std::nullopt;
        };
        if (auto v = next("--dataset")) { opt.dataset = *v; datasetExplicit = true; }
        else if (auto v = next("--outdir")) { opt.outDir = *v; outDirExplicit = true; }
        else if (auto v = next("--weights")) opt.initWeights = *v;
        else if (auto v = next("--holdout")) opt.holdoutFrac = std::stod(*v);
        else if (auto v = next("--test")) opt.testFrac = std::stod(*v);
        else if (auto v = next("--lr")) opt.lr = std::stod(*v);
        else if (auto v = next("--batch")) opt.batchSize = std::stoi(*v);
        else if (auto v = next("--epochs")) opt.maxEpochs = std::stoi(*v);
        else if (auto v = next("--patience")) opt.patience = std::stoi(*v);
        else if (auto v = next("--check-interval")) opt.checkIntervalBatches = std::stoi(*v);
        else if (auto v = next("--seed")) opt.seed = std::stoull(*v);
        else if (auto v = next("--limit")) opt.limit = std::stoll(*v);
        else if (auto v = next("--k")) opt.kOverride = std::stod(*v);
        else if (a == "--help" || a == "-h") return false;
    }
    return true;
}

static void printUsage() {
    std::cout <<
        "engine_tools tune [options]\n"
        "  --dataset PATH       EPD dataset (default: quiet-labeled.epd, found by searching\n"
        "                       for a 'data' directory near this executable; passing this\n"
        "                       explicitly is used as a literal path relative to CWD instead)\n"
        "  --outdir PATH        checkpoint output directory (default: tools/tune_output,\n"
        "                       resolved relative to the repo root; passing this explicitly\n"
        "                       is used as a literal path relative to CWD instead)\n"
        "  --weights PATH       start from this weights file instead of hand-set defaults\n"
        "                       (e.g. a previous run's *_final.txt, to keep tuning further)\n"
        "  --holdout FRAC       fraction used for early-stopping decisions during training (default: 0.2)\n"
        "  --test FRAC          fraction held out untouched until the very end, for the one\n"
        "                       honest final loss number (default: 0.1) - never used for any\n"
        "                       decision during training, unlike --holdout\n"
        "  --lr RATE            Adam step size (default: 0.05) - per-parameter adaptive,\n"
        "                       not directly comparable to a plain-SGD learning rate\n"
        "  --batch N            mini-batch size (default: 256)\n"
        "  --epochs N           max epochs (default: 200)\n"
        "  --patience N         early-stop patience, in holdout checks (default: 15)\n"
        "  --check-interval N   holdout-check every N batches (default: 2000)\n"
        "  --seed N             RNG seed for shuffling/split (default: 1)\n"
        "  --limit N            cap dataset to first N accepted positions (smoke test)\n"
        "  --k VALUE            skip K auto-calibration, use this K directly\n";
}

// ---------------------------------------------------------------------------
// EPD parsing
// ---------------------------------------------------------------------------

struct ParsedEpdLine {
    std::string fen;   // full 6-field FEN, ready for Board::fenToBoard
    double resultStm;  // game result in [0,1] oriented to the side to move
    bool ok = false;
};

static ParsedEpdLine parseEpdLine(const std::string& line) {
    ParsedEpdLine out;

    std::istringstream iss(line);
    std::string placement, side, castling, ep;
    if (!(iss >> placement >> side >> castling >> ep)) return out;
    if (side != "w" && side != "b") return out;

    size_t q1 = line.find('"');
    if (q1 == std::string::npos) return out;
    size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos) return out;
    std::string res = line.substr(q1 + 1, q2 - q1 - 1);

    double resultWhite;
    if (res == "1-0") resultWhite = 1.0;
    else if (res == "0-1") resultWhite = 0.0;
    else if (res == "1/2-1/2") resultWhite = 0.5;
    else return out;

    out.fen = placement + " " + side + " " + castling + " " + ep + " 0 1";
    out.resultStm = (side == "w") ? resultWhite : (1.0 - resultWhite);
    out.ok = true;
    return out;
}

// ---------------------------------------------------------------------------
// Tunable score parameter list, derived from enumerateTunableParams() so
// there's exactly one place (eval.cpp) that knows the field layout.
// attackerWeight entries (plain ints, not part of a .mg/.eg pair) are
// naturally skipped here - see the comment in evaluatePieceActivity for why
// they can't be gradient-tuned.
// ---------------------------------------------------------------------------

struct TunableScore {
    std::string name;
    int* mg;
    int* eg;
};

static std::vector<TunableScore> enumerateTunableScores(Weights& w) {
    auto params = enumerateTunableParams(w);
    std::vector<TunableScore> scores;
    scores.reserve(params.size() / 2);

    for (size_t i = 0; i + 1 < params.size(); ) {
        const std::string& n1 = params[i].name;
        if (n1.size() > 3 && n1.compare(n1.size() - 3, 3, ".mg") == 0) {
            std::string base = n1.substr(0, n1.size() - 3);
            if (params[i + 1].name == base + ".eg") {
                scores.push_back({base, params[i].value, params[i + 1].value});
                i += 2;
                continue;
            }
        }
        i += 1; // lone int (attackerWeight) - not traced/tuned
    }
    return scores;
}

// ---------------------------------------------------------------------------
// Trace collection - drives the real eval terms (via accum()'s trace hook)
// plus hand-computed material/PSQT/tempo (not covered by accum(), since
// board.material[]/psqtv[] are pre-accumulated incrementally elsewhere, not
// computed fresh inside evaluate()).
// ---------------------------------------------------------------------------

static void collectColourTrace(Board& board, Colour col, int sign, std::unordered_map<Score*, int>& agg) {
    std::vector<TraceEntry> local;
    g_traceSink = &local;
    if (col == WHITE) {
        evaluatePawnStructure<WHITE>(board);
        evaluateKingShelter<WHITE>(board);
        evaluatePieceActivity<WHITE>(board);
    } else {
        evaluatePawnStructure<BLACK>(board);
        evaluateKingShelter<BLACK>(board);
        evaluatePieceActivity<BLACK>(board);
    }
    g_traceSink = nullptr;

    for (TraceEntry& e : local) {
        agg[e.param] += sign * e.count;
    }
}

static void collectMaterialPsqtTrace(Board& board, Colour col, int sign, std::unordered_map<Score*, int>& agg) {
    for (int pt = PAWN; pt <= KING; ++pt) {
        int cnt = popcount(board.pieces(col, static_cast<PieceType>(pt)));
        if (cnt) agg[&W.material[pt]] += sign * cnt;
    }

    Bitboard occ = board.pieces(col);
    while (occ) {
        Square sq = pop_lsb(occ);
        PieceType pt = typeOf(board.pieceOn(sq));
        Square mirrored = mirrorSquareIfBlack(sq, col);
        agg[&W.psqt[pt][mirrored]] += sign;
    }
}

// Full trace for one position, oriented to the side to move (matches how
// evaluate() computes X<stm>() - X<~stm>()). Returns false (and an empty
// trace) if the position is skipped by the quiet sanity filter.
static bool collectTrace(Board& board, std::vector<std::pair<Score*, int>>& outTrace, int& mgPhase, int& egPhase) {
    if (board.checkers()) return false; // quiet-position sanity filter

    int gamePhase = 0;
    gamePhase += popcount(board.pieces(WHITE, KNIGHT) | board.pieces(BLACK, KNIGHT)) * gamePhaseWeightings[KNIGHT];
    gamePhase += popcount(board.pieces(WHITE, BISHOP) | board.pieces(BLACK, BISHOP)) * gamePhaseWeightings[BISHOP];
    gamePhase += popcount(board.pieces(WHITE, ROOK) | board.pieces(BLACK, ROOK)) * gamePhaseWeightings[ROOK];
    gamePhase += popcount(board.pieces(WHITE, QUEEN) | board.pieces(BLACK, QUEEN)) * gamePhaseWeightings[QUEEN];
    mgPhase = gamePhase > 24 ? 24 : gamePhase;
    egPhase = 24 - mgPhase;

    Colour stm = board.sideToMove();

    std::unordered_map<Score*, int> agg;
    collectMaterialPsqtTrace(board, stm, +1, agg);
    collectMaterialPsqtTrace(board, ~stm, -1, agg);
    collectColourTrace(board, stm, +1, agg);
    collectColourTrace(board, ~stm, -1, agg);
    agg[&W.tempo] += 1; // side to move always gets the tempo bonus

    outTrace.clear();
    outTrace.reserve(agg.size());
    for (auto& [param, count] : agg) {
        if (count != 0) outTrace.push_back({param, count});
    }
    return true;
}

// Integer reconstruction, matching evaluate()'s exact arithmetic (including
// truncating integer division), for the correctness gate only.
static Value reconstructEvalExact(const std::vector<std::pair<Score*, int>>& trace, int mgPhase, int egPhase) {
    long long mgSum = 0, egSum = 0;
    for (auto& [param, count] : trace) {
        mgSum += static_cast<long long>(param->mg) * count;
        egSum += static_cast<long long>(param->eg) * count;
    }
    return static_cast<Value>((mgSum * mgPhase + egSum * egPhase) / 24);
}

// ---------------------------------------------------------------------------
// Dataset: compact CSR-style storage (flattened, not one vector per position)
// so 1.4M positions doesn't mean 1.4M small heap allocations.
// ---------------------------------------------------------------------------

struct Dataset {
    std::vector<int32_t> flatIdx;
    std::vector<int16_t> flatCount;
    std::vector<uint32_t> offsets; // size() == numPositions + 1
    std::vector<uint8_t> mgPhase;
    std::vector<uint8_t> egPhase;
    std::vector<float> result;

    size_t size() const { return result.size(); }

    void addPosition(const std::vector<std::pair<int, int>>& sparse, int mg, int eg, double res) {
        offsets.push_back(static_cast<uint32_t>(flatIdx.size()));
        for (auto& [idx, count] : sparse) {
            flatIdx.push_back(idx);
            flatCount.push_back(static_cast<int16_t>(count));
        }
        mgPhase.push_back(static_cast<uint8_t>(mg));
        egPhase.push_back(static_cast<uint8_t>(eg));
        result.push_back(static_cast<float>(res));
    }
    void finalize() { offsets.push_back(static_cast<uint32_t>(flatIdx.size())); }
};

static double evalFromWeights(const Dataset& ds, size_t pos, const std::vector<double>& mg, const std::vector<double>& eg) {
    double mgSum = 0.0, egSum = 0.0;
    for (uint32_t i = ds.offsets[pos]; i < ds.offsets[pos + 1]; ++i) {
        int idx = ds.flatIdx[i];
        int cnt = ds.flatCount[i];
        mgSum += mg[idx] * cnt;
        egSum += eg[idx] * cnt;
    }
    return (mgSum * ds.mgPhase[pos] + egSum * ds.egPhase[pos]) / 24.0;
}

static inline double sigmoid(double evalCp, double K) {
    return 1.0 / (1.0 + std::pow(10.0, -K * evalCp / 400.0));
}

static double meanLoss(const Dataset& ds, const std::vector<size_t>& indices, const std::vector<double>& mg,
                        const std::vector<double>& eg, double K) {
    double total = 0.0;
    for (size_t pos : indices) {
        double pred = sigmoid(evalFromWeights(ds, pos, mg, eg), K);
        double diff = ds.result[pos] - pred;
        total += diff * diff;
    }
    return indices.empty() ? 0.0 : total / static_cast<double>(indices.size());
}

// ---------------------------------------------------------------------------
// Loading + cleaning
// ---------------------------------------------------------------------------

static bool loadDataset(const Options& opt, Dataset& ds, size_t& linesRead, size_t& parseFailures,
                         size_t& duplicatesSkipped, size_t& notQuietSkipped) {
    std::ifstream in(opt.dataset);
    if (!in) {
        std::cerr << "tune: could not open dataset '" << opt.dataset << "'\n";
        return false;
    }

    std::vector<PawnEntry> dummyPawnTable(PAWN_TABLE_SIZE);

    // Score* -> flat index, matching enumerateTunableScores()'s order exactly
    // (train() builds its mg/eg arrays from the same call, so the indices
    // agree). Score's first member is `mg`, so &Score == &Score.mg for any
    // standard-layout Score - reinterpreting the stored `int* mg` recovers
    // the real Score* that accum()'s trace entries and collectMaterialPsqtTrace
    // both hand out.
    std::vector<TunableScore> scores = enumerateTunableScores(W);
    std::unordered_map<Score*, int> scoreIndex;
    scoreIndex.reserve(scores.size() * 2);
    for (size_t i = 0; i < scores.size(); ++i) {
        scoreIndex[reinterpret_cast<Score*>(scores[i].mg)] = static_cast<int>(i);
    }

    std::unordered_set<uint64_t> seenKeys;
    std::vector<std::pair<Score*, int>> rawTrace;
    std::vector<std::pair<int, int>> sparse;

    bool gateChecked = false;
    int gateSamplesLeft = 200;

    std::string line;
    while (std::getline(in, line)) {
        ++linesRead;
        if (opt.limit >= 0 && static_cast<long long>(ds.size()) >= opt.limit) break;

        ParsedEpdLine parsed = parseEpdLine(line);
        if (!parsed.ok) { ++parseFailures; continue; }

        Board board;
        State root{};
        try {
            board.fenToBoard(parsed.fen, &root);
        } catch (...) {
            ++parseFailures;
            continue;
        }

        if (!seenKeys.insert(board.key()).second) { ++duplicatesSkipped; continue; }

        int mgPhase, egPhase;
        if (!collectTrace(board, rawTrace, mgPhase, egPhase)) { ++notQuietSkipped; continue; }

        sparse.clear();
        sparse.reserve(rawTrace.size());
        for (auto& [param, count] : rawTrace) {
            auto it = scoreIndex.find(param);
            if (it == scoreIndex.end()) {
                // A Score field fired in real eval logic (accum() recorded
                // it) but isn't registered in enumerateTunableParams(). Left
                // unchecked this silently drops the term from training with
                // no error - fail loudly instead, since the correctness gate
                // above can't catch this (it reconstructs from the raw trace
                // directly, bypassing scoreIndex, so it still matches
                // evaluate() even when a term is unregistered).
                std::cerr << "tune: a Score field fired during eval but is not registered in "
                          << "enumerateTunableParams() - if you added a new eval term, add its "
                          << "addScore(...) call there too. Aborting rather than silently training "
                          << "without it.\n";
                return false;
            }
            sparse.push_back({it->second, count});
        }

        // Correctness gate: for a sample of early positions, confirm the
        // trace reconstructs exactly what evaluate() itself computes.
        if (gateSamplesLeft > 0) {
            --gateSamplesLeft;
            Value fromTrace = reconstructEvalExact(rawTrace, mgPhase, egPhase);
            Value fromEval = evaluate(board, dummyPawnTable.data());
            if (fromTrace != fromEval) {
                std::cerr << "tune: CORRECTNESS GATE FAILED on '" << parsed.fen << "'\n"
                          << "  trace reconstruction = " << fromTrace << ", evaluate() = " << fromEval << "\n"
                          << "  the trace has drifted from real eval logic - aborting before training on bad data.\n";
                return false;
            }
            if (gateSamplesLeft == 0) gateChecked = true;
        }

        ds.addPosition(sparse, mgPhase, egPhase, parsed.resultStm);
    }
    ds.finalize();

    if (!gateChecked && ds.size() > 0) {
        std::cerr << "tune: warning - fewer than 200 positions available, correctness gate ran on only "
                  << (200 - gateSamplesLeft) << " of them\n";
    }

    return true;
}

// ---------------------------------------------------------------------------
// K calibration: coarse-to-fine 1D search minimizing mean loss over train set.
// ---------------------------------------------------------------------------

static double calibrateK(const Dataset& ds, const std::vector<size_t>& trainIdx, const std::vector<double>& mg,
                          const std::vector<double>& eg) {
    double lo = 0.1, hi = 2.0;
    double bestK = 1.0, bestLoss = std::numeric_limits<double>::infinity();

    for (int refine = 0; refine < 6; ++refine) {
        for (int step = 0; step <= 20; ++step) {
            double K = lo + (hi - lo) * step / 20.0;
            double loss = meanLoss(ds, trainIdx, mg, eg, K);
            if (loss < bestLoss) { bestLoss = loss; bestK = K; }
        }
        double span = (hi - lo) * 0.15;
        lo = std::max(0.01, bestK - span);
        hi = bestK + span;
    }
    return bestK;
}

// ---------------------------------------------------------------------------
// Training loop: mini-batch SGD with early stopping against the holdout set.
// ---------------------------------------------------------------------------

static std::string makeTimestamp() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tmBuf;
#if defined(_WIN32)
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmBuf);
    return std::string(stamp);
}

static void writeCheckpoint(const Options& opt, std::vector<TunableScore>& scores, const std::vector<double>& mg,
                             const std::vector<double>& eg, const std::string& stamp, const std::string& label) {
    for (size_t i = 0; i < scores.size(); ++i) {
        *scores[i].mg = static_cast<int>(std::lround(mg[i]));
        *scores[i].eg = static_cast<int>(std::lround(eg[i]));
    }

    std::filesystem::create_directories(opt.outDir);
    std::string path = opt.outDir + "/weights_" + stamp + "_" + label + ".txt";
    if (save_eval_weights_to_file(path, W)) {
        std::cout << "  checkpoint written: " << path << "\n";
    } else {
        std::cerr << "  warning: failed to write checkpoint to " << path << "\n";
    }
}

// Reports what actually changed, sorted by total |delta| (mg+eg), so a full
// tuning run doesn't leave you skimming ~1400 lines by hand to sanity-check
// it. Compares against whatever mg/eg started training with - hand-set
// defaults, or a --weights checkpoint if one was loaded.
static void writeDiffReport(const Options& opt, std::vector<TunableScore>& scores, const std::vector<double>& startMg,
                             const std::vector<double>& startEg, const std::vector<double>& finalMg,
                             const std::vector<double>& finalEg, const std::string& stamp) {
    struct Row {
        std::string name;
        int beforeMg, afterMg, beforeEg, afterEg;
        int absTotal;
    };

    std::vector<Row> rows;
    for (size_t i = 0; i < scores.size(); ++i) {
        int bMg = static_cast<int>(std::lround(startMg[i]));
        int aMg = static_cast<int>(std::lround(finalMg[i]));
        int bEg = static_cast<int>(std::lround(startEg[i]));
        int aEg = static_cast<int>(std::lround(finalEg[i]));
        if (bMg == aMg && bEg == aEg) continue;
        rows.push_back({scores[i].name, bMg, aMg, bEg, aEg, std::abs(aMg - bMg) + std::abs(aEg - bEg)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.absTotal > b.absTotal; });

    std::filesystem::create_directories(opt.outDir);
    std::string path = opt.outDir + "/diff_" + stamp + ".txt";
    std::ofstream out(path);
    out << "# " << rows.size() << " of " << scores.size() << " parameters changed, sorted by |delta| descending\n";
    out << "# name  mg: before -> after  eg: before -> after\n";
    for (const Row& r : rows) {
        out << r.name << "  mg: " << r.beforeMg << " -> " << r.afterMg << " (" << (r.afterMg - r.beforeMg >= 0 ? "+" : "")
            << (r.afterMg - r.beforeMg) << ")"
            << "  eg: " << r.beforeEg << " -> " << r.afterEg << " (" << (r.afterEg - r.beforeEg >= 0 ? "+" : "")
            << (r.afterEg - r.beforeEg) << ")\n";
    }

    std::cout << "tune: diff report written: " << path << " (" << rows.size() << "/" << scores.size()
              << " parameters changed)\n";
    std::cout << "tune: top " << std::min<size_t>(20, rows.size()) << " changes by magnitude:\n";
    for (size_t i = 0; i < std::min<size_t>(20, rows.size()); ++i) {
        const Row& r = rows[i];
        std::cout << "  " << r.name << "  mg: " << r.beforeMg << " -> " << r.afterMg << "  eg: " << r.beforeEg
                  << " -> " << r.afterEg << "\n";
    }
}

static void train(const Options& opt, Dataset& ds) {
    std::vector<TunableScore> scores = enumerateTunableScores(W);
    std::vector<double> mg(scores.size()), eg(scores.size());
    for (size_t i = 0; i < scores.size(); ++i) {
        mg[i] = *scores[i].mg;
        eg[i] = *scores[i].eg;
    }
    // Whatever training started from - hand-set defaults, or a --weights
    // checkpoint - kept around only for the final diff report.
    const std::vector<double> startMg = mg, startEg = eg;

    // Adam optimizer state. Parameters here span wildly different natural
    // scales (material weights in the hundreds, PSQT/mobility entries in the
    // tens), which a single global SGD learning rate handles poorly - Adam's
    // per-parameter adaptive step size is a direct fix for that.
    constexpr double beta1 = 0.9, beta2 = 0.999, adamEps = 1e-8;
    std::vector<double> mMomentMg(scores.size(), 0.0), vMomentMg(scores.size(), 0.0);
    std::vector<double> mMomentEg(scores.size(), 0.0), vMomentEg(scores.size(), 0.0);
    long long adamStep = 0;

    std::vector<size_t> allIdx(ds.size());
    for (size_t i = 0; i < ds.size(); ++i) allIdx[i] = i;
    std::mt19937_64 rng(opt.seed);
    std::shuffle(allIdx.begin(), allIdx.end(), rng);

    // Three-way split, not two: --test is carved out first and never looked
    // at again until the very end, after bestMg/bestEg is already locked in -
    // --holdout is what early-stopping actually watches during training, so
    // it's implicitly "fit to" via model selection (which checkpoint gets
    // kept). Reporting *its* loss as the final answer would be mildly
    // optimistic; --test never influences any decision, so its number is the
    // honest one.
    size_t testCount = static_cast<size_t>(ds.size() * opt.testFrac);
    size_t holdoutCount = static_cast<size_t>(ds.size() * opt.holdoutFrac);
    std::vector<size_t> testIdx(allIdx.begin(), allIdx.begin() + testCount);
    std::vector<size_t> holdoutIdx(allIdx.begin() + testCount, allIdx.begin() + testCount + holdoutCount);
    std::vector<size_t> trainIdx(allIdx.begin() + testCount + holdoutCount, allIdx.end());

    std::cout << "tune: " << ds.size() << " positions total (" << trainIdx.size() << " train, "
              << holdoutIdx.size() << " holdout, " << testIdx.size() << " test)\n";

    double K = opt.kOverride > 0 ? opt.kOverride : calibrateK(ds, trainIdx, mg, eg);
    std::cout << "tune: K = " << K << (opt.kOverride > 0 ? " (override)\n" : " (calibrated)\n");

    double initialHoldout = meanLoss(ds, holdoutIdx, mg, eg, K);
    double initialTest = meanLoss(ds, testIdx, mg, eg, K);
    std::cout << "tune: initial holdout loss (hand-set weights) = " << initialHoldout << "\n";
    std::cout << "tune: initial test loss (hand-set weights)    = " << initialTest << "\n";

    std::vector<double> bestMg = mg, bestEg = eg;
    double bestHoldout = initialHoldout;
    int checksSinceImprovement = 0;
    long long batchesSinceCheck = 0;

    for (int epoch = 0; epoch < opt.maxEpochs; ++epoch) {
        std::shuffle(trainIdx.begin(), trainIdx.end(), rng);

        for (size_t start = 0; start < trainIdx.size(); start += opt.batchSize) {
            size_t end = std::min(start + static_cast<size_t>(opt.batchSize), trainIdx.size());
            size_t n = end - start;
            if (n == 0) continue;

            std::vector<double> gradMg(scores.size(), 0.0), gradEg(scores.size(), 0.0);

            for (size_t bi = start; bi < end; ++bi) {
                size_t pos = trainIdx[bi];
                double evalCp = evalFromWeights(ds, pos, mg, eg);
                double pred = sigmoid(evalCp, K);
                double diff = pred - ds.result[pos];
                double common = 2.0 * diff * pred * (1.0 - pred) * (K / 400.0);

                for (uint32_t i = ds.offsets[pos]; i < ds.offsets[pos + 1]; ++i) {
                    int idx = ds.flatIdx[i];
                    int cnt = ds.flatCount[i];
                    gradMg[idx] += common * cnt * ds.mgPhase[pos] / 24.0;
                    gradEg[idx] += common * cnt * ds.egPhase[pos] / 24.0;
                }
            }

            ++adamStep;
            double biasCorr1 = 1.0 - std::pow(beta1, static_cast<double>(adamStep));
            double biasCorr2 = 1.0 - std::pow(beta2, static_cast<double>(adamStep));

            for (size_t i = 0; i < scores.size(); ++i) {
                double gMg = gradMg[i] / static_cast<double>(n);
                mMomentMg[i] = beta1 * mMomentMg[i] + (1.0 - beta1) * gMg;
                vMomentMg[i] = beta2 * vMomentMg[i] + (1.0 - beta2) * gMg * gMg;
                double mHatMg = mMomentMg[i] / biasCorr1;
                double vHatMg = vMomentMg[i] / biasCorr2;
                mg[i] -= opt.lr * mHatMg / (std::sqrt(vHatMg) + adamEps);

                double gEg = gradEg[i] / static_cast<double>(n);
                mMomentEg[i] = beta1 * mMomentEg[i] + (1.0 - beta1) * gEg;
                vMomentEg[i] = beta2 * vMomentEg[i] + (1.0 - beta2) * gEg * gEg;
                double mHatEg = mMomentEg[i] / biasCorr1;
                double vHatEg = vMomentEg[i] / biasCorr2;
                eg[i] -= opt.lr * mHatEg / (std::sqrt(vHatEg) + adamEps);
            }

            if (++batchesSinceCheck >= opt.checkIntervalBatches) {
                batchesSinceCheck = 0;
                double holdoutLoss = meanLoss(ds, holdoutIdx, mg, eg, K);
                double trainLoss = meanLoss(ds, trainIdx, mg, eg, K);
                std::cout << "epoch " << epoch << " pos " << start << "/" << trainIdx.size()
                          << "  train=" << trainLoss << "  holdout=" << holdoutLoss;

                if (holdoutLoss < bestHoldout) {
                    bestHoldout = holdoutLoss;
                    bestMg = mg;
                    bestEg = eg;
                    checksSinceImprovement = 0;
                    std::cout << "  (best)\n";
                    writeCheckpoint(opt, scores, mg, eg, makeTimestamp(), "best");
                } else {
                    ++checksSinceImprovement;
                    std::cout << "\n";
                    if (checksSinceImprovement >= opt.patience) {
                        std::cout << "tune: early stopping - no holdout improvement for " << opt.patience
                                  << " checks\n";
                        goto done;
                    }
                }
            }
        }
    }

done:
    std::cout << "tune: finished. best holdout loss = " << bestHoldout << " (started at " << initialHoldout << ")\n";

    // The one and only time the test set gets looked at - after bestMg/bestEg
    // is already fixed, so this number can't have influenced which
    // checkpoint got selected.
    double finalTest = meanLoss(ds, testIdx, bestMg, bestEg, K);
    std::cout << "tune: FINAL TEST LOSS (never touched during training) = " << finalTest
              << " (started at " << initialTest << ")\n";

    std::string finalStamp = makeTimestamp();
    writeCheckpoint(opt, scores, bestMg, bestEg, finalStamp, "final");
    writeDiffReport(opt, scores, startMg, startEg, bestMg, bestEg, finalStamp);
}

int run(int argc, char** argv) {
    Options opt;
    bool datasetExplicit = false;
    bool outDirExplicit = false;
    if (!parseArgs(argc, argv, opt, datasetExplicit, outDirExplicit)) {
        printUsage();
        return 1;
    }

    if (!datasetExplicit) {
        opt.dataset = findNearExecutable(opt.dataset).string();
    }
    if (!outDirExplicit) {
        opt.outDir = (findRepoRoot() / opt.outDir).string();
    }

    zobrist::initZobrist();
    bb::init();
    init_eval_weights_default();

    if (!opt.initWeights.empty()) {
        if (!load_eval_weights_from_file(opt.initWeights)) {
            std::cerr << "tune: --weights '" << opt.initWeights << "' failed to load - aborting rather than "
                      << "silently training from hand-set defaults instead of what you asked for\n";
            return 1;
        }
        std::cout << "tune: starting from weights file '" << opt.initWeights << "'\n";
    }

    std::cout << "tune: dataset path: " << opt.dataset << "\n";
    std::cout << "tune: output directory: " << opt.outDir << "\n";

    Dataset ds;
    size_t linesRead = 0, parseFailures = 0, duplicatesSkipped = 0, notQuietSkipped = 0;
    if (!loadDataset(opt, ds, linesRead, parseFailures, duplicatesSkipped, notQuietSkipped)) {
        return 1;
    }

    std::cout << "tune: dataset load complete\n"
              << "  lines read:         " << linesRead << "\n"
              << "  parse failures:      " << parseFailures << "\n"
              << "  duplicates skipped:  " << duplicatesSkipped << "\n"
              << "  not-quiet skipped:   " << notQuietSkipped << "\n"
              << "  positions accepted:  " << ds.size() << "\n";

    if (ds.size() < 100) {
        std::cerr << "tune: too few usable positions (" << ds.size() << "), aborting\n";
        return 1;
    }

    train(opt, ds);
    return 0;
}

} // namespace tune_cli
