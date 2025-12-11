#pragma once
#include "board.h"
#include <vector>


namespace engine {

class ThreadPool;

struct SharedState {
    ThreadPool& threads;
    // Eventually add a transposition table reference
};

struct SearchInfo {
    int depth      = 0;
    int seldepth   = 0;
    int score      = 0;
    bool isMate    = false;
    std::uint64_t nodes   = 0;
    std::uint64_t timeMs  = 0;
    std::uint64_t nps     = 0;
    std::vector<Move> pv;
};

class Worker {
public:
    Worker(SharedState& st);

    void startSearching();
    void clear();


private:
    ThreadPool& threads;
    Board board;

    Move pv[MAXDEPTH];
    int bestScore;
    int bestDepth;
};

}