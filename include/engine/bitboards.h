#pragma once
#include "Types.h"

namespace engine {

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


inline int popcount(Bitboard b) {
    return __builtin_popcountll(b);
}

inline int lsb(Bitboard b) {
    return __builtin_ctzll(b);  // count trailing zeros
}

inline int msb(Bitboard b) {
    return 63 - __builtin_clzll(b);  // count leading zeros
}

inline Bitboard pop_lsb(Bitboard& b) {
    Bitboard l = b & -b;  // isolate lowest 1 bit
    b ^= l;
    return l;
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
        // Moving east: anything on file H disappears
        return (bb & ~FileHBB) << 1;
    } else if constexpr (Dir == WEST) {
        // Moving west: anything on file A disappears
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
        return bb; // unreachable, but keeps compiler happy
    }
}

inline void setBit(Bitboard& bb, Square sq) {
    bb = bb | ((Bitboard)1 << sq);
}

inline Bitboard bit(Square sq) {
    return ((Bitboard)1 << sq);
}

namespace bb {
    void init();

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
}

}