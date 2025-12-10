#include "engine/types.h"
#include "engine/bitboards.h"
#include "engine/move.h"
#include "engine/hashing.h"
#include "engine/board.h"
#include <iostream>
#include "engine/movegen.h"
#include <vector>
#include <algorithm>


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
    Move buffer[256];
    Move* end = generate<Type>(b, buffer);

    std::vector<Move> out;
    out.reserve(end - buffer);

    for (Move* m = buffer; m != end; ++m) {
        if constexpr (Type == GEN_LEGAL) {
            out.push_back(*m);
        } else {
            if (b.legalMove(*m))
                out.push_back(*m);
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

}

namespace tests_cli{

    int run(int argc, char** argv) {
        (void)argc;
        (void)argv;
        engine::bb::init();
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
        tests::runPromotionMoveTest();
        tests::runGenAttacksBBTests();
        tests::runLegalMoveTests();
        tests::runGenLegalConsistencyTests();
        return 0;
    }
}