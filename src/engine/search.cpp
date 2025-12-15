#include "engine/search.h"
#include "engine/threads.h"
#include "engine/moveselector.h"
#include "engine/engine.h"


static const int CHECKFREQ = 4095;


namespace engine {

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
        // For now just return information on self search don't consider other threads
        threads.fireBestMove(bestPv[0], bestScore, bestDepth, bestPv);
    }
}

void Worker::clear() {
    std::fill(std::begin(bestPv), std::end(bestPv), NOMOVE);
    history.clear();
}

void Worker::iterativeDeepening() {
    SearchStack ss[MAXPLY];
    Move rootPv[MAXPLY];
    Move pv[MAXPLY];
    ss->pv = rootPv;
    (ss+1)->pv = pv;
    nodesSearched = 0;
    startTime = std::chrono::steady_clock::now();

    for (int i = 0; i < MAXPLY; i++) {
        ss[i].ply = i;
        bestPv[i] = NOMOVE;
    }

    State st;

    Move best;
    Value topScore;
    Value currValue;
    Move move;

    for (int depth = 1; depth < MAXPLY; depth++) {
        best = NOMOVE;
        topScore = -VALUEINFINITE;
        currValue = 0;
        MoveSelector ms = MoveSelector(bestPv[0], rootBoard, true, ss, history);
        
        while ((move = ms.selectMove()) != NOMOVE) {
            if (!rootBoard.legalMove(move)) continue;
            (ss+1)->pv[0] = NOMOVE;
            rootBoard.makeMove(move, &st);
            currValue = -search(ss+1, rootBoard, -VALUEINFINITE, -topScore, depth - 1, true);
            rootBoard.undoMove(move);

            if (threads.stop) return;

            if (currValue > topScore) {
                topScore = currValue;
                best = move;
                updatePV(ss->pv, move, (ss+1)->pv);
            }
        }
        
        for (int i = 0; ss->pv[i] != NOMOVE && i < MAXPLY; i++) {
            bestPv[i] = ss->pv[i];
        }
        bestScore = topScore;
        bestDepth = depth;

        auto now = std::chrono::steady_clock::now();
        int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();

        if (elapsedMs <= 0) elapsedMs = 1;
        uint64_t nps = nodesSearched * 1000ULL / static_cast<uint64_t>(elapsedMs);

        SearchInfo si;
        si.depth = bestDepth;
        si.score = bestScore;
        si.pv = bestPv;
        si.nodes = nodesSearched;
        si.nps = nps;

        if (threadID == 0) {
            threads.fireInfo(si);
        }

        if (limits.depth > 0 && limits.depth <= depth) return;

        if (threadID != 0 && checkSoftLimit(elapsedMs)) return;
    }
}

int Worker::search(SearchStack* ss, Board& board, int alpha, int beta, int depth, bool pvNode) {
    nodesSearched++;

    if (((nodesSearched & CHECKFREQ) == 0) && (threadID == 0) && checkMainWorker()) {
        threads.stop = true;
        return 0;
    }

    if (board.isDraw() || board.isRepetitionDraw()) return VALUEDRAW;

    if (depth <= 0) return qsearch(ss, board, alpha, beta);

    // Probe transposition-table
    Move ttmove = NOMOVE;
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
    }
    Move pv[MAXPLY];
    Value topScore = -VALUEINFINITE;
    Value currValue = 0;
    (ss+1)->pv = pv;
    int movesSearched = 0;
    MoveSelector ms = MoveSelector(ttmove, board, true, ss, history);
    SearchedMoves<SEARCHEDLISTCAP> searchedQuiets;
    SearchedMoves<SEARCHEDLISTCAP> searchedCaptures;
    State st;
    Move move;
    Move topMove = NOMOVE;
    int alphaOrig = alpha;
    bool unproven = false;

    while ((move = ms.selectMove()) != NOMOVE) {
        if (!board.legalMove(move)) continue;
        ss->current = move;
        ss->movedPT = typeOf(board.pieceOn(fromSq(move)));
        (ss+1)->pv[0] = NOMOVE;
        board.makeMove(move, &st);
        if (movesSearched == 0) {
            currValue= -search(ss+1, board, -beta, -alpha, depth-1, pvNode);
        } else {
            currValue = -search(ss+1, board, -(alpha+1), -alpha, depth-1, false);
            if (currValue > alpha && currValue < beta && !pvNode) {
                unproven = true;
            }
            if (pvNode && currValue > alpha && currValue < beta) {
                currValue = -search(ss+1, board, -beta, -alpha, depth-1, true);
            }
        }
        board.undoMove(move);

        if (threads.stop) return 0;

        if (movesSearched < SEARCHEDLISTCAP) {
            if (board.captureGenType(move)) {
                searchedCaptures.pushBack(move);
            } else {
                searchedQuiets.pushBack(move);
            }
        }

        // fail high
        if (currValue >= beta) {
            ttWriter.write(board.key(), move, NOVALUE, toTTScore(currValue, ss->ply), depth, tt.currentGeneration(), LOWER);
            updateHistories(board, ss, searchedCaptures, searchedQuiets, move, depth + QSEARCHHISTORYDEPTH);
            return currValue;
        }

        if (currValue > topScore) {
            topScore = currValue;
            topMove = move;
            if (topScore > alpha) {
                alpha = topScore;
                updatePV(ss->pv, move, (ss+1)->pv);
            }
        }
        movesSearched++;
    }
    
    if (movesSearched==0) {
        if (board.checkers()) {
            return -VALUEINFINITE + ss->ply;
        } else {
            return VALUEDRAW;
        }
    } 
    Bound b = (topScore <= alphaOrig) ? UPPER : unproven? LOWER : EXACT;
    ttWriter.write(board.key(), topMove, NOVALUE, toTTScore(topScore, ss->ply), depth, tt.currentGeneration(), b);
    updateHistories(board, ss, searchedCaptures, searchedQuiets, topMove, depth + QSEARCHHISTORYDEPTH);
    return topScore;
}

int Worker::qsearch(SearchStack* ss, Board& board, int alpha, int beta) {
    nodesSearched++;

    if (((nodesSearched & CHECKFREQ) == 0) && (threadID == 0) && checkMainWorker()) {
        threads.stop = true;
        return 0;
    }

    if (board.isDraw() || board.isRepetitionDraw() || (ss->ply >= MAXPLY)) return VALUEDRAW;

    Value topScore = -VALUEINFINITE;
    
    if (!board.checkers()) {
        Value standPat = evaluate(board);
        if (standPat >= beta) {
            return standPat;
        }

        if (standPat > alpha) {
            alpha = standPat;
        }
        topScore = standPat;
    }  

    Move ttmove = NOMOVE;
    auto [ttHit, ttData, ttWriter] = tt.probe(board.key());
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
    MoveSelector ms = MoveSelector(ttmove, board, false, ss, history);
    SearchedMoves<SEARCHEDLISTCAP> searchedQuiets;
    SearchedMoves<SEARCHEDLISTCAP> searchedCaptures;
    State st;
    Move move;
    Move topMove = NOMOVE;
    bool raisedAlpha = false;

    
    while ((move = ms.selectMove()) != NOMOVE) {

        if ((!isLoss(topScore) && !board.isCapture(move)) || !board.legalMove(move)) continue;
        
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

    if (movesSearched == 0 && board.checkers()) return -VALUEINFINITE + ss->ply;

    Bound b = raisedAlpha ? EXACT : UPPER;
    ttWriter.write(board.key(), topMove, NOVALUE, toTTScore(topScore, ss->ply), 0, tt.currentGeneration(), b);
    updateHistories(board, ss, searchedCaptures, searchedQuiets, topMove, QSEARCHHISTORYDEPTH);
    return topScore;
}

    void Worker::updateHistories(Board& board, SearchStack* ss, SearchedMoves<SEARCHEDLISTCAP>& searchedCaptures, SearchedMoves<SEARCHEDLISTCAP>& searchedQuiets, Move bestMove, int depth) {
        int bonus = depth * depth;
        int malus = bonus / 4;

        if (!board.captureGenType(bestMove)) {
            updateMainHistory(board, bestMove, bonus);
            updateContHistory(board, ss, bestMove, bonus);

            for (Move m : searchedQuiets) {
                updateMainHistory(board, m, -malus);
                updateContHistory(board, ss, m, -malus);
            }
        } else {
            updateCaptureHistory(board, bestMove, bonus);
        }

        for (Move m : searchedCaptures) {
            updateCaptureHistory(board, m, -malus);
        }
    }

    void Worker::updateMainHistory(Board& board, Move move, int reward) {
        Colour us = board.sideToMove();
        history.mainHist(board.sideToMove(), typeOf(board.pieceOn(fromSq(move))), fromSq(move), toSq(move)) += reward;
    }

    void Worker::updateCaptureHistory(Board& board, Move move, int reward) {
        history.capHist(typeOf(board.pieceOn(fromSq(move))), toSq(move), typeOf(board.pieceOn(toSq(move)))) += reward;
    }

    void Worker::updateContHistory(Board& board, SearchStack* ss, Move move, int reward) {
        if (ss->ply >= 1) {
            history.cont1Hist((ss-1)->movedPT, toSq((ss-1)->current), typeOf(board.pieceOn(fromSq(move))), toSq(move)) += reward;
        }

        if (ss->ply >= 2) {
            history.cont2Hist((ss-2)->movedPT, toSq((ss-2)->current), typeOf(board.pieceOn(fromSq(move))), toSq(move)) += reward; 
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