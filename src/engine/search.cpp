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
{
    std::fill(std::begin(bestPv), std::end(bestPv), NOMOVE);
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
    return;
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
        MoveSelector ms = MoveSelector(bestPv[0], rootBoard, true);
        
        while ((move = ms.selectMove()) != NOMOVE) {
            if (!rootBoard.legalMove(move)) continue;
            (ss+1)->pv[0] = NOMOVE;
            rootBoard.makeMove(move, &st);
            currValue = -search(ss+1, rootBoard, -VALUEINFINITE, -topScore, depth - 1);
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

int Worker::search(SearchStack* ss, Board& board, int alpha, int beta, int depth) {
    nodesSearched++;

    if (((nodesSearched & CHECKFREQ) == 0) && (threadID == 0) && checkMainWorker()) {
        threads.stop = true;
        return 0;
    }

    Move pv[MAXPLY];
    Value topScore = -VALUEINFINITE;
    Value currValue = 0;

    (ss+1)->pv = pv;
    int movesSearched = 0;

    if (board.isDraw() || board.isRepetitionDraw()) return VALUEDRAW;

    if (depth <= 0) return qsearch(ss, board, alpha, beta);

    MoveSelector ms = MoveSelector(NOMOVE, board, true);
    State st;
    Move move;

    while ((move = ms.selectMove()) != NOMOVE) {
        if (!board.legalMove(move)) continue;
        (ss+1)->pv[0] = NOMOVE;
        board.makeMove(move, &st);
        currValue = -search(ss+1, board, -beta, -alpha, depth - 1);
        board.undoMove(move);

        if (threads.stop) return 0;

        if (currValue >= beta) {
            return currValue;
        }

        if (currValue > topScore) {
            topScore = currValue;
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

    Value currValue = 0;
    int movesSearched = 0;
    MoveSelector ms = MoveSelector(NOMOVE, board, false);
    State st;
    Move move;

    
    while ((move = ms.selectMove()) != NOMOVE) {

        if ((!isLoss(topScore) && !board.isCapture(move)) || !board.legalMove(move)) continue;
        
        movesSearched++;
   
        board.makeMove(move, &st);
        currValue = -qsearch(ss+1, board, -beta, -alpha);
        board.undoMove(move);

        if (threads.stop) return 0;

        if (currValue >= beta) {            
            return currValue;
        }

        if (currValue >= topScore) {
            topScore = currValue;
            if (topScore >= alpha) {
                alpha = topScore;
            }
        }
        
    }

    if (movesSearched == 0 && board.checkers()) return -VALUEINFINITE + ss->ply;

    return topScore;
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