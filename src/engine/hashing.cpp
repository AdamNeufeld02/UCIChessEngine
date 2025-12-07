#include "engine/hashing.h"
#include <random>

namespace engine {

namespace zobrist {

ZobristKey psq[PIECECOUNT][SQUARECOUNT];
ZobristKey castlingKey[16];
ZobristKey epFile[8];
ZobristKey sideToMoveKey;

static uint64_t random64() {
    static std::mt19937_64 rng(0xABCDEF123456789ULL);
    std::uniform_int_distribution<uint64_t> dist;
    return dist(rng);
}

void initZobrist() {
    // piece-square keys
    for (int p = 0; p < PIECECOUNT; ++p) {
        for (int sq = 0; sq < SQUARECOUNT; ++sq) {
            psq[p][sq] = random64();
        }
    }

    // castling rights (0..15)
    for (int i = 0; i < 16; ++i)
        castlingKey[i] = random64();

    // en-passant files (A..H)
    for (int f = 0; f < 8; ++f)
        epFile[f] = random64();

    // side to move
    sideToMoveKey = random64();
}
}
}