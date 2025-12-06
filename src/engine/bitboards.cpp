#include "engine/bitboards.h"

namespace engine::bb {

void init(){

}

template<Direction Dir>
Bitboard slidingAttacksInDir(Square sq, Bitboard occ) {
    Bitboard attacks = 0;
    Bitboard ray = shift<Dir>(bit(sq));

    while (ray) {
        attacks |= ray;
        if (ray & occ) break;
        ray = shift<Dir>(ray);
    }
    return attacks;
}

template<Direction... Dirs>
Bitboard computeSlidingAttacks(Square sq, Bitboard occ) {
    Bitboard attacks = 0;
    ((attacks |= slidingAttacksInDir<Dirs>(sq, occ)), ...);
    return attacks;
}

template<Direction... Dirs>
constexpr Bitboard stepInDirections(Bitboard bb) {
    Bitboard attacks = 0;
    ((attacks |= shift<Dirs>(bb)), ...);
    return attacks;
}

Bitboard computePawnAttacks(Square sq, Colour col){
    Bitboard bb = bit(sq);
    if (col == WHITE) {
        return stepInDirections<NORTHEAST, NORTHWEST>(bb);
    } else {
        return stepInDirections<SOUTHEAST, SOUTHWEST>(bb);
    }
}

Bitboard computeRookAttacks(Square sq, Bitboard occ){
    return computeSlidingAttacks<NORTH, SOUTH, EAST, WEST>(sq, occ);
}

Bitboard computeBishopAttacks(Square sq, Bitboard occ) {
    return computeSlidingAttacks<NORTHWEST, NORTHEAST, SOUTHEAST, SOUTHWEST>(sq, occ);
}

Bitboard computeKingAttacks(Square sq) {
    return stepInDirections<
        NORTH, SOUTH, EAST, WEST,
        NORTHEAST, NORTHWEST, SOUTHEAST, SOUTHWEST>
        (bit(sq));
}

Bitboard computeKnightAttacks(Square sq) {
    Bitboard bb = bit(sq);

    return
        shift<NORTH>(shift<NORTHEAST>(bb)) |  // N + NE = NNE
        shift<NORTH>(shift<NORTHWEST>(bb)) |  // N + NW = NNW

        shift<SOUTH>(shift<SOUTHEAST>(bb)) |  // S + SE = SSE
        shift<SOUTH>(shift<SOUTHWEST>(bb)) |  // S + SW = SSW

        shift<EAST>(shift<NORTHEAST>(bb)) |   // E + NE = ENE
        shift<EAST>(shift<SOUTHEAST>(bb)) |   // E + SE = ESE

        shift<WEST>(shift<NORTHWEST>(bb)) |   // W + NW = WNW
        shift<WEST>(shift<SOUTHWEST>(bb));    // W + SW = WSW
}

};