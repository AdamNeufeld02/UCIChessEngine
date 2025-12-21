#pragma once
#include <array>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>

namespace engine {

enum class HistOutcome : uint8_t {
    FailHigh = 0,
    ImproveAlpha = 1,
    FailLow = 2
};

template<int MAX_ABS, int STEP>
struct FixedHistogram {
    static_assert(MAX_ABS > 0 && STEP > 0);
    static constexpr int MINV  = -MAX_ABS;
    static constexpr int MAXV  = +MAX_ABS;
    static constexpr int NBINS = (2 * MAX_ABS) / STEP + 1;

    std::array<uint64_t, NBINS> bins{};
    uint64_t total = 0;

    void add(int v) {
        if (v < MINV) v = MINV;
        if (v > MAXV) v = MAXV;
        int idx = (v - MINV) / STEP;
        if (idx < 0) idx = 0;
        if (idx >= NBINS) idx = NBINS - 1;
        bins[(size_t)idx]++;
        total++;
    }

    void mergeFrom(const FixedHistogram& o) {
        for (int i = 0; i < NBINS; i++) bins[(size_t)i] += o.bins[(size_t)i];
        total += o.total;
    }
};

struct HistoryDistSnapshot {
    // Quiet (main) history distributions
    FixedHistogram<8192, 16> quiet_failHigh;
    FixedHistogram<8192, 16> quiet_improveAlpha;
    FixedHistogram<8192, 16> quiet_failLow;

    // Capture history distributions
    FixedHistogram<8192, 16> capt_failHigh;
    FixedHistogram<8192, 16> capt_improveAlpha;
    FixedHistogram<8192, 16> capt_failLow;

    void mergeFrom(const HistoryDistSnapshot& o) {
        quiet_failHigh.mergeFrom(o.quiet_failHigh);
        quiet_improveAlpha.mergeFrom(o.quiet_improveAlpha);
        quiet_failLow.mergeFrom(o.quiet_failLow);

        capt_failHigh.mergeFrom(o.capt_failHigh);
        capt_improveAlpha.mergeFrom(o.capt_improveAlpha);
        capt_failLow.mergeFrom(o.capt_failLow);
    }

    bool empty() const {
        return quiet_failHigh.total == 0 && quiet_improveAlpha.total == 0 && quiet_failLow.total == 0 &&
               capt_failHigh.total  == 0 && capt_improveAlpha.total  == 0 && capt_failLow.total  == 0;
    }
};

class HistoryDistLogger {
public:
    static HistoryDistLogger& instance() {
        static HistoryDistLogger inst;
        return inst;
    }

    // Log for a quiet move (main history)
    // depth = remaining depth at the node (in plies), or any "depth bucket" you want.
    // If you don't pass depth, it goes into bucket 0 ("unknown/unbucketed").
    void logQuiet(HistOutcome o, int score) { logQuiet(o, score, 0); }
    void logQuiet(HistOutcome o, int score, int depth) {
        std::lock_guard<std::mutex> lk(mu_);
        HistoryDistSnapshot& s = snapForDepth_(depth);
        switch (o) {
            case HistOutcome::FailHigh:     s.quiet_failHigh.add(score); break;
            case HistOutcome::ImproveAlpha: s.quiet_improveAlpha.add(score); break;
            case HistOutcome::FailLow:      s.quiet_failLow.add(score); break;
        }
    }

    // Log for a capture move (capture history)
    // depth = remaining depth at the node (in plies), or any "depth bucket" you want.
    // If you don't pass depth, it goes into bucket 0 ("unknown/unbucketed").
    void logCapture(HistOutcome o, int score) { logCapture(o, score, 0); }
    void logCapture(HistOutcome o, int score, int depth) {
        std::lock_guard<std::mutex> lk(mu_);
        HistoryDistSnapshot& s = snapForDepth_(depth);
        switch (o) {
            case HistOutcome::FailHigh:     s.capt_failHigh.add(score); break;
            case HistOutcome::ImproveAlpha: s.capt_improveAlpha.add(score); break;
            case HistOutcome::FailLow:      s.capt_failLow.add(score); break;
        }
    }

    // One JSON line per flush call
    void flushJsonlAppend(const std::string& path,
                          const std::string& engineTag = "",
                          int threads = -1,
                          uint64_t nodes = 0,
                          int completedDepth = -1) {
        std::lock_guard<std::mutex> lk(mu_);
        std::ofstream out(path, std::ios::app);
        if (!out) return;

        auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::string runId = makeRunId_();

        out << "{";
        out << "\"ts_utc\":\"" << isoUtc_(t) << "\",";
        out << "\"run_id\":\"" << runId << "\",";
        out << "\"engine_tag\":\"" << escape_(engineTag) << "\",";
        out << "\"threads\":" << threads << ",";
        out << "\"nodes\":" << nodes << ",";
        out << "\"completed_depth\":" << completedDepth << ",";

        // "by_depth" is a JSON object whose keys are depth buckets (as strings).
        // We only emit buckets that have at least one sample.
        out << "\"by_depth\":{";
        bool firstDepth = true;
        for (int d = 0; d <= MAX_LOG_DEPTH; d++) {
            if (byDepth_[(size_t)d].empty()) continue;
            if (!firstDepth) out << ",";
            firstDepth = false;

            out << "\"" << d << "\":{";
            writeHist_(out, "quiet_fail_high", byDepth_[(size_t)d].quiet_failHigh); out << ",";
            writeHist_(out, "quiet_improve_alpha", byDepth_[(size_t)d].quiet_improveAlpha); out << ",";
            writeHist_(out, "quiet_fail_low", byDepth_[(size_t)d].quiet_failLow); out << ",";

            writeHist_(out, "capt_fail_high", byDepth_[(size_t)d].capt_failHigh); out << ",";
            writeHist_(out, "capt_improve_alpha", byDepth_[(size_t)d].capt_improveAlpha); out << ",";
            writeHist_(out, "capt_fail_low", byDepth_[(size_t)d].capt_failLow);
            out << "}";
        }
        out << "}";

        out << "}\n";
    }

private:
    HistoryDistLogger() = default;

    static constexpr int MAX_LOG_DEPTH = 64; // depth buckets 0..64 (0 = "unknown/unbucketed")

    HistoryDistSnapshot& snapForDepth_(int depth) {
        if (depth < 0) depth = 0;
        if (depth > MAX_LOG_DEPTH) depth = MAX_LOG_DEPTH;
        return byDepth_[(size_t)depth];
    }

    template<int MAX_ABS, int STEP>
    void writeHist_(std::ofstream& out, const char* key, const FixedHistogram<MAX_ABS, STEP>& h) {
        out << "\"" << key << "\":{";
        out << "\"min\":" << (-MAX_ABS) << ",";
        out << "\"max\":" << (MAX_ABS) << ",";
        out << "\"step\":" << STEP << ",";
        out << "\"n\":" << h.total << ",";
        out << "\"bins\":[";
        for (int i = 0; i < h.NBINS; i++) {
            if (i) out << ",";
            out << h.bins[(size_t)i];
        }
        out << "]}";
    }

    static std::string isoUtc_(std::time_t t) {
        std::tm tm{};
    #if defined(_WIN32)
        gmtime_s(&tm, &t);
    #else
        gmtime_r(&t, &tm);
    #endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    static std::string escape_(const std::string& s) {
        std::string o;
        o.reserve(s.size());
        for (char c : s) {
            if (c == '\\' || c == '"') { o.push_back('\\'); o.push_back(c); }
            else if (c == '\n') o += "\\n";
            else o.push_back(c);
        }
        return o;
    }

    static std::string makeRunId_() {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        uint64_t x = rng();
        std::ostringstream oss;
        oss << std::hex << x;
        return oss.str();
    }

    std::mutex mu_;
    std::array<HistoryDistSnapshot, (size_t)MAX_LOG_DEPTH + 1> byDepth_{};
};

} // namespace engine