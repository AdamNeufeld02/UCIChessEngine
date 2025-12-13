#pragma once

#include <algorithm>
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
ScoredMove* generate(const Board& board, ScoredMove* moveList);


template<GenType T>
struct MoveList {

    explicit MoveList(const Board& pos)
        : last(generate<T>(pos, moveList)) {}

    ScoredMove* begin() { return moveList; }
    ScoredMove* end()   { return last; }

    const ScoredMove* begin() const { return moveList; }
    const ScoredMove* end()   const { return last; }

    size_t size() const { return static_cast<size_t>(last - moveList); }

    bool contains(Move m) const {
        return std::find_if(begin(), end(),
            [m](const ScoredMove& sm) { return sm.move == m; }
        ) != end();
    }

private:
    ScoredMove moveList[MAXMOVES];
    ScoredMove* last;
};

}

