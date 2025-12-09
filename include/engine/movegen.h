#pragma once

#include "move.h"
#include "types.h"
#include "board.h"

namespace engine {

enum GenType {
    GEN_PSEUDO_LEGAL,
    GEN_LEGAL,
    GEN_CAPTURES,
    GEN_QUIETS,
    GEN_EVASIONS,
};


template<GenType>
Move* generate(const Board& board, Move* moveList);

}