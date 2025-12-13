#include "engine/moveselector.h"
#include "engine/eval.h"

namespace engine {

MoveSelector::MoveSelector(Move ttm, Board& bd, bool quiets) :
    ttMove(ttm),
    board(bd),
    allowQuiets(quiets) {
        if (ttm && board.pseudoLegalMove(ttm)) {
            stage = TTMove;
        } else if (!board.checkers()) {
            stage = CapturesInit;
        } else {
            stage = EvasionsInit;
        }
    }

template<GenType Type>
void MoveSelector::score() {
    if (Type == GEN_QUIETS) return;

    for (ScoredMove* p = cur; p != endCur; p++) {
        Square to = toSq(p->move);
        Flags flag = moveFlag(p->move);
        
        int victimValue = 0;
        int promValue = 0;
        if (flag == ENPASSANT) {
            victimValue = W.material[PHASEMG][PAWN];
        } else {
            PieceType cap = typeOf(board.pieceOn(to));
            victimValue = W.material[PHASEMG][cap];
        }

        if (flag == PROMOTION) {
            promValue = W.material[PHASEMG][promoPiece(p->move)];
        }
        p->score = victimValue + promValue;
    }
}

Move MoveSelector::selectMove() {
    while (true) {
        switch (stage) {

        case TTMove:
            stage = board.checkers() ? EvasionsInit : CapturesInit;
            {
                Move m = ttMove;
                return m;
            }

        case CapturesInit:
            cur = moves;
            endCur = capEnd = generate<GEN_CAPTURES>(board, moves);
            if (ttMove) capEnd = endCur = eraseMove(cur, capEnd, ttMove);
            score<GEN_CAPTURES>();
            stage = Captures;
            [[fallthrough]];

        case Captures:
            if (cur != capEnd)
                return pickBest(capEnd);
            stage = allowQuiets ? QuietsInit : Done;
            break;

        case QuietsInit:
            cur = capEnd;
            endCur = generate<GEN_QUIETS>(board, capEnd);
            if (ttMove) endCur = eraseMove(cur, endCur, ttMove);
            score<GEN_QUIETS>();
            stage = Quiets;
            [[fallthrough]];

        case Quiets:
            if (cur != endCur)
                return pickBest(endCur);
            stage = Done;
            break;

        case EvasionsInit:
            cur = moves;
            endCur = generate<GEN_EVASIONS>(board, moves);
            if (ttMove) endCur = eraseMove(cur, endCur, ttMove);
            score<GEN_EVASIONS>();
            stage = Evasions;
            [[fallthrough]];

        case Evasions:
            if (cur != endCur)
                return pickBest(endCur);
            stage = Done;
            break;

        case Done:
        default:
            return NOMOVE;
        }
    }
}

Move MoveSelector::pickBest(ScoredMove* end) {
    if (stage == Quiets) return (cur++)->move;
    ScoredMove* best = cur;

    for (ScoredMove* p = cur + 1; p != end; ++p) {
        if (p->score > best->score) {
            best = p;
        }
    }
    std::swap(*cur, *best);
    return (cur++)->move;
}

}