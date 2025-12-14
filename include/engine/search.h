#pragma once
#include "board.h"
#include "transpostable.h"
#include <vector>
#include <chrono>

namespace engine {

class ThreadPool;

struct SharedState {
    ThreadPool& threads;
    TranspositionTable& tt;
};

struct SearchInfo {
    int depth      = 0;
    int seldepth   = 0;
    int score      = 0;
    bool isMate    = false;
    std::uint64_t nodes   = 0;
    std::uint64_t timeMs  = 0;
    std::uint64_t nps     = 0;
    Move* pv;
};

struct SearchStack {
    int ply;
    Move* pv;
    Move current;
    bool didNull;
};

class Worker {
public:
    Worker(SharedState& st, size_t idx);

    void startSearching();
    void setRoot(const Board& root, const SearchLimits& l);
    void clear();

    void iterativeDeepening();
    Value search(SearchStack* ss, Board& board, Value alpha, Value beta, int depth);
    Value qsearch(SearchStack* ss, Board& board, Value alpha, Value beta);


private:

    void updatePV(Move* pv, Move move, Move* childPv);

    bool checkSoftLimit(int64_t elapsedMs);
    bool checkHardLimit(int64_t elapsedMs);
    bool checkLastManStanding();
    bool checkMainWorker();

    ThreadPool& threads;
    TranspositionTable& tt;
    Board rootBoard;
    SearchLimits limits;
    size_t threadID;
    Move bestPv[MAXPLY];
    Value bestScore;
    std::chrono::steady_clock::time_point startTime;
    int bestDepth;
    int nodesSearched;
};

}