#pragma once
#include "types.h"

namespace engine{

inline Piece pieceFromFenChar(char c) {
    Colour col = (c >= 'A' && c <= 'Z') ? WHITE : BLACK;
    char lower = (c >= 'A' && c <= 'Z') ? (c + 'a' - 'A') : c;

    PieceType pt;
    switch (lower) {
        case 'p': pt = PAWN;   break;
        case 'n': pt = KNIGHT; break;
        case 'b': pt = BISHOP; break;
        case 'r': pt = ROOK;   break;
        case 'q': pt = QUEEN;  break;
        case 'k': pt = KING;   break;
        default:  pt = NONE;   break;
    }
    return makePiece(col, pt);
}

inline int fileFromChar(char f) { return f - 'a'; }
inline int rankFromChar(char r) { return r - '1'; }

}