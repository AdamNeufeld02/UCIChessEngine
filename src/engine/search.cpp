#include "engine/search.h"
#include "engine/threads.h"
#include <chrono>

namespace engine {

Worker::Worker(SharedState& st)
    : threads(st.threads)
    , board()
    , bestScore(0)
    , bestDepth(0)
{
    std::fill(std::begin(pv), std::end(pv), Move(0));
}

void Worker::startSearching() {
    int iter = 0;
    while(true) {
        if (threads.stop) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2)); 
        iter++;
        SearchInfo si;
        si.depth = iter;
        threads.fireInfo(si);
    }
    threads.fireBestMove(makeMoveBasic(E2, E4), 100, 2, pv);
}

void Worker::clear() {
    return;
}

}