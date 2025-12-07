#pragma once

#include <cstdint>
#include "engine/types.h"

namespace engine {

typedef uint16_t Move;

enum Flags : uint8_t {
    NOFLAG    = 0,
    ENPASSENT = 1,
    PROMOTION = 2,
    CASTLE    = 3
};

// Encode promotion: promoCode = pieceType - 2  (KNIGHT=2 → 0, BISHOP=3 → 1, etc.)
constexpr uint16_t encodePromo(PieceType promo) {
    return static_cast<uint16_t>((static_cast<int>(promo) - 2) & 0b11);
}

// ---------------- PACKING ----------------

constexpr Move makeMoveBasic(Square from, Square to) {
    return static_cast<Move>(
          static_cast<Move>(from)
        | (static_cast<Move>(to) << 6)
        | (static_cast<Move>(NOFLAG) << 14)
    );
}

constexpr Move makeMoveWithFlag(Square from, Square to, Flags flag) {
    return static_cast<Move>(
          static_cast<Move>(from)
        | (static_cast<Move>(to) << 6)
        | (static_cast<Move>(flag) << 14)
    );
}

constexpr Move makePromoMove(Square from, Square to, PieceType promo, Flags flag = PROMOTION) {
    return static_cast<Move>(
          static_cast<Move>(from)
        | (static_cast<Move>(to) << 6)
        | (encodePromo(promo) << 12)
        | (static_cast<Move>(flag) << 14)
    );
}

// ---------------- UNPACKING ----------------

constexpr Square fromSq(Move m) {
    return static_cast<Square>(m & 0b111111);
}

constexpr Square toSq(Move m) {
    return static_cast<Square>((m >> 6) & 0b111111);
}

constexpr Flags moveFlag(Move m) {
    return static_cast<Flags>((m >> 14) & 0b11);
}

constexpr bool isPromotion(Move m) {
    return moveFlag(m) == PROMOTION;
}

constexpr bool isEnpassent(Move m) {
    return moveFlag(m) == ENPASSENT;
}

constexpr bool isCastle(Move m) {
    return moveFlag(m) == CASTLE;
}

constexpr PieceType promoPiece(Move m) {
    return static_cast<PieceType>(((m >> 12) & 0b11) + 2);  
}

}