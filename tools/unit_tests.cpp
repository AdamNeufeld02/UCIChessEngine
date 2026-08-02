#include "engine/types.h"
#include "engine/bitboards.h"
#include "engine/move.h"
#include "engine/hashing.h"
#include "engine/board.h"
#include <iostream>
#include "engine/movegen.h"
#include "engine/transpostable.h"
#include <vector>
#include <algorithm>
#include <random>
#include <cmath>
#include <limits>
#include <cstdint>


namespace tests {

int failures = 0;

static char pieceToChar(engine::Piece p) {
    if (p == engine::EMPTY) return '.';

    engine::PieceType pt = typeOf(p);
    engine::Colour col   = colourOf(p);

    char c = '?';
    switch (pt) {
        case engine::PAWN:   c = 'P'; break;
        case engine::KNIGHT: c = 'N'; break;
        case engine::BISHOP: c = 'B'; break;
        case engine::ROOK:   c = 'R'; break;
        case engine::QUEEN:  c = 'Q'; break;
        case engine::KING:   c = 'K'; break;
        case engine::NONE:   c = '.'; break;
        default:             c = '.'; break;
    }
    return (col == engine::WHITE ? c : tolower(c));
}

static void printBoard(const engine::Board& b) {
    std::cout << "\n============== CURRENT BOARD ==============\n\n";

    for (int rank = 7; rank >= 0; --rank) {
        std::cout << (rank + 1) << "  ";
        for (int file = 0; file < 8; ++file) {
            engine::Square sq = static_cast<engine::Square>(rank * 8 + file);
            engine::Piece p   = b.pieceOn(sq);
            std::cout << pieceToChar(p) << " ";
        }
        std::cout << "\n";
    }

    std::cout << "\n   a b c d e f g h\n";

    std::cout << "\nSide to move: "
              << (b.sideToMove() == engine::WHITE ? "White" : "Black") << "\n";

    std::cout << "All pieces BB: 0x" << std::hex << b.allPieces << std::dec << "\n";
    std::cout << "White pieces BB: 0x" << std::hex << b.colourBB[engine::WHITE] << std::dec << "\n";
    std::cout << "Black pieces BB: 0x" << std::hex << b.colourBB[engine::BLACK] << std::dec << "\n";

    std::cout << "White king square: " << int(b.kingSquare(engine::WHITE)) << "\n";
    std::cout << "Black king square: " << int(b.kingSquare(engine::BLACK)) << "\n";

    std::cout << "\n===========================================\n\n";
}


void printBitboard(engine::Bitboard bb) {
    std::cout << "\nBitboard:\n";
    for (int rank = 7; rank >= 0; --rank) {           // 8 ranks, top to bottom
        for (int file = 0; file < 8; ++file) {        // 8 files, left to right
            int sq = rank * 8 + file;                 // square index 0..63
            engine::Bitboard mask = 1ULL << sq;

            std::cout << ((bb & mask) ? "1 " : ". ");
        }
        std::cout << "  " << (rank + 1) << "\n";      // rank label
    }
    std::cout << "\nA B C D E F G H\n\n";             // file labels
}

static std::vector<engine::Move> sortAndUnique(std::vector<engine::Move> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

static void expectMoveSetsEqual(const std::vector<engine::Move>& a,
                                const std::vector<engine::Move>& b,
                                const char* msg) {
    auto sa = sortAndUnique(a);
    auto sb = sortAndUnique(b);

    if (sa != sb) {
        std::cout << "FAIL: " << msg
                  << " (|A|=" << sa.size()
                  << ", |B|=" << sb.size() << ")\n";

        std::cout << "  A moves (sorted, unique):";
        for (auto m : sa) std::cout << " " << +m;
        std::cout << "\n  B moves (sorted, unique):";
        for (auto m : sb) std::cout << " " << +m;
        std::cout << "\n";

        ++failures;
    }
}

template <engine::GenType Type>
static std::vector<engine::Move> collectMovesFiltered(engine::Board& b) {
    using namespace engine;
    MoveList moveList = MoveList<Type>(b);

    std::vector<Move> out;
    out.reserve(moveList.size());

    for (const ScoredMove& sm : moveList) {
        Move m = sm.move;
        if constexpr (Type == GEN_LEGAL) {
            out.push_back(m);
        } else {
            if (b.legalMove(m))
                out.push_back(m);
        }
    }
    return out;
}

void expectEq(engine::Bitboard actual, engine::Bitboard expected, const char* msg) {
    if (actual != expected) {
        std::cout << "FAIL: " << msg << "\n";
        std::cout << "Expected: ";
        printBitboard(expected);
        std::cout << "got: ";
        printBitboard(actual);
        std::cout << "\n";
        ++failures;
    }
}

inline void expectEqInt(int actual, int expected, const char* msg) {
    if (actual != expected) {
        std::cout << "FAIL: " << msg << " (expected " << expected << ", got " << actual << ")\n";
        failures++;
    }
}

void expectTrue(bool cond, const char* msg) {
    if (!cond) {
        std::cout << "FAIL: " << msg << "\n";
        ++failures;
    }
}

template <typename T>
inline void expectEq(T actual, T expected, const char* msg) {
    if (actual != expected) {
        std::cout << "FAIL: " << msg
                  << " (expected " << +expected << ", got " << +actual << ")\n";
        ++failures;
    }
}

// Recompute zobrist from scratch for a given Board+State
inline engine::ZobristKey recomputeZobrist(const engine::Board& b, const engine::State* st) {
    using namespace engine;
    ZobristKey key = 0ULL;

    for (int sq = 0; sq < SQUARECOUNT; ++sq) {
        Square s = static_cast<Square>(sq);
        Piece pc = b.pieceOn(s);
        if (pc != EMPTY) {
            key ^= zobrist::psq[pc][sq];
        }
    }

    key ^= zobrist::castlingKey[static_cast<uint8_t>(st->castlingRights)];

    if (st->epSquare != NOSQUARE) {
        int file = static_cast<int>(st->epSquare) & 7;
        key ^= zobrist::epFile[file];
    }

    if (b.colToMove == BLACK) {
        key ^= zobrist::sideToMoveKey;
    }

    return key;
}

// Recompute the pawn-only zobrist key from scratch (both colours, no
// side-to-move component) to cross-check incremental maintenance in
// board.cpp's makeMove.
inline engine::ZobristKey recomputePawnZobrist(const engine::Board& b) {
    using namespace engine;
    ZobristKey key = 0ULL;

    for (int sq = 0; sq < SQUARECOUNT; ++sq) {
        Square s = static_cast<Square>(sq);
        Piece pc = b.pieceOn(s);
        if (pc != EMPTY && typeOf(pc) == PAWN) {
            key ^= zobrist::psq[pc][sq];
        }
    }

    return key;
}

static inline uint64_t test_mul_hi_u64(uint64_t a, uint64_t b) {
#if defined(_MSC_VER) && defined(_M_X64)
    return __umulh(a, b);
#else
    return (uint64_t)(((__uint128_t)a * (__uint128_t)b) >> 64);
#endif
}

static inline uint64_t test_fast_index(uint64_t x, uint64_t n) {
    return test_mul_hi_u64(x, n);
}

static inline size_t test_bucketIndex(uint64_t key, size_t bucketCount) {
    return (size_t)test_fast_index(key, (uint64_t)bucketCount);
}

static void checkUniformity(size_t nBuckets, uint64_t samples, const char* name) {
    std::mt19937_64 rng(0x12345678ULL);
    std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());

    std::vector<uint32_t> hist(nBuckets, 0);

    for (uint64_t i = 0; i < samples; ++i) {
        uint64_t key = dist(rng);
        size_t idx = test_bucketIndex(key, nBuckets);
        // bounds safety
        if (idx >= nBuckets) {
            expectTrue(false, "fast_index produced out-of-range index (uniformity loop)");
            return;
        }
        hist[idx]++;
    }

    const double expected = (double)samples / (double)nBuckets;
    const double var = expected * (1.0 - 1.0 / (double)nBuckets); // binomial approx
    const double sd = std::sqrt(var);

    double chi2 = 0.0;
    double maxAbsZ = 0.0;
    uint32_t minC = std::numeric_limits<uint32_t>::max();
    uint32_t maxC = 0;

    for (size_t i = 0; i < nBuckets; ++i) {
        const double c = (double)hist[i];
        minC = std::min(minC, hist[i]);
        maxC = std::max(maxC, hist[i]);

        const double diff = c - expected;
        chi2 += (diff * diff) / expected;

        const double z = diff / sd;
        maxAbsZ = std::max(maxAbsZ, std::abs(z));
    }

    const double df = (double)nBuckets - 1.0;
    const double chi2PerDf = chi2 / df;

    std::cout << "  [" << name << "] n=" << nBuckets
              << " samples=" << samples
              << " min=" << minC
              << " max=" << maxC
              << " max|z|=" << maxAbsZ
              << " chi2/df=" << chi2PerDf
              << "\n";

    // These are *statistical sanity checks*, not a proof.
    // Chosen to be conservative so it doesn't flake.
    expectTrue(maxAbsZ < 8.0, "fast_index histogram max|z| too large (suspicious non-uniformity)");
    expectTrue(chi2PerDf > 0.75 && chi2PerDf < 1.35, "fast_index chi2/df out of expected range");
}

void runFastIndexTests() {
    failures = 0;
    std::cout << "Running fast_index tests...\n";

    // -----------------------
    // 1) Bounds tests
    // -----------------------
    {
        std::mt19937_64 rng(0xC0FFEEULL);
        std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());

        const uint64_t Ns[] = {
            1, 2, 3, 5, 7, 10,
            31, 32, 33,
            997, 1000, 1001,
            262144, 262145,
            1000000, 16777216
        };

        constexpr int TRIALS = 1'000'000;

        for (uint64_t n : Ns) {
            expectTrue(n > 0, "n must be > 0");
            for (int i = 0; i < TRIALS; ++i) {
                uint64_t x = dist(rng);
                uint64_t idx = test_fast_index(x, n);
                if (idx >= n) {
                    std::cout << "FAIL: bounds (n=" << n << " idx=" << idx << ")\n";
                    ++failures;
                    break;
                }
            }
        }

        std::cout << (failures == 0 ? "  Bounds: OK\n" : "  Bounds: FAILED\n");
    }

    // -----------------------
    // 2) Uniformity tests
    // -----------------------
    // Keep this moderate so your unit tests don't take forever.
    // You can crank SAMPLES up when you want to stress it.
    {
        constexpr uint64_t SAMPLES = 2'000'000;

        checkUniformity(997,  SAMPLES, "prime-ish");
        checkUniformity(1000, SAMPLES, "non-power-of-two");
        checkUniformity(1024, SAMPLES, "power-of-two");
        checkUniformity(16384, SAMPLES, "bigger");
    }

    std::cout << (failures == 0 ? "All fast_index tests passed.\n"
                                : "fast_index tests FAILED.\n");
}

void runBitshiftTests() {
    using namespace engine;

    failures = 0;
    std::cout << "Running bitboard tests...\n";

    Bitboard bb = bit(D5);

    // Shift southwest
    Bitboard bb_sw = shift<SOUTHWEST>(bb);  // assuming SOUTHWEST = -9 or similar
    // D5 southwest is C4
    expectEq(bb_sw, bit(C4), "D5 southwest should be C4");

    // Shift northeast
    Bitboard bb_ne = shift<NORTHEAST>(bb);  // assuming +9
    // D5 northeast is E6
    expectEq(bb_ne, bit(E6), "D5 northeast should be E6");

    // Edge cases: A-file and H-file
    Bitboard bb_a1 = bit(A1);
    Bitboard bb_west = shift<WEST>(bb_a1);   // depending on your masking, this should become 0
    expectEq(bb_west, Bitboard{0}, "A1 west should be 0 (no wrap)");

    std::cout << (failures == 0 ? "All bitboard tests passed.\n"
                                : "Bitboard tests FAILED.\n");
}

void runBetweenBBTests() {
    using namespace engine;
    using namespace engine::bb;

    failures = 0;
    std::cout << "Running betweenBB / lineBB tests...\n";

    // ----------------------------------------
    // 1. Vertical between: E2 → E7
    // ----------------------------------------
    {
        Bitboard actualBetween = betweenBB[E2][E7];

        Bitboard expectedBetween = 0ULL;
        expectedBetween |= bit(E3);
        expectedBetween |= bit(E4);
        expectedBetween |= bit(E5);
        expectedBetween |= bit(E6);
        expectedBetween |= bit(E7);  // second square always included

        expectEq(actualBetween, expectedBetween, "betweenBB[E2][E7] vertical up");

        // lineBB: full E-file (A2..H2? nope: full file E1..E8)
        Bitboard actualLine = lineBB[E2][E7];

        Bitboard expectedLine = 0ULL;
        expectedLine |= bit(E1);
        expectedLine |= bit(E2);
        expectedLine |= bit(E3);
        expectedLine |= bit(E4);
        expectedLine |= bit(E5);
        expectedLine |= bit(E6);
        expectedLine |= bit(E7);
        expectedLine |= bit(E8);

        expectEq(actualLine, expectedLine, "lineBB[E2][E7] full E-file");
        expectEq(lineBB[E7][E2], expectedLine, "lineBB[E7][E2] symmetry full E-file");
    }

    // E7 → E2
    {
        Bitboard actualBetween = betweenBB[E7][E2];

        Bitboard expectedBetween = 0ULL;
        expectedBetween |= bit(E6);
        expectedBetween |= bit(E5);
        expectedBetween |= bit(E4);
        expectedBetween |= bit(E3);
        expectedBetween |= bit(E2);  // second square always included

        expectEq(actualBetween, expectedBetween, "betweenBB[E7][E2] vertical down");
    }

    // ----------------------------------------
    // 2. Horizontal between: A4 → F4
    // ----------------------------------------
    {
        Bitboard actualBetween = betweenBB[A4][F4];

        Bitboard expectedBetween = 0ULL;
        expectedBetween |= bit(B4);
        expectedBetween |= bit(C4);
        expectedBetween |= bit(D4);
        expectedBetween |= bit(E4);
        expectedBetween |= bit(F4);  // second square always included

        expectEq(actualBetween, expectedBetween, "betweenBB[A4][F4] horizontal right");

        // lineBB: full 4th rank A4..H4
        Bitboard actualLine = lineBB[A4][F4];

        Bitboard expectedLine = 0ULL;
        expectedLine |= bit(A4);
        expectedLine |= bit(B4);
        expectedLine |= bit(C4);
        expectedLine |= bit(D4);
        expectedLine |= bit(E4);
        expectedLine |= bit(F4);
        expectedLine |= bit(G4);
        expectedLine |= bit(H4);

        expectEq(actualLine, expectedLine, "lineBB[A4][F4] full 4th rank");
        expectEq(lineBB[F4][A4], expectedLine, "lineBB[F4][A4] symmetry full 4th rank");
    }

    // F4 → A4
    {
        Bitboard actualBetween = betweenBB[F4][A4];

        Bitboard expectedBetween = 0ULL;
        expectedBetween |= bit(E4);
        expectedBetween |= bit(D4);
        expectedBetween |= bit(C4);
        expectedBetween |= bit(B4);
        expectedBetween |= bit(A4);  // second square always included

        expectEq(actualBetween, expectedBetween, "betweenBB[F4][A4] horizontal left");
    }

    // ----------------------------------------
    // 3. Diagonal between: C1 → H6
    // ----------------------------------------
    {
        Bitboard actualBetween = betweenBB[C1][H6];

        Bitboard expectedBetween = 0ULL;
        expectedBetween |= bit(D2);
        expectedBetween |= bit(E3);
        expectedBetween |= bit(F4);
        expectedBetween |= bit(G5);
        expectedBetween |= bit(H6);  // second square always included

        expectEq(actualBetween, expectedBetween, "betweenBB[C1][H6] diagonal up-right");

        // lineBB: full diagonal C1–H6
        Bitboard actualLine = lineBB[C1][H6];

        Bitboard expectedLine = 0ULL;
        expectedLine |= bit(C1);
        expectedLine |= bit(D2);
        expectedLine |= bit(E3);
        expectedLine |= bit(F4);
        expectedLine |= bit(G5);
        expectedLine |= bit(H6);

        expectEq(actualLine, expectedLine, "lineBB[C1][H6] full diagonal");
        expectEq(lineBB[H6][C1], expectedLine, "lineBB[H6][C1] symmetry diagonal");
    }

    // H6 → C1
    {
        Bitboard actualBetween = betweenBB[H6][C1];

        Bitboard expectedBetween = 0ULL;
        expectedBetween |= bit(G5);
        expectedBetween |= bit(F4);
        expectedBetween |= bit(E3);
        expectedBetween |= bit(D2);
        expectedBetween |= bit(C1);  // second square always included

        expectEq(actualBetween, expectedBetween, "betweenBB[H6][C1] diagonal down-left");
    }

    // ----------------------------------------
    // 4. Non-aligned squares: betweenBB just second, lineBB = 0
    // ----------------------------------------
    {
        Bitboard actualBetween = betweenBB[A1][B3];
        Bitboard actualLine    = lineBB[A1][B3];

        Bitboard expectedBetween = 0ULL;
        expectedBetween |= bit(B3);  // not on same line/diag, only second square

        expectEq(actualBetween, expectedBetween, "betweenBB[A1][B3] non-aligned");
        expectEq(actualLine, Bitboard{0}, "lineBB[A1][B3] non-aligned should be 0");
    }

    {
        Bitboard actualBetween = betweenBB[D4][E6];
        Bitboard actualLine    = lineBB[D4][E6];

        Bitboard expectedBetween = 0ULL;
        expectedBetween |= bit(E6);  // knight move relation, no line => only second

        expectEq(actualBetween, expectedBetween, "betweenBB[D4][E6] knight-like non-aligned");
        expectEq(actualLine, Bitboard{0}, "lineBB[D4][E6] knight-like non-aligned should be 0");
    }

    // ----------------------------------------
    // 5. Same square: betweenBB is that square, lineBB is 0
    // ----------------------------------------
    {
        Bitboard actualBetween = betweenBB[E4][E4];
        Bitboard actualLine    = lineBB[E4][E4];

        Bitboard expectedBetween = 0ULL;
        expectedBetween |= bit(E4);  // second square always included (same as first)

        expectEq(actualBetween, expectedBetween, "betweenBB[E4][E4] same square");
        expectEq(actualLine, Bitboard{0}, "lineBB[E4][E4] same square should be 0");
    }

    std::cout << (failures == 0 ? "All betweenBB / lineBB tests passed.\n"
                                : "betweenBB / lineBB tests FAILED.\n");
}

void runPawnAttackTests() {
    using namespace engine;
    failures = 0;
    std::cout << "Running pawn attack tests...\n";
    // White pawn from D5 → C6, E6
    Bitboard white_attacks = bb::computePawnAttacks(D5, WHITE);
    expectEq(white_attacks, bit(C6) | bit(E6), "White pawn attacks from D5");

    // White pawn from A2 → only B3
    Bitboard wa = bb::computePawnAttacks(A2, WHITE);
    expectEq(wa, bit(B3), "White pawn from A2 should only attack B3");

    // Black pawn from H7 → only G6
    Bitboard ba = bb::computePawnAttacks(H7, BLACK);
    expectEq(ba, bit(G6), "Black pawn from H7 should only attack G6");

    std::cout << (failures == 0 ? "All pawn attack tests passed.\n"
                                : "pawn attack tests FAILED.\n");
}

void runKnightAttackTests() {
    using namespace engine;
    using namespace bb;

    failures = 0;
    std::cout << "Running knight attack tests...\n";

    // Test 1: Knight from D4 (center-ish) on empty board
    {
        Bitboard attacks = computeKnightAttacks(D4);

        Bitboard expected = 0ULL;
        // From D4, knight moves to:
        // B3, B5, C2, C6, E2, E6, F3, F5
        expected |= bit(B3);
        expected |= bit(B5);
        expected |= bit(C2);
        expected |= bit(C6);
        expected |= bit(E2);
        expected |= bit(E6);
        expected |= bit(F3);
        expected |= bit(F5);

        expectEq(attacks, expected, "Knight attacks from D4 on empty board");
    }

    // Test 2: Knight from corner A1
    {
        Bitboard attacks = computeKnightAttacks(A1);

        Bitboard expected = 0ULL;
        // From A1, knight moves to: B3, C2
        expected |= bit(B3);
        expected |= bit(C2);

        expectEq(attacks, expected, "Knight attacks from A1 on empty board");
    }

    // Test 3: Knight from corner H8
    {
        Bitboard attacks = computeKnightAttacks(H8);

        Bitboard expected = 0ULL;
        // From H8, knight moves to: F7, G6
        expected |= bit(F7);
        expected |= bit(G6);

        expectEq(attacks, expected, "Knight attacks from H8 on empty board");
    }

    // Test 4: Knight from edge B1
    {
        Bitboard attacks = computeKnightAttacks(B1);

        Bitboard expected = 0ULL;
        // B1 (file=1, rank=0) → A3, C3, D2
        expected |= bit(A3);
        expected |= bit(C3);
        expected |= bit(D2);

        expectEq(attacks, expected, "Knight attacks from B1 on empty board");
    }

    std::cout << (failures == 0 ? "All knight attack tests passed.\n"
                                : "Knight attack tests FAILED.\n");

}

void runBishopAttackTests() {
    using namespace engine;
    using namespace engine::bb; // if computeSlidingAttacks lives here

    failures = 0;
    std::cout << "Running bishop attack tests...\n";

    // ----------------------------------------
    // Test 1: Bishop from D4 on empty board
    // ----------------------------------------
    {
        Bitboard occ = 0ULL;
        Bitboard attacks = computeBishopAttacks(D4, occ);

        Bitboard expected = 0ULL;
        // NE: E5, F6, G7, H8
        expected |= bit(E5);
        expected |= bit(F6);
        expected |= bit(G7);
        expected |= bit(H8);
        // NW: C5, B6, A7
        expected |= bit(C5);
        expected |= bit(B6);
        expected |= bit(A7);
        // SE: E3, F2, G1
        expected |= bit(E3);
        expected |= bit(F2);
        expected |= bit(G1);
        // SW: C3, B2, A1
        expected |= bit(C3);
        expected |= bit(B2);
        expected |= bit(A1);

        expectEq(attacks, expected, "Bishop attacks from D4 on empty board");
    }

    // ----------------------------------------
    // Test 2: Bishop from D4 with blockers
    // - Friendly blocker at F6 (should block beyond F6)
    // - Enemy blocker at B2 (square included, but not beyond)
    // ----------------------------------------
    {
        Bitboard occ = 0ULL;
        occ |= bit(F6);  // blocker on NE diagonal
        occ |= bit(B2);  // blocker on SW diagonal

        Bitboard attacks = computeBishopAttacks(D4, occ);

        Bitboard expected = 0ULL;
        // NE: E5, F6 (stop at F6, no G7/H8)
        expected |= bit(E5);
        expected |= bit(F6);
        // NW: C5, B6, A7 (no blockers there)
        expected |= bit(C5);
        expected |= bit(B6);
        expected |= bit(A7);
        // SE: E3, F2, G1 (no blockers there)
        expected |= bit(E3);
        expected |= bit(F2);
        expected |= bit(G1);
        // SW: C3, B2 (stop at B2, no A1)
        expected |= bit(C3);
        expected |= bit(B2);

        expectEq(attacks, expected, "Bishop attacks from D4 with blockers on F6/B2");
    }

    // ----------------------------------------
    // Test 3: Edge case - bishop from corner A1 on empty board
    // ----------------------------------------
    {
        Bitboard occ = 0ULL;
        Bitboard attacks = computeBishopAttacks(A1, occ);

        Bitboard expected = 0ULL;
        // From A1 only NE diagonal exists: B2, C3, D4, E5, F6, G7, H8
        expected |= bit(B2);
        expected |= bit(C3);
        expected |= bit(D4);
        expected |= bit(E5);
        expected |= bit(F6);
        expected |= bit(G7);
        expected |= bit(H8);

        expectEq(attacks, expected, "Bishop attacks from A1 on empty board");
    }

    std::cout << (failures == 0 ? "All bishop attack tests passed.\n"
                                : "Bishop attack tests FAILED.\n");
}

void runRookAttackTests() {
    using namespace engine;
    using namespace engine::bb;

    failures = 0;
    std::cout << "Running rook attack tests...\n";

    // ----------------------------------------
    // Test 1: Rook from D4 on empty board
    // ----------------------------------------
    {
        Bitboard occ = 0ULL;
        Bitboard attacks = computeRookAttacks(D4, occ);

        Bitboard expected = 0ULL;

        // North: D5, D6, D7, D8
        expected |= bit(D5);
        expected |= bit(D6);
        expected |= bit(D7);
        expected |= bit(D8);

        // South: D3, D2, D1
        expected |= bit(D3);
        expected |= bit(D2);
        expected |= bit(D1);

        // East: E4, F4, G4, H4
        expected |= bit(E4);
        expected |= bit(F4);
        expected |= bit(G4);
        expected |= bit(H4);

        // West: C4, B4, A4
        expected |= bit(C4);
        expected |= bit(B4);
        expected |= bit(A4);

        expectEq(attacks, expected, "Rook attacks from D4 on empty board");
    }

    // ----------------------------------------
    // Test 2: Rook from D4 with blockers
    // - Friendly blocker at D6 (north)
    // - Enemy blocker at B4 (west)
    // ----------------------------------------
    {
        Bitboard occ = 0ULL;
        occ |= bit(D6);  // north blocker
        occ |= bit(B4);  // west blocker

        Bitboard attacks = computeRookAttacks(D4, occ);

        Bitboard expected = 0ULL;

        // North: D5, D6 (stop at D6, no D7/D8)
        expected |= bit(D5);
        expected |= bit(D6);

        // South: D3, D2, D1 (no blockers)
        expected |= bit(D3);
        expected |= bit(D2);
        expected |= bit(D1);

        // East: E4, F4, G4, H4 (no blockers)
        expected |= bit(E4);
        expected |= bit(F4);
        expected |= bit(G4);
        expected |= bit(H4);

        // West: C4, B4 (stop at B4, no A4)
        expected |= bit(C4);
        expected |= bit(B4);

        expectEq(attacks, expected, "Rook attacks from D4 with blockers on D6/B4");
    }

    // ----------------------------------------
    // Test 3: Edge case - rook from corner A1 on empty board
    // ----------------------------------------
    {
        Bitboard occ = 0ULL;
        Bitboard attacks = computeRookAttacks(A1, occ);

        Bitboard expected = 0ULL;

        // From A1:
        // North: A2, A3, A4, A5, A6, A7, A8
        expected |= bit(A2);
        expected |= bit(A3);
        expected |= bit(A4);
        expected |= bit(A5);
        expected |= bit(A6);
        expected |= bit(A7);
        expected |= bit(A8);

        // East: B1, C1, D1, E1, F1, G1, H1
        expected |= bit(B1);
        expected |= bit(C1);
        expected |= bit(D1);
        expected |= bit(E1);
        expected |= bit(F1);
        expected |= bit(G1);
        expected |= bit(H1);

        // South/West from A1 are off-board and should produce no bits

        expectEq(attacks, expected, "Rook attacks from A1 on empty board");
    }

    std::cout << (failures == 0 ? "All rook attack tests passed.\n"
                                : "Rook attack tests FAILED.\n");
}

void runKingAttackTests() {
    using namespace engine;
    using namespace bb;

    failures = 0;
    std::cout << "Running king attack tests...\n";

    // Test 1: King from D4 on empty board
    {
        Bitboard attacks = computeKingAttacks(D4);

        Bitboard expected = 0ULL;
        // Adjacent squares:
        // Rank up/down
        expected |= bit(D5);
        expected |= bit(D3);
        // File left/right
        expected |= bit(C4);
        expected |= bit(E4);
        // Diagonals
        expected |= bit(C5);
        expected |= bit(E5);
        expected |= bit(C3);
        expected |= bit(E3);

        expectEq(attacks, expected, "King attacks from D4 on empty board");
    }

    // Test 2: King from corner A1
    {
        Bitboard attacks = computeKingAttacks(A1);

        Bitboard expected = 0ULL;
        // From A1, king can go to: A2, B1, B2
        expected |= bit(A2);
        expected |= bit(B1);
        expected |= bit(B2);

        expectEq(attacks, expected, "King attacks from A1 on empty board");
    }

    // Test 3: King from edge H5
    {
        Bitboard attacks = computeKingAttacks(H5);

        Bitboard expected = 0ULL;
        // From H5, valid neighbors:
        // H6, H4, G5, G6, G4
        expected |= bit(H6);
        expected |= bit(H4);
        expected |= bit(G5);
        expected |= bit(G6);
        expected |= bit(G4);

        expectEq(attacks, expected, "King attacks from H5 on empty board");
    }

    std::cout << (failures == 0 ? "All king attack tests passed.\n"
                                : "King attack tests FAILED.\n");

}

void runMoveTests() {
    using namespace engine;

    failures = 0;
    std::cout << "Running move encoding tests...\n";

    // ---------------------------------------------
    // Test 1: basic move (no flag)
    // ---------------------------------------------
    {
        Move m = makeMoveBasic(E2, E4);

        expectEqInt(fromSq(m), E2, "fromSq basic");
        expectEqInt(toSq(m),   E4, "toSq basic");
        expectEqInt(moveFlag(m), NOFLAG, "moveFlag basic");

        expectEqInt(isPromotion(m), false, "isPromotion basic");
        expectEqInt(isEnpassent(m), false, "isEnpassent basic");
        expectEqInt(isCastle(m),    false, "isCastle basic");
    }

    // ---------------------------------------------
    // Test 2: move with flag (castle)
    // ---------------------------------------------
    {
        Move m = makeMoveWithFlag(E1, G1, CASTLE);

        expectEqInt(fromSq(m), E1, "fromSq castle");
        expectEqInt(toSq(m),   G1, "toSq castle");
        expectEqInt(moveFlag(m), CASTLE, "moveFlag castle");

        expectEqInt(isCastle(m), true, "isCastle check");
        expectEqInt(isPromotion(m), false, "isPromotion castle");
    }

    // ---------------------------------------------
    // Test 3: en-passant move
    // ---------------------------------------------
    {
        Move m = makeMoveWithFlag(E5, D6, ENPASSANT);

        expectEqInt(fromSq(m), E5, "fromSq enpassent");
        expectEqInt(toSq(m),   D6, "toSq enpassent");
        expectEqInt(moveFlag(m), ENPASSANT, "moveFlag enpassent");

        expectEqInt(isEnpassent(m), true, "isEnpassent check");
    }

    // ---------------------------------------------
    // Test 4: promotion move
    // ---------------------------------------------
    {
        Move m = makePromoMove(E7, E8, QUEEN);

        expectEqInt(fromSq(m), E7, "fromSq promo");
        expectEqInt(toSq(m),   E8, "toSq promo");
        expectEqInt(moveFlag(m), PROMOTION, "moveFlag promo");

        expectEqInt(isPromotion(m), true, "isPromotion check");

        // Promo piece encoded properly (Queen = PT_Queen)
        expectEqInt(promoPiece(m), QUEEN, "promoPiece decoding");
    }

    // ---------------------------------------------
    // Test 5: promotion as knight
    // ---------------------------------------------
    {
        Move m = makePromoMove(H7, H8, KNIGHT);

        expectEqInt(fromSq(m), H7, "fromSq promo knight");
        expectEqInt(toSq(m),   H8, "toSq promo knight");
        expectEqInt(moveFlag(m), PROMOTION, "moveFlag promo knight");

        expectEqInt(promoPiece(m), KNIGHT, "promoPiece knight");
    }

    // ---------------------------------------------
    // Test 6: round-trip correctness for a variety
    // ---------------------------------------------
    {
        struct Test {
            Square from, to;
            Flags flag;
            PieceType promo;
            bool usePromo;
        };

        Test tests[] = {
            {A1, H8, NOFLAG,    NONE,   false},
            {B2, C3, CASTLE,    NONE,   false},
            {H2, H4, ENPASSANT, NONE,   false},
            {E7, E8, PROMOTION, QUEEN,  true},
            {C7, C8, PROMOTION, KNIGHT, true},
        };

        for (const auto& t : tests) {
            Move m = t.usePromo
                     ? makePromoMove(t.from, t.to, t.promo)
                     : makeMoveWithFlag(t.from, t.to, t.flag);

            expectEqInt(fromSq(m), t.from, "roundtrip fromSq");
            expectEqInt(toSq(m),   t.to,   "roundtrip toSq");

            if (!t.usePromo)
                expectEqInt(moveFlag(m), t.flag, "roundtrip flag");

            if (t.usePromo)
                expectEqInt(promoPiece(m), t.promo, "roundtrip promo");
        }
    }

    std::cout << (failures == 0 ? "All move encoding tests passed.\n"
                                : "Move encoding tests FAILED.\n");
}

void runFenStartposTest() {
    using namespace engine;

    failures = 0;
    std::cout << "Running FEN startpos test...\n";

    Board board;
    State root{};
    const std::string startFen =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    board.fenToBoard(startFen, &root);

    expectEq(board.colToMove, WHITE, "startpos side to move");

    expectEq(board.pieceOn(E1), makePiece(WHITE, KING), "white king on E1");
    expectEq(board.pieceOn(E8), makePiece(BLACK, KING), "black king on E8");
    expectEq(board.pieceOn(A1), makePiece(WHITE, ROOK), "white rook on A1");
    expectEq(board.pieceOn(H8), makePiece(BLACK, ROOK), "black rook on H8");

    int pawnCount = 0;
    for (int sq = 0; sq < SQUARECOUNT; ++sq) {
        Piece pc = board.pieceOn(static_cast<Square>(sq));
        if (typeOf(pc) == PAWN) pawnCount++;
    }
    expectEq(pawnCount, 16, "startpos pawn count = 16");

    expectTrue(root.castlingRights & WhiteKingSide, "white K castling right");
    expectTrue(root.castlingRights & WhiteQueenSide, "white Q castling right");
    expectTrue(root.castlingRights & BlackKingSide, "black K castling right");
    expectTrue(root.castlingRights & BlackQueenSide, "black Q castling right");

    expectEq(root.epSquare, NOSQUARE, "startpos epSquare = NOSQUARE");

    ZobristKey recomputed = recomputeZobrist(board, &root);
    expectEq(root.boardKey, recomputed, "startpos zobrist matches recomputed");

    std::cout << (failures == 0 ? "All fen start pos tests passed.\n"
                                : "fen start pos tests FAILED.\n");
}

void runQuietMoveTest() {
    using namespace engine;

    failures = 0;
    std::cout << "Running quiet move test (e2e4)...\n";

    Board board;
    State root{};
    State after{};
    const std::string startFen =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    board.fenToBoard(startFen, &root);

    Move e2e4 = makeMoveBasic(E2, E4);
    board.makeMove(e2e4, &after);

    expectEq(board.pieceOn(E2), EMPTY, "e2 empty after e2e4");
    expectEq(board.pieceOn(E4), makePiece(WHITE, PAWN), "white pawn on e4");

    expectEq(board.colToMove, BLACK, "side to move after e2e4");

    ZobristKey recomputedAfter = recomputeZobrist(board, &after);
    expectEq(after.boardKey, recomputedAfter, "zobrist after e2e4 matches recomputed");

    board.undoMove(e2e4);

    expectEq(board.pieceOn(E2), makePiece(WHITE, PAWN), "e2 pawn restored after undo");
    expectEq(board.pieceOn(E4), EMPTY, "e4 empty after undo");
    expectEq(board.colToMove, WHITE, "side to move restored after undo");

    ZobristKey recomputedRoot = recomputeZobrist(board, &root);
    expectEq(root.boardKey, recomputedRoot, "zobrist restored to root after undo");

    std::cout << (failures == 0 ? "All quiet move tests passed.\n"
                                : "quiet move tests FAILED.\n");
}

void runCaptureMoveTest() {
    using namespace engine;

    failures = 0;
    std::cout << "Running capture move test...\n";

    Board board;
    State root{};
    State after{};

    // Simple position: white pawn on e4, black pawn on d5
    const std::string fen =
        "k7/8/8/3p4/4P3/8/8/K7 w - - 0 1";

    board.fenToBoard(fen, &root);

    Move e4d5 = makeMoveBasic(E4, D5);

    board.makeMove(e4d5, &after);

    expectEq(board.pieceOn(E4), EMPTY, "e4 empty after capture");
    expectEq(board.pieceOn(D5), makePiece(WHITE, PAWN), "white pawn on d5 after capture");

    expectEq(after.captured, makePiece(BLACK, PAWN), "captured piece stored in state");

    board.undoMove(e4d5);

    expectEq(board.pieceOn(E4), makePiece(WHITE, PAWN), "white pawn back on e4 after undo");
    expectEq(board.pieceOn(D5), makePiece(BLACK, PAWN), "black pawn restored on d5 after undo");

    ZobristKey recomputedRoot = recomputeZobrist(board, &root);
    expectEq(root.boardKey, recomputedRoot, "capture undo zobrist restored");

    std::cout << (failures == 0 ? "All capture move tests passed.\n"
                            : "capture move tests FAILED.\n");
}

void runEnPassantMoveTest() {
    using namespace engine;

    failures = 0;
    std::cout << "Running en-passant move test...\n";

    Board board;
    State root{};
    State after{};

    // Position: white pawn on e5, black pawn on d5, ep square d6, white to move
    const std::string fen =
        "k7/8/8/3pP3/8/8/8/K7 w - d6 0 1";

    board.fenToBoard(fen, &root);

    Move epMove = makeMoveWithFlag(E5, D6, ENPASSANT);

    board.makeMove(epMove, &after);

    expectEq(board.pieceOn(E5), EMPTY, "e5 empty after ep");
    expectEq(board.pieceOn(D5), EMPTY, "d5 pawn captured ep");
    expectEq(board.pieceOn(D6), makePiece(WHITE, PAWN), "white pawn on d6 after ep");

    board.undoMove(epMove);

    expectEq(board.pieceOn(E5), makePiece(WHITE, PAWN), "e5 pawn restored after undo ep");
    expectEq(board.pieceOn(D5), makePiece(BLACK, PAWN), "d5 pawn restored after undo ep");
    expectEq(board.pieceOn(D6), EMPTY, "d6 empty after undo ep");

    ZobristKey recomputedRoot = recomputeZobrist(board, &root);
    expectEq(root.boardKey, recomputedRoot, "ep undo zobrist restored");

    std::cout << (failures == 0 ? "All en-passant move tests passed.\n"
                            : "en-passant move tests FAILED.\n");
}

void runEpRepetitionHashTest() {
    using namespace engine;

    failures = 0;
    std::cout << "Running ep-square repetition hash test...\n";

    // A double push whose ep target has no enemy pawn adjacent must be
    // hash-identical to the same final placement with no ep square at all —
    // the "possible moves" are unaffected by it, so the position is the same
    // for repetition purposes. See board.cpp makeMove/fenToBoard.
    {
        Board viaPush;
        State s0{}, s1{};
        viaPush.fenToBoard("4k3/5p2/8/8/8/8/8/4K3 b - - 0 1", &s0);
        viaPush.makeMove(makeMoveBasic(F7, F5), &s1);

        Board viaFen;
        State t0{};
        viaFen.fenToBoard("4k3/8/8/5p2/8/8/8/4K3 w - - 0 1", &t0);

        expectTrue(viaPush.epSquare() == NOSQUARE, "uncapturable ep square not set");
        expectEq(viaPush.key(), viaFen.key(), "uncapturable ep does not affect hash");
    }

    // A double push whose ep target CAN actually be captured must set the ep
    // square, and must hash differently from the same placement without it,
    // since the legal moves genuinely differ.
    {
        Board viaPush;
        State s0{}, s1{};
        viaPush.fenToBoard("4k3/5p2/8/4P1P1/8/8/8/4K3 b - - 0 1", &s0);
        viaPush.makeMove(makeMoveBasic(F7, F5), &s1);

        Board viaFen;
        State t0{};
        viaFen.fenToBoard("4k3/8/8/4PpP1/8/8/8/4K3 w - - 0 1", &t0);

        expectTrue(viaPush.epSquare() == F6, "capturable ep square is set");
        expectTrue(viaPush.key() != viaFen.key(), "capturable ep does affect hash");
    }

    std::cout << (failures == 0 ? "All ep-square repetition hash tests passed.\n"
                            : "ep-square repetition hash tests FAILED.\n");
}

void runPromotionMoveTest() {
    using namespace engine;

    failures = 0;
    std::cout << "Running promotion move test...\n";

    Board board;
    State root{};
    State after{};

    // White pawn on e7, black bishop e8, white king h1
    const std::string fen =
        "k3b3/4P3/8/8/8/8/8/7K w - - 0 1";

    board.fenToBoard(fen, &root);

    Move promo = makePromoMove(E7, E8, QUEEN);

    board.makeMove(promo, &after);

    expectEq(board.pieceOn(E7), EMPTY, "e7 empty after promotion");
    expectEq(board.pieceOn(E8), makePiece(WHITE, QUEEN), "white queen on e8 after promotion");

    // Undo
    board.undoMove(promo);

    expectEq(board.pieceOn(E7), makePiece(WHITE, PAWN), "white pawn back on e7 after undo promo");
    expectEq(board.pieceOn(E8), makePiece(BLACK, BISHOP), "black bishop restored on e8 after undo promo");

    ZobristKey recomputedRoot = recomputeZobrist(board, &root);
    expectEq(root.boardKey, recomputedRoot, "promo undo zobrist restored");

    std::cout << (failures == 0 ? "All promotion move tests passed.\n"
                            : "promotion move tests FAILED.\n");
}

void runGenAttacksBBTests() {
    using namespace engine;
    using namespace engine::bb;

    failures = 0;
    std::cout << "Running genAttacksBB tests...\n";

    // ----------------------------------------
    // Knights & kings: should ignore occupancy
    // ----------------------------------------
    {
        Bitboard occ1 = 0ULL;
        Bitboard occ2 = bit(A1) | bit(H8) | bit(D4); // random junk

        Bitboard k1 = genAttacksBB<KNIGHT>(D4, occ1);
        Bitboard k2 = genAttacksBB<KNIGHT>(D4, occ2);
        Bitboard expectedKnight = knightAttacks[D4];

        expectEq(k1, expectedKnight, "genAttacksBB KNIGHT (occ = 0) from D4");
        expectEq(k2, expectedKnight, "genAttacksBB KNIGHT (with occ) from D4");
    }

    {
        Bitboard occ1 = 0ULL;
        Bitboard occ2 = bit(C3) | bit(E5);

        Bitboard k1 = genAttacksBB<KING>(E4, occ1);
        Bitboard k2 = genAttacksBB<KING>(E4, occ2);
        Bitboard expectedKing = kingAttacks[E4];

        expectEq(k1, expectedKing, "genAttacksBB KING (occ = 0) from E4");
        expectEq(k2, expectedKing, "genAttacksBB KING (with occ) from E4");
    }

    // ----------------------------------------
    // Bishops & rooks: empty board vs reference
    // ----------------------------------------
    {
        Bitboard occ = 0ULL;

        Bitboard bAtt = genAttacksBB<BISHOP>(D4, occ);
        Bitboard bRef = computeBishopAttacks(D4, occ);
        expectEq(bAtt, bRef, "genAttacksBB BISHOP vs computeBishopAttacks on empty board (D4)");

        Bitboard rAtt = genAttacksBB<ROOK>(D4, occ);
        Bitboard rRef = computeRookAttacks(D4, occ);
        expectEq(rAtt, rRef, "genAttacksBB ROOK vs computeRookAttacks on empty board (D4)");
    }

    // ----------------------------------------
    // Bishops & rooks: with blockers (same as existing tests)
    // ----------------------------------------
    {
        // Bishop from D4 with blockers F6, B2
        Bitboard occ = 0ULL;
        occ |= bit(F6);
        occ |= bit(B2);

        Bitboard bAtt = genAttacksBB<BISHOP>(D4, occ);
        Bitboard bRef = computeBishopAttacks(D4, occ);
        expectEq(bAtt, bRef, "genAttacksBB BISHOP vs computeBishopAttacks with blockers on F6/B2");
    }

    {
        // Rook from D4 with blockers D6, B4
        Bitboard occ = 0ULL;
        occ |= bit(D6);
        occ |= bit(B4);

        Bitboard rAtt = genAttacksBB<ROOK>(D4, occ);
        Bitboard rRef = computeRookAttacks(D4, occ);
        expectEq(rAtt, rRef, "genAttacksBB ROOK vs computeRookAttacks with blockers on D6/B4");
    }

    // ----------------------------------------
    // Queen: should be bishop | rook for same occupancy
    // ----------------------------------------
    {
        Bitboard occ = 0ULL;
        occ |= bit(D6);
        occ |= bit(B4);
        occ |= bit(F6);
        occ |= bit(B2);

        Bitboard qAtt = genAttacksBB<QUEEN>(D4, occ);
        Bitboard bRef = computeBishopAttacks(D4, occ);
        Bitboard rRef = computeRookAttacks(D4, occ);
        Bitboard expected = bRef | rRef;

        expectEq(qAtt, expected, "genAttacksBB QUEEN == bishop|rook from D4 with blockers");
    }

    // ----------------------------------------
    // Edge cases for sliders: corners
    // ----------------------------------------
    {
        Bitboard occ = 0ULL;
        Bitboard bAtt = genAttacksBB<BISHOP>(A1, occ);
        Bitboard bRef = computeBishopAttacks(A1, occ);
        expectEq(bAtt, bRef, "genAttacksBB BISHOP from A1 on empty board");
    }

    {
        Bitboard occ = 0ULL;
        Bitboard rAtt = genAttacksBB<ROOK>(H8, occ);
        Bitboard rRef = computeRookAttacks(H8, occ);
        expectEq(rAtt, rRef, "genAttacksBB ROOK from H8 on empty board");
    }

    std::cout << (failures == 0 ? "All genAttacksBB tests passed.\n"
                                : "genAttacksBB tests FAILED.\n");
}

void runLegalMoveTests() {
    using namespace engine;

    failures = 0;
    std::cout << "Running legalMove() tests...\n";

    State root{};

    //----------------------------------------------------------
    // 1. EN PASSANT: ILLEGAL due to discovered rook check
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("7k/8/8/2rPpK2/8/8/8/8 w - e6 0 1", &root);
        Move m = makeMoveWithFlag(D5, E6, ENPASSANT);
        expectEq(b.legalMove(m), false, "illegal en passant discovered rook check");
    }

    //----------------------------------------------------------
    // 2. EN PASSANT: LEGAL normal case (same board w/o rook)
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("7k/8/8/3PpK2/8/8/8/8 w - e6 0 1", &root);
        Move m = makeMoveWithFlag(D5, E6, ENPASSANT);
        expectEq(b.legalMove(m), true, "legal en passant");
    }

    //----------------------------------------------------------
    // 3. CASTLING ILLEGAL: king passes through attacked square
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/8/8/8/8/6b1/R3K2R w KQ - 0 1", &root);
        Move m = makeMoveWithFlag(E1, G1, CASTLE);
        expectEq(b.legalMove(m), false, "illegal castle (passed-through-check)");
    }

    //----------------------------------------------------------
    // 4. CASTLING LEGAL: standard K-side castle
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", &root);
        Move m = makeMoveWithFlag(E1, G1, CASTLE);
        expectEq(b.legalMove(m), true, "legal castle");
    }

    //----------------------------------------------------------
    // 5. KING MOVE ILLEGAL: king moves into rook check
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1", &root);
        Move m = makeMoveWithFlag(E1, D2, NOFLAG);
        expectEq(b.legalMove(m), false, "illegal king move into check");
    }

    //----------------------------------------------------------
    // 6. KING MOVE LEGAL
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1", &root);
        Move m = makeMoveWithFlag(E1, D1, NOFLAG);
        expectEq(b.legalMove(m), true, "legal king move");
    }

    //----------------------------------------------------------
    // 7. PINNED KNIGHT: ILLEGAL move off pin line
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/8/8/4r3/8/4N3/4K3 w - - 0 1", &root);

        // Knight pseudo legal move
        Move legal_along = makeMoveWithFlag(E2, G3, NOFLAG);
        expectEq(b.legalMove(legal_along), false, "illegal pinned knight move");
    }

    //----------------------------------------------------------
    // 8. PINNED ROOK: LEGAL move toward the pinner (along line)
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/4r3/8/8/8/4R3/4K3 w - - 0 1", &root);

        Move m = makeMoveWithFlag(E2, E4, NOFLAG);
        expectEq(b.legalMove(m), true, "rook pinned: must move in line");
    }

    //----------------------------------------------------------
    // 9. PINNED ROOK: LEGAL move (along file)
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/4r3/8/8/8/4R3/4K3 w - - 0 1", &root);

        Move m = makeMoveWithFlag(E2, E3, NOFLAG); // along pin line
        expectEq(b.legalMove(m), true, "rook pinned: legal along file");
    }

    //----------------------------------------------------------
    // 10. PINNED ROOK: ILLEGAL sideways move
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/4r3/8/8/8/4R3/4K3 w - - 0 1", &root);
        Move m = makeMoveWithFlag(E2, F2, NOFLAG); // sideways
        expectEq(b.legalMove(m), false, "rook pinned: illegal sideways");
    }

    //----------------------------------------------------------
    // 11. PINNED BISHOP: LEGAL move along diagonal pin line
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/7b/8/8/4B3/8/2K5 w - - 0 1", &root);
        Move m = makeMoveWithFlag(E3, F4, NOFLAG); // diagonal toward rook
        expectEq(b.legalMove(m), true, "pinned bishop: legal along diagonal");
    }

    //----------------------------------------------------------
    // 12. PINNED BISHOP: ILLEGAL diagonal off the pin line
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/7b/8/8/4B3/8/2K5 w - - 0 1", &root);
        Move m = makeMoveWithFlag(E3, D4, NOFLAG);  // diagonal but not on pin ray
        expectEq(b.legalMove(m), false, "pinned bishop: illegal off diagonal pin line");
    }

    //----------------------------------------------------------
    // 13. PINNED PAWN: legal forward move along pin line
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/4q3/8/8/8/8/4P3/4K3 w - - 0 1", &root);
        Move m = makeMoveWithFlag(E2, E3, NOFLAG);
        expectEq(b.legalMove(m), true, "pinned pawn: legal forward along file");
    }

    //----------------------------------------------------------
    // 14. PINNED PAWN: illegal diagonal capture off file
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/4q3/8/8/8/3r4/4P3/4K3 w - - 0 1", &root);
        Move m = makeMoveWithFlag(E2, D3, NOFLAG); // diagonal
        expectEq(b.legalMove(m), false, "pinned pawn: illegal diagonal");
    }

    //----------------------------------------------------------
    // 15. LEGAL CASTLING: rook square attacked
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k2r/8/8/8/8/8/8/4K2R w K - 0 1", &root);
        Move m = makeMoveWithFlag(E1, G1, CASTLE);
        expectEq(b.legalMove(m), true, "legal castle: rook square attacked");
    }

    //----------------------------------------------------------
    // 16. ILLEGAL CASTLING: king is in check
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4rk2/8/8/8/8/8/8/4K2R w K - 0 1", &root);
        Move m = makeMoveWithFlag(E1, G1, CASTLE);
        expectEq(b.legalMove(m), false, "illegal castle: king in check");
    }

    //----------------------------------------------------------
    // 17. ILLEGAL CASTLING: king passes through check
    //----------------------------------------------------------
    {
        Board b;
        b.fenToBoard("4k3/8/8/8/8/8/1b6/R3K2R w KQ - 0 1", &root);
        Move m = makeMoveWithFlag(E1, C1, CASTLE);
        expectEq(b.legalMove(m), false, "illegal castle: passes through check");
    }

    std::cout << (failures == 0 ? "All legalMove() tests passed.\n"
                                : "legalMove() tests FAILED.\n");
}

void runGenLegalConsistencyTests() {
    using namespace engine;

    failures = 0;
    std::cout << "Running movegen consistency tests (GEN_LEGAL vs subgenerators)...\n";

    // --------------------------
    // Positions where NOT in check:
    // GEN_LEGAL == GEN_CAPTURES ∪ GEN_QUIETS
    // --------------------------
    {
        struct NonCheckCase {
            const char* fen;
            const char* name;
        };

        NonCheckCase cases[] = {
            {
                "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                "startpos"
            },
            {
                "r3k2r/pppq1ppp/2npbn2/4p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w kq - 0 10",
                "castle_middlegame"
            },
            {
                "8/4P3/k7/8/8/8/8/4K2k w - - 0 1",
                "quiet_promotion"
            },
            {
                "4r3/4P3/8/8/8/8/8/4K2k w - - 0 1",
                "capture_promotion"
            },
        };

        for (const auto& c : cases) {
            Board b;
            State root{};
            b.fenToBoard(c.fen, &root);

            Bitboard checkers = b.checkers();
            if (checkers != 0ULL) {
                std::cout << "WARNING: Non-check test position '" << c.name
                          << "' is actually in check (checkers=0x"
                          << std::hex << checkers << std::dec << ")\n";
            }

            auto legal  = collectMovesFiltered<GEN_LEGAL>(b);
            auto caps   = collectMovesFiltered<GEN_CAPTURES>(b);
            auto quiets = collectMovesFiltered<GEN_QUIETS>(b);

            std::vector<Move> unionCQ = caps;
            unionCQ.insert(unionCQ.end(), quiets.begin(), quiets.end());

            expectMoveSetsEqual(unionCQ, legal,
                                (std::string("GEN_LEGAL vs GEN_CAPTURES∪GEN_QUIETS (") +
                                 c.name + ")").c_str());
        }
    }

    // --------------------------
    // Positions where IN check:
    // GEN_LEGAL == GEN_EVASIONS
    // --------------------------
    {
        struct CheckCase {
            const char* fen;
            const char* name;
        };

        CheckCase cases[] = {
            {
                "4k3/7q/r7/8/4R3/8/8/4K3 b - - 0 1",
                "single_check_file"
            },
            {
                "R3k3/8/pp4bb/8/4R3/8/8/4K3 b - - 0 1",
                "double_check"
            },
        };

        for (const auto& c : cases) {
            Board b;
            State root{};
            b.fenToBoard(c.fen, &root);

            Bitboard checkers = b.checkers();
            if (checkers == 0ULL) {
                std::cout << "WARNING: Check test position '" << c.name
                          << "' is NOT in check (checkers=0)\n";
            }

            auto legal    = collectMovesFiltered<GEN_LEGAL>(b);
            auto evasions = collectMovesFiltered<GEN_EVASIONS>(b);

            expectMoveSetsEqual(evasions, legal,
                                (std::string("GEN_LEGAL vs GEN_EVASIONS (") +
                                 c.name + ")").c_str());
        }
    }

    std::cout << (failures == 0
                    ? "All movegen consistency tests passed.\n"
                    : "movegen consistency tests FAILED.\n");
}

void runTranspositionTableBasicTests() {
    using namespace engine;

    failures = 0;
    std::cout << "Running transposition table basic read/write tests...\n";

    // Make a TT and give it some space. (Use >= 1MB, your resize() clamps anyway.)
    TranspositionTable tt;
    tt.resize(8);
    tt.clear();
    tt.newSearch(); // generation increments

    // -----------------------------
    // 1) Miss -> write -> hit cycle
    // -----------------------------
    {
        Board b;
        State root{};
        const std::string fen =
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

        b.fenToBoard(fen, &root);

        const ZobristKey key = root.boardKey;

        // Probe should MISS first.
        {
            auto [hit, data, writer] = tt.probe(key);
            expectEq(hit, false, "TT first probe should miss");

            // Write some deterministic content.
            Move m = makeMoveBasic(E2, E4);
            Value eval  = NOVALUE;
            Value value = Value(-34);
            int depth   = 7;

            writer.write(key, m, eval, value, depth, /*generation*/ 1, EXACT);
        }

        // Probe should HIT now, and data should match.
        {
            auto [hit, data, writer] = tt.probe(key);
            (void)writer;

            expectEq(hit, true, "TT second probe should hit");
            expectEq(data.move,  makeMoveBasic(E2, E4), "TT move round-trip");
            expectEq(data.eval,  NOVALUE,               "TT eval round-trip");
            expectEq(data.value, Value(-34),            "TT value round-trip");
            expectEqInt((int)data.depth, 7,             "TT depth round-trip");
            expectEq(data.bound, EXACT,                 "TT bound round-trip");
        }
    }

    // ----------------------------------------------------------
    // 2) "Worse write" should NOT replace (lower depth, non-EXACT)
    // ----------------------------------------------------------
    {
        Board b;
        State root{};
        const std::string fen =
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

        b.fenToBoard(fen, &root);
        const ZobristKey key = root.boardKey;

        // Seed the entry with a good record.
        {
            auto [hit, data, writer] = tt.probe(key);
            (void)hit; (void)data;

            writer.write(key,
                         makeMoveBasic(G1, F3),
                         Value(100),
                         Value(200),
                         /*depth*/ 10,
                         /*generation*/ 2,
                         EXACT);
        }

        // Attempt to overwrite with LOWER depth and non-EXACT bound.
        // According to TTEntry::save(), this should NOT replace value/eval/depth/bound
        // (it may update move if move != 0, but your logic only guarantees non-replace
        //  for the main fields; we assert those).
        {
            auto [hit, data, writer] = tt.probe(key);
            expectEq(hit, true, "TT should hit seeded record before overwrite attempt");

            writer.write(key,
                         makeMoveBasic(B1, C3),
                         Value(-5),
                         Value(-6),
                         /*depth*/ 3,
                         /*generation*/ 3,
                         LOWER); // or UPPER, just not EXACT
        }

        // Verify core fields stayed as the better record.
        {
            auto [hit, data, writer] = tt.probe(key);
            (void)writer;

            expectEq(hit, true, "TT should still hit after overwrite attempt");
            expectEq(data.eval,  Value(100), "TT eval should not be replaced by lower depth non-EXACT");
            expectEq(data.value, Value(200), "TT value should not be replaced by lower depth non-EXACT");
            expectEqInt((int)data.depth, 10, "TT depth should not be replaced by lower depth non-EXACT");
            expectEq(data.bound, EXACT,      "TT bound should not be replaced by lower depth non-EXACT");
        }
    }

    // -----------------------------------------
    // 3) Different position key should be a miss
    // -----------------------------------------
    {
        Board b;
        State root{};
        const std::string fenDifferent =
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"; // after e2e4

        b.fenToBoard(fenDifferent, &root);
        const ZobristKey key = root.boardKey;

        auto [hit, data, writer] = tt.probe(key);
        (void)data; (void)writer;

        expectEq(hit, false, "TT probe for different FEN key should miss (unless replaced by chance)");
    }

    std::cout << (failures == 0 ? "All transposition table tests passed.\n"
                                : "transposition table tests FAILED.\n");
}

void runSeeThresholdTests() {
    using namespace engine;

    failures = 0;
    std::cout << "Running SEE threshold (seeThreshold) tests...\n";

    // ----------------------------------------------------------
    // 1) Flagged moves short-circuit to true (CASTLE/EP/PROMO)
    // ----------------------------------------------------------
    {
        Board b;
        State root{};
        b.fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &root);

        // Even if the move isn't legal in this position, seeThreshold returns true immediately on flags.
        expectTrue(b.seeThreshold(makeMoveWithFlag(E1, G1, CASTLE), 0), "SEE flagged CASTLE returns true");
        expectTrue(b.seeThreshold(makeMoveWithFlag(E5, D6, ENPASSANT), 0), "SEE flagged ENPASSANT returns true");
        expectTrue(b.seeThreshold(makePromoMove(E7, E8, QUEEN), 0), "SEE flagged PROMOTION returns true");
    }

    // ----------------------------------------------------------
    // 2) Immediate fail: capturedValue - thresh < 0 => false
    // Knight takes pawn, but thresh too high
    // ----------------------------------------------------------
    {
        Board b;
        State root{};
        b.fenToBoard("4k3/8/8/4p3/4N3/8/8/4K3 w - - 0 1", &root);

        Move m = makeMoveBasic(E4, E5); // Nxe5 (capture pawn)
        expectEq(b.seeThreshold(m, 200), false, "SEE fails immediately when threshold too high");
    }

    // ----------------------------------------------------------
    // 3) Immediate pass: exchange <= 0 after subtracting capt-from piece
    // Pawn takes queen should pass even with decent threshold
    // ----------------------------------------------------------
    {
        Board b;
        State root{};
        b.fenToBoard("4k3/8/8/4q3/3P4/8/8/4K3 w - - 0 1", &root);

        Move m = makeMoveBasic(D4, E5); // dxe5 capturing queen on e5
        expectEq(b.seeThreshold(m, 0), true, "SEE winning capture passes (thresh=0)");
        expectEq(b.seeThreshold(m, 200), true, "SEE winning capture passes (thresh=200)");
    }

    // ----------------------------------------------------------
    // 4) Pinned recapture is masked out by your pinned filtering
    // Case A: black rook e7 pinned to king e8 by white rook e1
    // White plays Nxe5 (E4->E5). Rook recapture exists but should be ignored.
    // ----------------------------------------------------------
    {
        Board b;
        State root{};
        b.fenToBoard("4k3/4r3/8/4p3/4N3/8/8/4R1K1 w - - 0 1", &root);

        Move m = makeMoveBasic(E4, E5); // Nxe5
        expectEq(b.seeThreshold(m, 0), true, "SEE ignores pinned recapture attacker (pinned rook masked)");
    }

    // Case B: remove the pin (white rook not on e1), rook recapture should now count -> fail at thresh=0
    {
        Board b;
        State root{};
        b.fenToBoard("4k3/4r3/8/4p3/4N3/8/8/R5K1 w - - 0 1", &root);

        Move m = makeMoveBasic(E4, E5); // Nxe5
        expectEq(b.seeThreshold(m, 0), false, "SEE counts unpinned recapture attacker (should fail)");
    }

    std::cout << (failures == 0 ? "All SEE threshold tests passed.\n"
                                : "SEE threshold tests FAILED.\n");
}

void runPawnKeyIncrementalTests() {
    using namespace engine;

    failures = 0;
    std::cout << "Running incremental pawn-key maintenance tests...\n";

    // 1) Quiet pawn move: key changes and matches from-scratch recompute; undo restores it.
    {
        Board board;
        State root{}, after{};
        board.fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &root);

        ZobristKey beforeKey = board.pawnKey();
        expectEq(beforeKey, recomputePawnZobrist(board), "startpos pawn key matches recompute");

        Move e2e4 = makeMoveBasic(E2, E4);
        board.makeMove(e2e4, &after);
        expectEq(board.pawnKey(), recomputePawnZobrist(board), "pawn key after e2e4 matches recompute");
        expectTrue(board.pawnKey() != beforeKey, "pawn key changes after a pawn push");

        board.undoMove(e2e4);
        expectEq(board.pawnKey(), beforeKey, "pawn key restored after undo of quiet pawn move");
    }

    // 2) Non-pawn move: pawn key must be completely unaffected.
    {
        Board board;
        State root{}, after{};
        board.fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &root);
        ZobristKey beforeKey = board.pawnKey();

        Move g1f3 = makeMoveBasic(G1, F3);
        board.makeMove(g1f3, &after);
        expectEq(board.pawnKey(), beforeKey, "pawn key unchanged after knight move");
        expectEq(board.pawnKey(), recomputePawnZobrist(board), "pawn key after knight move matches recompute");

        board.undoMove(g1f3);
        expectEq(board.pawnKey(), beforeKey, "pawn key restored after undo of knight move");
    }

    // 3) Pawn capture.
    {
        Board board;
        State root{}, after{};
        board.fenToBoard("k7/8/8/3p4/4P3/8/8/K7 w - - 0 1", &root);

        Move e4d5 = makeMoveBasic(E4, D5);
        board.makeMove(e4d5, &after);
        expectEq(board.pawnKey(), recomputePawnZobrist(board), "pawn key after pawn capture matches recompute");

        board.undoMove(e4d5);
        expectEq(board.pawnKey(), recomputePawnZobrist(board), "pawn key restored after undo of pawn capture");
    }

    // 4) En-passant capture.
    {
        Board board;
        State root{}, after{};
        board.fenToBoard("k7/8/8/3pP3/8/8/8/K7 w - d6 0 1", &root);

        Move epMove = makeMoveWithFlag(E5, D6, ENPASSANT);
        board.makeMove(epMove, &after);
        expectEq(board.pawnKey(), recomputePawnZobrist(board), "pawn key after ep capture matches recompute");

        board.undoMove(epMove);
        expectEq(board.pawnKey(), recomputePawnZobrist(board), "pawn key restored after undo of ep capture");
    }

    // 5) Promotion: the pawn must leave the pawn key entirely (the promoted
    // piece is not a pawn), not just move square.
    {
        Board board;
        State root{}, after{};
        board.fenToBoard("k3b3/4P3/8/8/8/8/8/7K w - - 0 1", &root);

        Move promo = makePromoMove(E7, E8, QUEEN);
        board.makeMove(promo, &after);
        expectEq(board.pawnKey(), recomputePawnZobrist(board), "pawn key after promotion matches recompute");

        board.undoMove(promo);
        expectEq(board.pawnKey(), recomputePawnZobrist(board), "pawn key restored after undo of promotion");
    }

    // 6) Castling: no pawns involved, pawn key must not move at all.
    {
        Board board;
        State root{}, after{};
        board.fenToBoard("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", &root);
        ZobristKey beforeKey = board.pawnKey();

        Move castle = makeMoveWithFlag(E1, G1, CASTLE);
        board.makeMove(castle, &after);
        expectEq(board.pawnKey(), beforeKey, "pawn key unchanged after castling");

        board.undoMove(castle);
        expectEq(board.pawnKey(), beforeKey, "pawn key unchanged after undo of castling");
    }

    // 7) Null move: pawn key must not move at all.
    {
        Board board;
        State root{}, after{};
        board.fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &root);
        ZobristKey beforeKey = board.pawnKey();

        board.makeNullMove(&after);
        expectEq(board.pawnKey(), beforeKey, "pawn key unchanged after null move");

        board.undoNullMove();
        expectEq(board.pawnKey(), beforeKey, "pawn key unchanged after undo null move");
    }

    std::cout << (failures == 0 ? "All incremental pawn-key tests passed.\n"
                                : "incremental pawn-key tests FAILED.\n");
}

void runPawnHashCacheTests() {
    using namespace engine;

    failures = 0;
    std::cout << "Running pawn hash cache tests...\n";

    // Function-local static: PAWN_TABLE_SIZE entries at 64 bytes each is a
    // couple MB, too big to want repeatedly on the stack.
    static PawnEntry table[PAWN_TABLE_SIZE];

    // 1) A probe populates the entry, and the cached values match direct,
    // uncached calls to the same eval terms.
    {
        std::fill(std::begin(table), std::end(table), PawnEntry{});

        Board board;
        State root{};
        board.fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &root);

        Value v1 = evaluate(board, table);
        Value v2 = evaluate(board, table);
        expectEqInt(v1, v2, "pawn hash: repeated eval on an unchanged position is stable");

        PawnEntry& pe = table[board.pawnKey() & (PAWN_TABLE_SIZE - 1)];
        expectEq(pe.key, board.pawnKey(), "pawn hash: entry key matches position after probe");

        Score freshWhite = evaluatePawnStructure<WHITE>(board);
        Score freshBlack = evaluatePawnStructure<BLACK>(board);
        expectTrue(pe.pawnScore[WHITE].mg == freshWhite.mg && pe.pawnScore[WHITE].eg == freshWhite.eg,
                   "pawn hash: cached white pawn score matches fresh computation");
        expectTrue(pe.pawnScore[BLACK].mg == freshBlack.mg && pe.pawnScore[BLACK].eg == freshBlack.eg,
                   "pawn hash: cached black pawn score matches fresh computation");

        Score freshShelterWhite = evaluateKingShelter<WHITE>(board);
        expectTrue(pe.shelterScore[WHITE].mg == freshShelterWhite.mg && pe.shelterScore[WHITE].eg == freshShelterWhite.eg,
                   "pawn hash: cached white shelter score matches fresh computation");
    }

    // 2) A king-only move (pawn key unchanged) must recompute shelter but
    // must NOT touch the cached pawn-structure scores.
    {
        std::fill(std::begin(table), std::end(table), PawnEntry{});

        Board board;
        State root{}, after{};
        board.fenToBoard("4k3/8/8/8/4P3/8/8/7K w - - 0 1", &root);

        evaluate(board, table);
        PawnEntry& pe = table[board.pawnKey() & (PAWN_TABLE_SIZE - 1)];
        Score pawnScoreBefore[COLOURNB] = {pe.pawnScore[WHITE], pe.pawnScore[BLACK]};

        Move kingMove = makeMoveBasic(H1, H2);
        board.makeMove(kingMove, &after);
        expectEq(board.pawnKey(), pe.key, "pawn hash: pawn key unchanged after a king-only move (sanity)");

        evaluate(board, table);
        expectTrue(pe.pawnScore[WHITE].mg == pawnScoreBefore[WHITE].mg && pe.pawnScore[WHITE].eg == pawnScoreBefore[WHITE].eg,
                   "pawn hash: pawnScore[WHITE] untouched by a king-only move");
        expectTrue(pe.pawnScore[BLACK].mg == pawnScoreBefore[BLACK].mg && pe.pawnScore[BLACK].eg == pawnScoreBefore[BLACK].eg,
                   "pawn hash: pawnScore[BLACK] untouched by a king-only move");
        expectEq(pe.kingSquare[WHITE], board.kingSquare(WHITE), "pawn hash: cached white king square updated after king move");
    }

    // 3) A stale/colliding entry (wrong key) must be fully replaced, not
    // partially reused.
    {
        std::fill(std::begin(table), std::end(table), PawnEntry{});

        Board board;
        State root{};
        board.fenToBoard("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &root);

        size_t idx = board.pawnKey() & (PAWN_TABLE_SIZE - 1);
        table[idx].key = board.pawnKey() ^ 0xABCDEF1234567890ULL;
        table[idx].pawnScore[WHITE] = Score{9999, 9999};
        table[idx].pawnScore[BLACK] = Score{9999, 9999};
        table[idx].kingSquare[WHITE] = A1;
        table[idx].kingSquare[BLACK] = A1;

        evaluate(board, table);

        Score freshWhite = evaluatePawnStructure<WHITE>(board);
        expectEq(table[idx].key, board.pawnKey(), "pawn hash: stale entry key replaced after mismatch");
        expectTrue(table[idx].pawnScore[WHITE].mg == freshWhite.mg && table[idx].pawnScore[WHITE].eg == freshWhite.eg,
                   "pawn hash: stale garbage pawnScore overwritten with fresh computation");
    }

    // 4) Masked indexing always stays in bounds.
    {
        uint64_t testKeys[] = {0ULL, ~0ULL, 1ULL, (uint64_t)PAWN_TABLE_SIZE, (uint64_t)PAWN_TABLE_SIZE - 1, (uint64_t)PAWN_TABLE_SIZE + 1};
        for (uint64_t k : testKeys) {
            size_t idx = k & (PAWN_TABLE_SIZE - 1);
            expectTrue(idx < PAWN_TABLE_SIZE, "pawn hash: masked index stays in bounds");
        }
    }

    std::cout << (failures == 0 ? "All pawn hash cache tests passed.\n"
                                : "pawn hash cache tests FAILED.\n");
}

}

namespace tests_cli{

    int run(int argc, char** argv) {
        (void)argc;
        (void)argv;
        engine::zobrist::initZobrist();
        engine::bb::init();
        engine::init_eval_weights_default();
        tests::runBitshiftTests();
        tests::runBetweenBBTests();
        tests::runPawnAttackTests();
        tests::runKnightAttackTests();
        tests::runBishopAttackTests();
        tests::runRookAttackTests();
        tests::runKingAttackTests();
        tests::runMoveTests();
        tests::runFenStartposTest();
        tests::runQuietMoveTest();
        tests::runCaptureMoveTest();
        tests::runEnPassantMoveTest();
        tests::runEpRepetitionHashTest();
        tests::runPromotionMoveTest();
        tests::runGenAttacksBBTests();
        tests::runLegalMoveTests();
        tests::runGenLegalConsistencyTests();
        tests::runFastIndexTests();
        tests::runTranspositionTableBasicTests();
        tests::runSeeThresholdTests();
        tests::runPawnKeyIncrementalTests();
        tests::runPawnHashCacheTests();
        return 0;
    }
}