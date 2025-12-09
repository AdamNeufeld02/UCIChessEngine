#pragma once
#include <cstdint>

namespace engine{

typedef uint64_t Bitboard;

enum Colour {
    WHITE, BLACK, COLOURNB
};

constexpr Colour operator~(Colour c) {
    return Colour(c ^ 1);
}

enum PieceType {
    NONE,
    PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
};

enum Piece {
    EMPTY, 
    WHITEPAWN = PAWN, WHITEKNIGHT, WHITEBISHOP, WHITEROOK, WHITEQUEEN, WHITEKING,
    BLACKPAWN = WHITEPAWN + 8, BLACKKNIGHT, BLACKBISHOP, BLACKROOK, BLACKQUEEN, BLACKKING,
    PIECECOUNT
};

enum Square : int8_t {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    SQUARECOUNT,
    NOSQUARE,
};

enum Direction {
    NORTH = 8,
    EAST = 1,
    SOUTH = -NORTH,
    WEST = -EAST,
    NORTHEAST = NORTH + EAST,
    SOUTHEAST = SOUTH + EAST,
    NORTHWEST = NORTH + WEST,
    SOUTHWEST = SOUTH + WEST
};

inline Square operator+(Square sq, Direction dir) {
    return static_cast<Square>(static_cast<int>(sq) + static_cast<int>(dir));
}

inline Square operator+(Direction dir, Square sq) {
    return sq + dir;
}

inline Square& operator+=(Square& sq, Direction dir) {
    sq = static_cast<Square>(static_cast<int>(sq) + static_cast<int>(dir));
    return sq;
}

enum CastlingRight : uint8_t {
    WhiteKingSide  = 1 << 0,
    WhiteQueenSide = 1 << 1,
    BlackKingSide  = 1 << 2,
    BlackQueenSide = 1 << 3,
    CastlingRightsNone = 0,
    CastlingRightNb = 4,
    WhiteCastling = WhiteKingSide | WhiteQueenSide,
    BlackCastling = BlackKingSide | BlackQueenSide,
    AllCastling = WhiteCastling | BlackCastling,
};

constexpr CastlingRight operator~(CastlingRight cr) {
    return static_cast<CastlingRight>(~static_cast<uint8_t>(cr) & 0xF);
}

constexpr CastlingRight operator|(CastlingRight a, CastlingRight b) {
    return static_cast<CastlingRight>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b)
    );
}

constexpr CastlingRight operator&(CastlingRight a, CastlingRight b) {
    return static_cast<CastlingRight>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b)
    );
}

constexpr CastlingRight castlingRights[64] = {
    ~WhiteQueenSide, AllCastling, AllCastling, AllCastling, ~WhiteCastling, AllCastling, AllCastling, ~WhiteKingSide,
    AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling,
    AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling,
    AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling,
    AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling,
    AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling,
    AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling, AllCastling,
    ~BlackQueenSide, AllCastling, AllCastling, AllCastling, ~BlackCastling, AllCastling, AllCastling, ~BlackKingSide,
};

constexpr Piece makePiece(Colour c, PieceType pt) {
    return Piece((c << 3) + pt);
}

inline Colour colourOf(Piece pc) {
    return Colour(pc >> 3);
}

inline PieceType typeOf(Piece pc) {
    return PieceType(pc & 7);
}

inline Square squareFromFileRank(int file, int rank) {
    return static_cast<Square>(rank * 8 + file);
}

};