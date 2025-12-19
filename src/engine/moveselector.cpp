#include "engine/moveselector.h"
#include "engine/eval.h"

namespace engine {

MoveSelector::MoveSelector(Move ttm, Board& bd, bool quiets, SearchStack* srchStck, History& hist) :
    ttMove(ttm),
    board(bd),
    allowQuiets(quiets),
    history(hist),
    ss(srchStck)
    {
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
    for (ScoredMove* p = cur; p != endCur; p++) {
        Move m = p->move;
        Square from = fromSq(m);
        Square to   = toSq(m);
        Flags flag  = moveFlag(m);
        PieceType pt = typeOf(board.pieceOn(from));
        if ((Type == GEN_CAPTURES) || board.captureGenType(p->move)) {
            int MVV = 0;
            int captHist = 0;
            if (flag == ENPASSANT) {
                MVV = W.material[PAWN].mg;
                captHist = history.capHist(pt, to, PAWN);
            } else {
                PieceType cap = typeOf(board.pieceOn(to));
                captHist = history.capHist(pt, to, cap);
                MVV = W.material[cap].mg;
            }
            if (Type == GEN_EVASIONS) {
                p->score = MVV_SCALE * MVV + CAPT_HIST_SCALE * captHist + (1 << 15);
            } else {
                p->score = MVV_SCALE * MVV + CAPT_HIST_SCALE * captHist;
            }
           
        } else {
            int mainHist = history.mainHist(board.sideToMove(), pt, from, to);
            int contHist1, contHist2;
            if (ss->ply >= 1  && (ss-1)->current != NOMOVE) {
                contHist1 = history.cont1Hist((ss-1)->movedPT, toSq((ss-1)->current), pt, to);
            } else {
                contHist1 = -100;
            }
            if (ss->ply >= 2  && (ss-2)->current != NOMOVE) {
                contHist2 = history.cont2Hist((ss-2)->movedPT, toSq((ss-2)->current), pt, to);
            } else {
                contHist2 = -100;
            }
            p->score = MAIN_HIST_SCALE * mainHist + CONT_HIST_1_SCALE * contHist1 + CONT_HIST_2_SCALE * contHist2;
        }
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
            cur = badCapEnd = moves;
            endCur = capEnd = generate<GEN_CAPTURES>(board, moves);
            if (ttMove) capEnd = endCur = eraseMove(cur, capEnd, ttMove);
            score<GEN_CAPTURES>();
            stage = GoodCaptures;
            [[fallthrough]];

        case GoodCaptures:
            while (cur != capEnd) {
                Move capt = pickBest(capEnd);
                if (board.seeThreshold(capt, -20)) {
                    return capt;
                }
                std::swap(*badCapEnd++, *(cur-1));
            }

            stage = allowQuiets ? QuietsInit : (badCapEnd != moves) ? BadCaptureinit : Done;
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
            stage = (badCapEnd != moves) ? BadCaptureinit : Done;
            break;

        case BadCaptureinit:
            cur = moves;
            stage = BadCaptures;
            [[fallthrough]];

        case BadCaptures:
            if (cur != badCapEnd)
                return pickBest(badCapEnd);
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