#pragma once

#include "move.h"
#include "board.h"
#include "movegen.h"
#include "search.h"

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

static const int MVV_SCALE = 16;
static const int CAPT_HIST_SCALE = 1;
static const int MAIN_HIST_SCALE = 2;
static const int CONT_HIST_1_SCALE = 1;
static const int CONT_HIST_2_SCALE = 1;


class MoveSelector {
public:
    MoveSelector(Move ttm, Board& bd, bool quiets, SearchStack* ss, History& hist);

    Move selectMove();

    template<GenType Type>
    void score();


private:
    Move pickBest(ScoredMove* end);
    ScoredMove* eraseMove(ScoredMove* first, ScoredMove* last, Move m);

    ScoredMove moves[MAXMOVES];
    Board& board;
    Stage stage;
    Move ttMove;
    bool allowQuiets;
    ScoredMove* cur;
    ScoredMove* endCur;
    ScoredMove* capEnd;

    SearchStack* ss;
    History& history;
};

inline ScoredMove* MoveSelector::eraseMove(ScoredMove* first, ScoredMove* last, Move m) {
    for (ScoredMove* p = first; p != last; ++p) {
        if (p->move == m) {
            *p = *(last - 1);
            return last - 1;
        }
    }
    return last;
}

}