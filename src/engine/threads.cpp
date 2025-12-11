#include "engine/threads.h"

namespace engine {


Thread::Thread(SharedState& st, size_t n, size_t total)
    : idx(n)
    , nThreads(total)
    , worker(st)
{
    searching = false;
    exit = false;
    th = std::thread(&Thread::idleLoop, this);
}

Thread::~Thread() {
    {
        std::lock_guard<std::mutex> lk(mutex);
        exit = true;
        cv.notify_all();
    }
    if (th.joinable()) {
        th.join();
    }
}

void Thread::idleLoop() {
    std::unique_lock<std::mutex> lk(mutex);

    for (;;) {
        cv.wait(lk, [this] { return searching || exit; });
        if (exit)
            break;
        bool doSearch = searching;
        lk.unlock();

        if (doSearch) {
            worker.startSearching();
        }
        lk.lock();
        searching = false;
    }
}

void Thread::startSearching() {
    std::lock_guard<std::mutex> lk(mutex);
    searching = true;
    cv.notify_one();
}

void Thread::clearWorker() {
    std::lock_guard<std::mutex> lk(mutex);
    worker.clear();
}

ThreadPool::ThreadPool()
{
    stop = false;
}

void ThreadPool::set(size_t n) {
    threads.clear();
    if (n == 0) n = 1;
    threads.reserve(n);
    SharedState shared = {*this};
    for (size_t i = 0; i < n; ++i) {
        threads.push_back(std::make_unique<Thread>(shared, i, n));
    }
}

void ThreadPool::startSearching(Board& board, SearchLimits sl) {
    stop = false;
    for (auto& t : threads) {
        t->startSearching();
    }
}

void ThreadPool::clearThreads() {
    for (auto& t : threads) {
        t->clearWorker();
    }
}

Thread* ThreadPool::getBestThread() {
    if (threads.empty()) return nullptr;
    return threads[0].get();
}

size_t ThreadPool::numThreads() {
    return threads.size();
}

void ThreadPool::fireBestMove(Move m, int score, int depth, Move* pv) {
    if (bestMoveCallback) {
        bestMoveCallback(m, score, depth, pv);
    }
}

void ThreadPool::fireInfo(const SearchInfo& info) {
    if (infoCallback) {
        infoCallback(info);
    }
}
}