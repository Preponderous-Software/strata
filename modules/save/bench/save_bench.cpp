// strata-save-bench <scratch dir> <agents> <grid size> <ticks> [reps]
//
// Measures the save core on the toy world (RFC 0019 §9): sync save phases,
// the async save's main-thread stall, load, file sizes and peak RSS. Prints one
// JSON line. Timings are the minimum over `reps` runs (default 5), which is far
// steadier than the mean on shared CI runners.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/resource.h>

#include "../tests/toy_game.h"
#include "strata/save/sha256.h"
#include "strata/save/slot.h"

using namespace strata;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: strata-save-bench <dir> <agents> <grid> <ticks> [reps]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const int agents = std::atoi(argv[2]), grid = std::atoi(argv[3]), ticks = std::atoi(argv[4]);
    const int reps = argc > 5 ? std::max(1, std::atoi(argv[5])) : 5;
    if (std::system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "'").c_str()) != 0) std::fprintf(stderr, "shell command failed\n");
    std::string err;
    auto slot = save::Slot::open(dir, toy::saveSchema(2), toy::layers(), &err);
    if (!slot) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    world::World w = toy::generate(42, grid, grid, agents);
    if (!slot->create(w, &err) || !slot->load().ok) {
        std::fprintf(stderr, "create/load failed: %s\n", err.c_str());
        return 1;
    }
    for (int t = 0; t < ticks; ++t) toy::step(w);
    const double inf = 1e300;
    double exportMs = inf, verifyMs = inf, commitMs = inf, totalMs = inf, stallMs = inf, asyncMs = inf;
    double composeMs = inf, hydrateMs = inf, loadMs = inf;
    uint64_t deltaBytes = 0;
    bool exact = true;
    using Clock = std::chrono::steady_clock;
    for (int r = 0; r < reps; ++r) {
        toy::step(w);  // a different state each rep, so every save is a real write
        auto t0 = Clock::now();
        auto s = slot->save(w, w.globals["toy:tick"]);
        const double total = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (!s.ok) {
            std::fprintf(stderr, "save failed: %s\n", s.error.c_str());
            return 1;
        }
        exportMs = std::min(exportMs, s.exportMs);
        verifyMs = std::min(verifyMs, s.verifyMs);
        commitMs = std::min(commitMs, s.commitMs);
        totalMs = std::min(totalMs, total);
        deltaBytes = s.bytes;
        toy::step(w);
        t0 = Clock::now();
        auto f = slot->saveAsync(w, w.globals["toy:tick"]);
        const double stall = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        auto a = f.get();
        if (!a.ok) {
            std::fprintf(stderr, "async save failed: %s\n", a.error.c_str());
            return 1;
        }
        stallMs = std::min(stallMs, stall);
        asyncMs = std::min(asyncMs, a.exportMs + a.verifyMs + a.commitMs);
        t0 = Clock::now();
        auto l = slot->load();
        const double lt = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (!l.ok) {
            std::fprintf(stderr, "load failed: %s\n", l.error.c_str());
            return 1;
        }
        exact = exact && world::equal(l.current, w);
        composeMs = std::min(composeMs, l.composeMs);
        hydrateMs = std::min(hydrateMs, l.hydrateMs);
        loadMs = std::min(loadMs, lt);
    }
    // Runner-speed calibration: SHA-256 of 32 MB (a 4 MB buffer, 8 times, so
    // it barely moves peak RSS), min of 5. The gate scales
    // timing baselines by calib_now / calib_baseline, so a slower shared
    // runner does not read as a regression (and a faster one cannot hide one).
    double calibMs = inf;
    {
        std::string buf(4u << 20, 'x');
        for (int i = 0; i < 5; ++i) {
            const auto c0 = Clock::now();
            save::Sha256 hasher;
            for (int k = 0; k < 8; ++k) hasher.update(buf.data(), buf.size());
            const std::string h = save::toHex(hasher.finish());
            calibMs = std::min(calibMs, std::chrono::duration<double, std::milli>(Clock::now() - c0).count());
            buf[static_cast<size_t>(i)] = h[0];
        }
    }
    uint64_t baseBytes = 0;
    for (const auto& n : save::fs::list(dir))
        if (n.compare(0, 5, "base-") == 0) {
            std::string sha;
            save::sha256File(save::fs::join(dir, n), sha, &baseBytes);
        }
    struct rusage ru {};
    getrusage(RUSAGE_SELF, &ru);
    std::printf("{\"agents\":%d,\"grid\":%d,\"ticks\":%d,\"reps\":%d,\"save_ms\":%.2f,\"export_ms\":%.2f,"
                "\"verify_ms\":%.2f,\"commit_ms\":%.2f,\"async_stall_ms\":%.3f,\"async_worker_ms\":%.2f,"
                "\"load_ms\":%.2f,\"compose_ms\":%.2f,\"hydrate_ms\":%.2f,\"base_bytes\":%llu,\"delta_bytes\":%llu,"
                "\"peak_rss_mb\":%.1f,\"calib_ms\":%.2f,\"round_trip\":\"%s\"}\n",
                agents, grid, ticks, reps, totalMs, exportMs, verifyMs, commitMs, stallMs, asyncMs, loadMs, composeMs,
                hydrateMs, static_cast<unsigned long long>(baseBytes), static_cast<unsigned long long>(deltaBytes),
                ru.ru_maxrss / 1024.0, calibMs, exact ? "EXACT" : "MISMATCH");
    return exact ? 0 : 1;
}
