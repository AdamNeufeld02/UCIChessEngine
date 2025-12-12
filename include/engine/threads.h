#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <functional> 

#include "board.h"
#include "types.h"
#include "search.h"

namespace engine {
class Thread {
public:
    Thread(SharedState& st, size_t n, size_t total);
    ~Thread();

    void idleLoop();
    void startSearching(const Board& board, const SearchLimits sl);
    void clearWorker();

    void waitForSearch();


private:
    std::mutex mutex;
    std::condition_variable cv;
    size_t idx;
    size_t nThreads;
    Worker worker;
    bool searching = true;
    bool exit = false;
    std::thread th;
};




class ThreadPool {
public:
    ThreadPool();
    void startSearching(const Board& board, const SearchLimits sl);
    void clearThreads();
    Thread* getBestThread();
    size_t numThreads();
    void set(size_t nThreads);
    void waitForAllThreads();

    // === Callbacks ===
    using BestMoveCallback = std::function<void(Move best, int score, int depth, Move* pv)>;
    using InfoCallback = std::function<void(const SearchInfo&)>;

    void setBestMoveCallback(BestMoveCallback cb) { bestMoveCallback = std::move(cb); }
    void setInfoCallback(InfoCallback cb)         { infoCallback     = std::move(cb); }

    void fireBestMove(Move best, int score, int depth, Move* pv);
    void fireInfo(const SearchInfo& info);

    std::atomic_bool stop = false;
private:
    std::vector<std::unique_ptr<Thread>> threads;

    BestMoveCallback bestMoveCallback;
    InfoCallback     infoCallback;

};
}