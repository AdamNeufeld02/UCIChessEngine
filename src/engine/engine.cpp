#include "engine/engine.h"
#include "engine/movegen.h"

namespace engine {

Engine::Engine() {
    board = Board();
}

void Engine::setPosition(std::string fen, std::vector<std::string>& moves) {
    states = std::make_unique<std::deque<State>>(1);
    board.fenToBoard(fen, &states->back());
    for (const auto& ms : moves) {
        Move m = uciStringToMove(ms);
        if (m == NOMOVE) return;
        states->emplace_back();
        board.makeMove(m, &states->back());
    }  
}

void Engine::setThreads(size_t n) {
    threadPool.set(n);
}

void Engine::go(SearchLimits sl) {
    threadPool.startSearching(board, sl);
}

void Engine::stopSearch() {
    threadPool.stop = true;
}

void Engine::clearSearch() {
    threadPool.clearThreads();
}

Move Engine::uciStringToMove(std::string move) {
    Move moveList[MAXMOVES];
    Move* end;
    end = generate<GEN_LEGAL>(board, moveList);
    int len = end - moveList;

    for (int i = 0; i < len; i++) {
        if (move == moveToUciString(moveList[i])) {
            return moveList[i];
        }
    }

    return NOMOVE;
}

std::string Engine::squareUciString(Square sq) {
    return std::string{char('a' + fileOf(sq)), char('1' + rankOf(sq))};
}

std::string Engine::moveToUciString(Move move) {
    Square from = fromSq(move);
    Square to = toSq(move);

    std::string moveString = squareUciString(from) + squareUciString(to);

    if (moveFlag(move) == PROMOTION) {
        moveString += "**nbrq*"[promoPiece(move)];
    }

    return moveString;
}

}