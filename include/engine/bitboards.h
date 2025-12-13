#pragma once
#include "types.h"

namespace engine {

struct Magic {
    Bitboard* attacks;
    Bitboard occMask;
    Bitboard magic;
    int shift;
};

constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileBBB = FileABB << 1;
constexpr Bitboard FileCBB = FileABB << 2;
constexpr Bitboard FileDBB = FileABB << 3;
constexpr Bitboard FileEBB = FileABB << 4;
constexpr Bitboard FileFBB = FileABB << 5;
constexpr Bitboard FileGBB = FileABB << 6;
constexpr Bitboard FileHBB = FileABB << 7;

constexpr Bitboard Rank1BB = 0xFF;
constexpr Bitboard Rank2BB = Rank1BB << (8 * 1);
constexpr Bitboard Rank3BB = Rank1BB << (8 * 2);
constexpr Bitboard Rank4BB = Rank1BB << (8 * 3);
constexpr Bitboard Rank5BB = Rank1BB << (8 * 4);
constexpr Bitboard Rank6BB = Rank1BB << (8 * 5);
constexpr Bitboard Rank7BB = Rank1BB << (8 * 6);
constexpr Bitboard Rank8BB = Rank1BB << (8 * 7);

extern Magic rookMagics[SQUARECOUNT];
extern Magic bishopMagics[SQUARECOUNT];

constexpr int ROOK_ATTACK_TABLE_SIZE = 102400;
constexpr int BISHOP_ATTACK_TABLE_SIZE = 5248;

// Precomputed Attack Sets
// Rook and Bishop attack sets are handled by magic bitboard lookups
extern Bitboard pawnAttacks[COLOURNB][SQUARECOUNT];
extern Bitboard knightAttacks[SQUARECOUNT];
extern Bitboard kingAttacks[SQUARECOUNT];
extern Bitboard rookAttackTable[ROOK_ATTACK_TABLE_SIZE];
extern Bitboard bishopAttackTable[BISHOP_ATTACK_TABLE_SIZE];

extern Bitboard betweenBB[SQUARECOUNT][SQUARECOUNT];
extern Bitboard lineBB[SQUARECOUNT][SQUARECOUNT];
extern Bitboard castleMasks[BlackQueenSide + 1];


inline int popcount(Bitboard b) {
    return __builtin_popcountll(b);
}

inline int lsb(Bitboard b) {
    return __builtin_ctzll(b);
}

inline int msb(Bitboard b) {
    return 63 - __builtin_clzll(b);
}

inline Square pop_lsb(Bitboard& bb) {
    int sq = lsb(bb);
    bb &= bb-1;
    return static_cast<Square>(sq);
}

inline int squarescan_lsb(Bitboard& b) {
    int sq = __builtin_ctzll(b);
    b &= (b - 1);
    return sq;
}

template<Direction Dir>
constexpr Bitboard shift(Bitboard bb) {
    if constexpr (Dir == NORTH) {
        return bb << 8;
    } else if constexpr (Dir == SOUTH) {
        return bb >> 8;
    } else if constexpr (Dir == EAST) {
        return (bb & ~FileHBB) << 1;
    } else if constexpr (Dir == WEST) {
        return (bb & ~FileABB) >> 1;
    } else if constexpr (Dir == NORTHEAST) {
        return (bb & ~FileHBB) << 9;
    } else if constexpr (Dir == NORTHWEST) {
        return (bb & ~FileABB) << 7;
    } else if constexpr (Dir == SOUTHEAST) {
        return (bb & ~FileHBB) >> 7;
    } else if constexpr (Dir == SOUTHWEST) {
        return (bb & ~FileABB) >> 9;
    } else {
        static_assert(Dir == NORTH, "Unsupported Direction in shift<>");
        return bb;
    }
}

inline void setBit(Bitboard& bb, Square sq) {
    bb = bb | ((Bitboard)1 << sq);
}

inline Bitboard bit(Square sq) {
    return ((Bitboard)1 << sq);
}

inline bool moreThanOne(Bitboard bb) {
    return bb & (bb - 1);
}

inline int fileOf(int sq) {
    return sq & 7;
}

inline int rankOf(int sq) {
    return sq >> 3;
}

namespace bb {
    void init();
    void initMagicBitboards();
    void initBetweenBBAndLineBB();

    Bitboard computePawnAttacks(Square sq, Colour col);
    Bitboard computeRookAttacks(Square sq, Bitboard occ);
    Bitboard computeBishopAttacks(Square sq, Bitboard occ);
    Bitboard computeKnightAttacks(Square sq);
    Bitboard computeKingAttacks(Square sq);

    template<Direction Dir>
    Bitboard slidingAttacksInDir(Square sq, Bitboard occ);

    template<Direction... Dirs>
    Bitboard computeSlidingAttacks(Square sq, Bitboard occ);

    template<Direction... Dirs>
    constexpr Bitboard stepInDirections(Bitboard bb);

    Bitboard rookMask(Square sq);
    Bitboard bishopMask(Square sq);
    Bitboard indexToOccupancy(int index, Bitboard mask);
}

inline Bitboard rookAttacks(Square sq, Bitboard occ) {
    const Magic& m = rookMagics[sq];
    Bitboard key = ((occ & m.occMask) * m.magic) >> m.shift;
    return m.attacks[key];
}

inline Bitboard bishopAttacks(Square sq, Bitboard occ) {
    const Magic& m = bishopMagics[sq];
    Bitboard key = ((occ & m.occMask) * m.magic) >> m.shift;
    return m.attacks[key];
}

template<PieceType pt>
inline Bitboard genAttacksBB(Square square, Bitboard occ) {
    using namespace bb;
    switch (pt) {
    case KNIGHT:
        return knightAttacks[square];
    case BISHOP:
        return bishopAttacks(square, occ);
    case ROOK:
        return rookAttacks(square, occ);
    case QUEEN:
        return bishopAttacks(square, occ) | rookAttacks(square, occ);
    case KING:
        return kingAttacks[square];
    default:
        return 0ULL;
    }
}

inline Bitboard genAttacksBB(PieceType pt, Square sq, Bitboard occ) {
    using namespace bb;
    switch (pt) {
    case KNIGHT:
        return genAttacksBB<KNIGHT>(sq, occ);
    case BISHOP:
        return genAttacksBB<BISHOP>(sq, occ);
    case ROOK:
        return genAttacksBB<ROOK>(sq, occ);
    case QUEEN:
        return genAttacksBB<QUEEN>(sq, occ);
    case KING:
        return genAttacksBB<KING>(sq, occ);
    default:
        return 0ULL;
    }
}

}