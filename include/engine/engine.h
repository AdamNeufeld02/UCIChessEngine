#pragma once

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include "types.h"
#include "board.h"
#include "move.h"

namespace engine {

class Engine {
public:
    Engine();
    void go(SearchLimits sl);
    void setPosition(std::string fen, std::vector<std::string>& moves);
    void stopSearch();
    void clearSearch();

private:
    std::unique_ptr<std::deque<State>> states;
    Board board;
    Move uciStringToMove(std::string move);
    std::string moveToUciString(Move move);
    std::string squareUciString(Square sq);
};

}