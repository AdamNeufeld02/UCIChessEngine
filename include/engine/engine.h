#pragma once

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <utility>
#include "types.h"
#include "board.h"
#include "move.h"
#include "threads.h"

namespace engine {

class Engine {
public:
    Engine();
    void go(SearchLimits sl);
    void setPosition(std::string fen, std::vector<std::string>& moves);
    void stopSearch();
    void clearSearch();
    void readyUp();

    void calculateLimits(SearchLimits& limits);

    using BestMoveCallback = ThreadPool::BestMoveCallback;
    using InfoCallback     = ThreadPool::InfoCallback;

    void setBestMoveCallback(BestMoveCallback cb) {
        threadPool.setBestMoveCallback(std::move(cb));
    }

    void setInfoCallback(InfoCallback cb) {
        threadPool.setInfoCallback(std::move(cb));
    }

    Move uciStringToMove(std::string move);
    std::string moveToUciString(Move move);
    std::string squareUciString(Square sq);

    void setThreads(size_t n);

private:
    std::unique_ptr<std::deque<State>> states;
    Board board;
    ThreadPool threadPool;
    
};

}