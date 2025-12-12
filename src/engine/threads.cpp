#include "engine/threads.h"

namespace engine {


Thread::Thread(SharedState& st, size_t n, size_t total)
    : idx(n)
    , nThreads(total)
    , worker(st, n)
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
        cv.notify_all();
    }
}

void Thread::startSearching(const Board& board, const SearchLimits sl) {
    std::lock_guard<std::mutex> lk(mutex);
    worker.setRoot(board, sl);
    searching = true;
    cv.notify_one();
}

void Thread::waitForSearch() {
    std::unique_lock<std::mutex> lk(mutex);
    cv.wait(lk, [&]{ return !searching; });
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

void ThreadPool::startSearching(const Board& board, const SearchLimits sl) {
    stop = false;
    for (auto& t : threads) {
        t->startSearching(board, sl);
    }
}

void ThreadPool::waitForAllThreads() {
    for (auto& t : threads) {
        t->waitForSearch();
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