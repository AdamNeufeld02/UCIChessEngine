#pragma once

#include "move.h"
#include "board.h"
#include "movegen.h"

namespace engine {

enum Stage : uint8_t{
    TTMove,
    CapturesInit,
    Captures,
    QuietsInit,
    Quiets,
    EvasionsInit,
    Evasions,
    Done
};


class MoveSelector {
public:
    MoveSelector(Move ttm, Board& bd, bool quiets);

    Move selectMove();

    template<GenType Type>
    void score();


private:
    Move pickBest(ScoredMove* end);

    ScoredMove moves[MAXMOVES];
    Board& board;
    Stage stage;
    Move ttMove;
    bool allowQuiets;
    ScoredMove* cur;
    ScoredMove* endCur;
    ScoredMove* capEnd;
};

}