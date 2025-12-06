#include "engine/types.h"
#include "engine/bitboards.h"
#include <iostream>


namespace tests {

int failures = 0;

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

void expect_true(bool cond, const char* msg) {
    if (!cond) {
        std::cout << "FAIL: " << msg << "\n";
        ++failures;
    }
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

}

namespace tests_cli{

    int run(int argc, char** argv){
        tests::runBitshiftTests();
        tests::runPawnAttackTests();
        tests::runKnightAttackTests();
        tests::runBishopAttackTests();
        tests::runRookAttackTests();
        tests::runKingAttackTests();
        return 0;
    }
}