#include "engine/search.h"
#include "engine/threads.h"
#include "engine/moveselector.h"
#include "engine/engine.h"
#include "engine/historystats.h"

static const int CHECKFREQ = 4095;
constexpr int HIST_CAP = 8192;
constexpr int ETA_SHIFT = 3;

static inline int clampHist(int x) {
    if (x >  HIST_CAP) return  HIST_CAP;
    if (x < -HIST_CAP) return -HIST_CAP;
    return x;
}

static inline void updateToward(int& h, int target) {
    target = clampHist(target);
    h += (target - h) >> ETA_SHIFT;
}

static inline int winTarget(int depth) {
    return std::min(HIST_CAP, depth * depth * 8);
}

static inline int loseTarget(int depth) {
    return -winTarget(depth) / 8;
}


namespace engine {

static inline int quietHistoryScore(SearchStack* ss, Worker& w, const Board& b, Move m) {
    Square from = fromSq(m);
    Square to   = toSq(m);
    Piece pc    = b.pieceOn(from);
    PieceType pt = typeOf(pc);
    int contHistScore = 0;
    if (ss->ply >= 1 && (ss-1)->current != NOMOVE) {
        contHistScore += w.history.cont1Hist((ss-1)->movedPT, toSq((ss-1)->current), pt, to);
    }
    if (ss->ply >= 2 && (ss-2)->current != NOMOVE) {
        contHistScore += w.history.cont2Hist((ss-2)->movedPT, toSq((ss-2)->current), pt, to);
    }
    return 2 * (int)w.history.mainHist(b.sideToMove(), pt, from, to) + contHistScore;
}

static inline int captureHistoryScore(Worker& w, const Board& b, Move m) {
    Square from = fromSq(m);
    Square to   = toSq(m);

    Piece attacker = b.pieceOn(from);
    Piece victim   = b.pieceOn(to);
    PieceType at = typeOf(attacker);
    PieceType vt = typeOf(victim);

    return (int)w.history.capHist(at, to, vt);
}

Worker::Worker(SharedState& st, size_t idx)
    : threads(st.threads)
    , rootBoard()
    , bestScore(0)
    , bestDepth(0)
    , threadID(idx)
    , tt(st.tt)
{
    clear();
}

void Worker::setRoot(const Board& root, const SearchLimits& l) {
    rootBoard   = root;
    limits  = l;
}

void Worker::startSearching() {
    threads.incrementActive();
    iterativeDeepening();
    threads.decrementActive();
    if (threadID == 0) {
        threads.waitForOthers(threadID);
        Move best = threads.voteBestMove();
        threads.fireBestMove(best, bestScore, bestDepth, bestPv);
        HistoryDistLogger::instance().flushJsonlAppend(
            "history_score_distributions.jsonl",
            "dev",
            threads.numThreads(),
            nodesSearched,
            bestDepth
        );
    }
}

void Worker::clear() {
    std::fill(std::begin(bestPv), std::end(bestPv), NOMOVE);
    history.clear();
    for (int i = 0; i < MAXPLY; i++) {
        killerMoves[i][0] = NOMOVE;
        killerMoves[i][1] = NOMOVE;
    }
}

void Worker::iterativeDeepening() {
    nodesSearched = 0;
    startTime = std::chrono::steady_clock::now();
    selDepth = 0;

    SearchStack ss[MAXPLY];
    Move rootPv[MAXPLY];
    ss->pv = rootPv;
    for (int i = 0; i < MAXPLY; i++) {
        ss[i].ply = i;
        bestPv[i] = NOMOVE;
    }

    Value alpha = NOVALUE;

    for (int depth = 1; depth < MAXPLY; depth++) {

        alpha = aspirationSearch(ss, rootBoard, alpha, depth);

        if (threads.stop) return;

        for (int i = 0; ss->pv[i] != NOMOVE && i < MAXPLY; i++) {
            bestPv[i] = ss->pv[i];
        }

        bestScore = alpha;
        bestDepth = depth;

        auto now = std::chrono::steady_clock::now();
        int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();

        if (elapsedMs <= 0) elapsedMs = 1;
        uint64_t nps = nodesSearched * 1000ULL / static_cast<uint64_t>(elapsedMs);

        SearchInfo si;
        si.depth = bestDepth;
        si.seldepth = selDepth;
        si.pv = bestPv;
        si.nodes = nodesSearched;
        si.nps = nps;
        si.timeMs = elapsedMs;
        si.isMate = std::abs(bestScore) >= VALUEMATEINMAX;
        if (si.isMate) {
            if (bestScore > 0) {
                si.score = (VALUEMATE - bestScore + 1) / 2;
            } else {
                si.score = (VALUEMATE + bestScore + 1) / 2;
            }

        } else {
            si.score = bestScore;
        }
        

        if (threadID == 0) {
            threads.fireInfo(si);
        }

        if (limits.depth > 0 && limits.depth <= depth) return;

        if (threadID != 0 && checkSoftLimit(elapsedMs)) return;
    }
}

Value Worker::aspirationSearch(SearchStack* ss, Board& board, Value prevScore, int depth) {
    if (std::abs(prevScore) >= VALUEMATEINMAX) {
        return rootSearch(ss, board, -VALUEINFINITE, VALUEINFINITE, depth);
    }

    Value delta = 30;
    Value alpha = prevScore - delta;
    Value beta  = prevScore + delta;

    alpha = std::max(alpha, -VALUEINFINITE);
    beta  = std::min(beta,  VALUEINFINITE);

    while (true) {
        Value val = rootSearch(ss, board, alpha, beta, depth);
        if (threads.stop)
            return 0;

        if (val <= alpha) {
            alpha = std::max(val - delta, -VALUEINFINITE);
        }
        else if (val >= beta) {
            beta = std::min(val + delta, VALUEINFINITE);
        }
        else {
            return val;
        }

        delta *= 2;
    }
}

Value Worker::rootSearch(SearchStack* ss, Board& board, Value alpha, Value beta, int depth) {
    Move pv[MAXPLY];
    (ss+1)->pv = pv;

    State st;
    Move move;
    Value currValue = 0;
    Value topScore = -VALUEINFINITE;
    int movesSearched = 0;

    MoveSelector ms = MoveSelector(bestPv[0], board, true, ss, history, killerMoves[ss->ply]);
    
    while ((move = ms.selectMove()) != NOMOVE) {
        if (!board.legalMove(move)) continue;
        ss->current = move;
        ss->movedPT = typeOf(board.pieceOn(fromSq(move)));
        
        board.makeMove(move, &st);
        if (movesSearched == 0) {
            (ss+1)->pv[0] = NOMOVE;
            currValue= -search(ss+1, board, -beta, -alpha, depth-1, true);
        } else {
            (ss+1)->pv[0] = NOMOVE;
            currValue = -search(ss+1, board, -(alpha+1), -alpha, depth-1, false);
            if (currValue > alpha && currValue < beta) {
                (ss+1)->pv[0] = NOMOVE;
                currValue = -search(ss+1, board, -beta, -alpha, depth-1, true);
            }
        }
        board.undoMove(move);

        if (threads.stop) return 0;

        if (currValue >= beta) {
            return currValue;
        }

        if (currValue > topScore) {
            topScore = currValue;
            if (currValue > alpha) {
                alpha = currValue;
                updatePV(ss->pv, move, (ss+1)->pv);
            }
        }
        movesSearched++;
    }
    return topScore;
}

Value Worker::search(SearchStack* ss, Board& board, int alpha, int beta, int depth, bool pvNode) {
    nodesSearched++;

    if (((nodesSearched & CHECKFREQ) == 0) && (threadID == 0) && checkMainWorker()) {
        threads.stop = true;
        return 0;
    }

    if (board.isDraw() || board.isRepetitionDraw()) return VALUEDRAW;

    if (depth <= 0) return qsearch(ss, board, alpha, beta);

    // Probe transposition-table
    Move ttmove = NOMOVE;
    Value staticEval = NOVALUE;
    auto [ttHit, ttData, ttWriter] = tt.probe(board.key());
    if (ttHit) {
        
        if (ttData.depth >= depth) {
            Value ttValue = fromTTScore(ttData.value, ss->ply);
            if (ttData.bound == EXACT) {
                return ttValue;
            } else if (ttData.bound == LOWER && ttValue >= beta) {
                return ttValue;
            } else if (ttData.bound == UPPER && ttValue <= alpha) {
                return ttValue;
            }
        }
        ttmove = ttData.move;
        staticEval = ttData.eval;
    }

    if (staticEval == NOVALUE) {
        staticEval = evaluate(board);
    }

    // Reverse Futility Pruning
    if (!pvNode && !board.checkers() && depth <= 7) {
        Value margin = rfpMargin(depth);
        if (staticEval - margin >= beta) {
            return staticEval;
        }
    }

    // Razoring
    if (!pvNode && !board.checkers() && depth <= 2) {
        Value margin = razorMargin(depth);
        if (staticEval + margin <= alpha) {
            return qsearch(ss, board, alpha, beta);
        }
    }

    State st;

    if (!pvNode && ((ss-1)->current != NOMOVE) && staticEval >= beta && !board.checkers() && board.nonPawnMaterial(board.sideToMove()) && depth >= 3){
        ss->current = NOMOVE;
        int R = 2 + depth/4;
        board.makeNullMove(&st);
        Value nullValue = -search(ss+1, board, -beta, -beta+1, depth-R, false);
        board.undoNullMove();
        if (threads.stop) return 0;
        if (nullValue >= beta) {
            if (nullValue >= VALUEMATEINMAX || nullValue <= -VALUEMATEINMAX) {
                return beta;
            } else {
                return nullValue;
            }
        }
    }

    Move pv[MAXPLY];
    Value topScore = -VALUEINFINITE;
    Value currValue = 0;
    (ss+1)->pv = pv;
    int movesSearched = 0;
    MoveSelector ms = MoveSelector(ttmove, board, true, ss, history, killerMoves[ss->ply]);
    SearchedMoves<SEARCHEDLISTCAP> searchedQuiets;
    SearchedMoves<SEARCHEDLISTCAP> searchedCaptures;
    Move move;
    Move topMove = NOMOVE;

    while ((move = ms.selectMove()) != NOMOVE) {
        if (!board.legalMove(move)) continue;

        bool isQuiet = !board.captureGenType(move);
        bool givesCheck = board.givesCheck(move);

        // Futility pruning
        if (!pvNode && !board.checkers() && depth <= 2 && isQuiet && !givesCheck && movesSearched > 1) {
            Value margin = futilityMargin(depth);
            if (staticEval + margin <= alpha) {
                continue;
            }
        }
        int historyScore = isQuiet ? quietHistoryScore(ss, *this, board, move) : captureHistoryScore(*this, board, move);
        ss->current = move;
        ss->movedPT = typeOf(board.pieceOn(fromSq(move)));
        bool doLMR = !board.checkers() && depth > 2 && movesSearched > 3 && !givesCheck && isQuiet; 
        board.makeMove(move, &st); 
        if (movesSearched == 0) {
            (ss+1)->pv[0] = NOMOVE;
            currValue= -search(ss+1, board, -beta, -alpha, depth-1, pvNode);
        } else {
            (ss+1)->pv[0] = NOMOVE;
            int r = doLMR ? calcReduction(depth, movesSearched, pvNode) : 0;
            currValue = -search(ss+1, board, -(alpha+1), -alpha, depth-1-r, false);

            if (!pvNode && currValue > alpha && r != 0) {
                (ss+1)->pv[0] = NOMOVE;
                currValue = -search(ss+1, board, -(alpha+1), -alpha, depth-1, false);
            }

            if (pvNode && currValue > alpha) {
                (ss+1)->pv[0] = NOMOVE;
                currValue = -search(ss+1, board, -beta, -alpha, depth-1, true);
            }
        }
        board.undoMove(move);

        if (threads.stop) return 0;

        // fail high
        if (currValue >= beta) {
            ttWriter.write(board.key(), move, staticEval, toTTScore(currValue, ss->ply), depth, tt.currentGeneration(), LOWER);
            updateHistories(board, ss, searchedCaptures, searchedQuiets, move, depth + QSEARCHHISTORYDEPTH);
            if (isQuiet && killerMoves[ss->ply][0] != move) {
                killerMoves[ss->ply][1] = killerMoves[ss->ply][0];
                killerMoves[ss->ply][0] = move;
            }
            if (isQuiet) {
                HistoryDistLogger::instance().logQuiet(HistOutcome::FailHigh, historyScore, depth);
            } else {
                HistoryDistLogger::instance().logCapture(HistOutcome::FailHigh, historyScore, depth);
            }
            return currValue;
        }

        if (currValue > topScore) {
            topScore = currValue;
            topMove = move;
            if (topScore > alpha) {
                alpha = topScore;
                updatePV(ss->pv, move, (ss+1)->pv);
                if (isQuiet) {
                    HistoryDistLogger::instance().logQuiet(HistOutcome::ImproveAlpha, historyScore, depth);
                } else {
                    HistoryDistLogger::instance().logCapture(HistOutcome::ImproveAlpha, historyScore, depth);
                }
            } else {
                if (isQuiet) {
                    HistoryDistLogger::instance().logQuiet(HistOutcome::FailLow, historyScore, depth);
                } else {
                    HistoryDistLogger::instance().logCapture(HistOutcome::FailLow, historyScore, depth);
                }
            }
        } else if (movesSearched < SEARCHEDLISTCAP) {
            if (!isQuiet) {
                searchedCaptures.pushBack(move);
            } else {
                searchedQuiets.pushBack(move);
            }
        }
        movesSearched++;
    }
    
    if (movesSearched==0) {
        if (board.checkers()) {
            return -VALUEMATE + ss->ply;
        } else {
            return VALUEDRAW;
        }
    } 
    Bound b = pvNode && topMove != NOMOVE ? EXACT : UPPER;
    ttWriter.write(board.key(), topMove, staticEval, toTTScore(topScore, ss->ply), depth, tt.currentGeneration(), b);
    updateHistories(board, ss, searchedCaptures, searchedQuiets, topMove, depth + QSEARCHHISTORYDEPTH);
    return topScore;
}

Value Worker::qsearch(SearchStack* ss, Board& board, int alpha, int beta) {
    nodesSearched++;
    if (ss->ply > selDepth) selDepth = ss->ply;

    if (((nodesSearched & CHECKFREQ) == 0) && (threadID == 0) && checkMainWorker()) {
        threads.stop = true;
        return 0;
    }

    if (board.isDraw() || board.isRepetitionDraw() || (ss->ply >= MAXPLY)) return VALUEDRAW;

    Value topScore = -VALUEINFINITE;
    
    Move ttmove = NOMOVE;
    auto [ttHit, ttData, ttWriter] = tt.probe(board.key());
    Value standPat = NOVALUE;

    if (ttHit) {
        standPat = ttData.eval;
    }
    if (standPat == NOVALUE) {
        standPat = evaluate(board);
    }

    if (!board.checkers()) {
        if (standPat >= beta) {
            return standPat;
        }

        if (standPat > alpha) {
            alpha = standPat;
        }
        topScore = standPat;
    }  

    if (ttHit) {
        if (ttData.depth >= 0) {
            Value ttValue = fromTTScore(ttData.value, ss->ply);
            if (ttData.bound == EXACT) {
                return ttValue;
            } else if (ttData.bound == LOWER && ttValue >= beta) {
                return ttValue;
            } else if (ttData.bound == UPPER && ttValue <= alpha) {
                return ttValue;
            }
        }
        ttmove = ttData.move;
    }

    Value currValue = 0;
    int movesSearched = 0;
    MoveSelector ms = MoveSelector(ttmove, board, false, ss, history, killerMoves[ss->ply]);
    SearchedMoves<SEARCHEDLISTCAP> searchedQuiets;
    SearchedMoves<SEARCHEDLISTCAP> searchedCaptures;
    State st;
    Move move;
    Move topMove = NOMOVE;
    bool raisedAlpha = false;

    
    while ((move = ms.selectMove()) != NOMOVE) {

        if (!board.legalMove(move)) continue;

        // pruning
        if (!board.checkers()) {
            if (board.captureGenType(move)) {
                Value captVal = moveFlag(move) == ENPASSANT ? PAWN : W.material[typeOf(board.pieceOn(toSq(move)))].mg;
                Value promVal = moveFlag(move) == PROMOTION ? W.material[promoPiece(move)].mg - W.material[PAWN].mg : 0;

                // delta pruning
                if (standPat + captVal + promVal + 164 <= alpha) continue;

                // SEE pruning
                if (!board.seeThreshold(move, -20)) continue;
            }
        }
        
        movesSearched++;
        ss->current = move;
        ss->movedPT = typeOf(board.pieceOn(fromSq(move)));
        board.makeMove(move, &st);
        currValue = -qsearch(ss+1, board, -beta, -alpha);
        board.undoMove(move);

        if (threads.stop) return 0;

        if (movesSearched < SEARCHEDLISTCAP) {
            if (board.captureGenType(move)) {
                searchedCaptures.pushBack(move);
            } else {
                searchedQuiets.pushBack(move);
            }
        }

        if (currValue >= beta) {      
            ttWriter.write(board.key(), move, NOVALUE, toTTScore(currValue, ss->ply), 0, tt.currentGeneration(), LOWER);      
            updateHistories(board, ss, searchedCaptures, searchedQuiets, move, QSEARCHHISTORYDEPTH);
            return currValue;
        }

        if (currValue >= topScore) {
            topScore = currValue;
            topMove = move;
            if (topScore >= alpha) {
                raisedAlpha = true;
                alpha = topScore;
            }
        }
        
    }

    if (movesSearched == 0 && board.checkers()) return -VALUEMATE + ss->ply;

    Bound b = raisedAlpha ? EXACT : UPPER;
    ttWriter.write(board.key(), topMove, NOVALUE, toTTScore(topScore, ss->ply), 0, tt.currentGeneration(), b);
    updateHistories(board, ss, searchedCaptures, searchedQuiets, topMove, QSEARCHHISTORYDEPTH);
    return topScore;
}

int Worker::calcReduction(int depth, int moveCount, bool pvNode) {
    if (depth < 3 || moveCount < 4) return 0;
    int r = 1;
    if (depth >= 8) r++;
    if (depth >= 12) r++;
    if (moveCount >= 8) r++;
    if (moveCount >= 16) r++;
    if (pvNode) r--;
    if (r < 0) r = 0;
    return r;
}

inline Value Worker::rfpMargin(int depth) {
    return Value(80 + 50 * depth);
}

inline Value Worker::razorMargin(int depth) {
    return Value(200 + 150 * depth);
}

inline Value Worker::futilityMargin(int depth) {
    return Value(60 + 40 * depth + 30 * depth * depth);
}

void Worker::updateHistories(Board& board, SearchStack* ss, SearchedMoves<SEARCHEDLISTCAP>& searchedCaptures, SearchedMoves<SEARCHEDLISTCAP>& searchedQuiets, Move bestMove, int depth) {
    int wt = winTarget(depth);
    int lt = loseTarget(depth);

    if (!board.captureGenType(bestMove)) {
        updateMainHistory(board, bestMove, wt);
        updateContHistory(board, ss, bestMove, wt);

        for (Move m : searchedQuiets) {
            updateMainHistory(board, m, lt);
            updateContHistory(board, ss, m, lt);
        }
    } else {
        updateCaptureHistory(board, bestMove, wt);
    }

    for (Move m : searchedCaptures) {
        updateCaptureHistory(board, m, lt);
    }
}

void Worker::updateMainHistory(Board& board, Move move, int target) {
    updateToward(history.mainHist(board.sideToMove(), typeOf(board.pieceOn(fromSq(move))), fromSq(move), toSq(move)), target);
}

void Worker::updateCaptureHistory(Board& board, Move move, int target) {
    updateToward(history.capHist(typeOf(board.pieceOn(fromSq(move))), toSq(move), typeOf(board.pieceOn(toSq(move)))), target);
}

void Worker::updateContHistory(Board& board, SearchStack* ss, Move move, int target) {
    if (ss->ply >= 1 && (ss-1)->current != NOMOVE) {
        updateToward(history.cont1Hist((ss-1)->movedPT, toSq((ss-1)->current), typeOf(board.pieceOn(fromSq(move))), toSq(move)), target);
    }

    if (ss->ply >= 2 && (ss-2)->current != NOMOVE) {
        updateToward(history.cont2Hist((ss-2)->movedPT, toSq((ss-2)->current), typeOf(board.pieceOn(fromSq(move))), toSq(move)), target); 
    }

    if (ss->ply >= 3 && (ss-3)->current != NOMOVE) {
        updateToward(history.cont3Hist((ss-3)->movedPT, toSq((ss-3)->current), typeOf(board.pieceOn(fromSq(move))), toSq(move)), target); 
    }

    if (ss->ply >= 4 && (ss-4)->current != NOMOVE) {
        updateToward(history.cont3Hist((ss-4)->movedPT, toSq((ss-4)->current), typeOf(board.pieceOn(fromSq(move))), toSq(move)), target); 
    }
}


void Worker::updatePV(Move* pv, Move move, Move* childPv) {
    for (*pv++ = move; childPv && *childPv != NOMOVE; )
        *pv++ = *childPv++;
    *pv = NOMOVE;
}

bool Worker::checkSoftLimit(int64_t elapsedMs) {
    return limits.softTimeLimitMs && (elapsedMs > limits.softTimeLimitMs);
}

bool Worker::checkHardLimit(int64_t elapsedMs) {
    return limits.hardTimeLimitMs && (elapsedMs > limits.hardTimeLimitMs);
}

bool Worker::checkLastManStanding() {
    return (threads.numThreads() > 1) && (threads.activeSearchers() == 1);
}

bool Worker::checkMainWorker() {
    auto now = std::chrono::steady_clock::now();
    int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    return checkHardLimit(elapsedMs) || (checkLastManStanding() && checkSoftLimit(elapsedMs));
}

}