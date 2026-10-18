// Strata save core (RFC 0019 §6, M1): content-addressed saves behind an atomic
// HEAD pointer.
//
// A slot is a directory:
//
//   base-<sha256>.usdc              the immutable base world (written once)
//   save-<tick>-<sha256>.usdc       immutable save deltas; the name carries the hash
//   HEAD                            names the current base + save and the recent history,
//                                   ends with a SHA-256 of its own lines; replaced by rename
//   HEAD.prev                       the previous HEAD (hard link), for fallback
//   LOCK                            exclusive lock while a process has the slot open
//
// Guarantees:
//  - A save is written to a temp file, fsynced, hashed, then VERIFIED (composed,
//    hydrated and compared field by field with the state being saved) before it
//    is renamed to its content-addressed name. Only then is HEAD replaced. A
//    crash at any instant leaves HEAD naming a complete, verified save.
//  - Every file's SHA-256 is checked BEFORE USD parses a byte of it.
//  - Every layer (content, base, mods, save) is opened by Strata; a missing or
//    unreadable one fails the load. USD itself would skip it with a warning and
//    load the bare base world.
//  - Integrity failures fall back to the previous saves (newest first) and say
//    so. Policy refusals (another game, a newer format or schema, a different mod
//    list) never fall back, because an older save would silently lose progress.
//  - Older schemas are migrated on a copy: new base + new delta, verified, then
//    committed through the same protocol. The original files are never edited.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "strata/save/fs.h"
#include "strata/usd/world_io.h"
#include "strata/world/world.h"

namespace strata::save {

// What the game tells the engine about its saved data.
struct Schema {
    std::string gameId;
    int version = 1;
    // An empty world (tables, fields, channels, variant keys) for a schema
    // version: the current one and every older version that can be migrated.
    std::function<world::World(int version)> templateFor;
    // upgrade[v] turns a hydrated version-v world into a version-(v+1) world.
    std::map<int, std::function<bool(world::World&, std::string* err)>> upgrade;
};

struct ModRef {
    std::string id;                   // stable mod id
    std::vector<std::string> layers;  // weakest first
};

struct Layers {
    std::vector<std::string> content;  // the game's shipped content (prefab library), weakest first
    std::vector<ModRef> mods;          // the player's load order, weakest first (later wins)
};

// Test hooks: stop the save at a protocol step, as a crash would.
enum class Crash { None, AfterTempWrite, AfterVerify, AfterRename, AfterHeadTemp, AfterPrevLink };

struct SaveOptions {
    Crash crashAt = Crash::None;
    // Test hook: called on the fsynced temp file before it is hashed and verified.
    std::function<void(const std::string& tmpPath)> tamper;
};

struct SaveResult {
    bool ok = false;
    std::string error;
    std::string file;    // committed file name inside the slot
    std::string sha256;
    uint64_t bytes = 0;
    int64_t tick = 0;
    double snapshotMs = 0;  // main-thread copy (async saves only)
    double exportMs = 0, verifyMs = 0, commitMs = 0;
};

struct LoadOptions {
    bool fallback = true;           // try older saves after an integrity failure
    bool allowModMismatch = false;  // explicit player choice ("continue without X")
    bool allowMigration = true;
};

struct LoadResult {
    bool ok = false;
    std::string error;
    world::World current;          // hydrated live state
    usd::Manifest manifest;        // of the loaded save (empty gameId for a never-saved slot)
    std::string saveFile;          // "" when the slot holds only a base
    bool usedFallback = false;     // HEAD's save was damaged; an older one was loaded
    bool migrated = false;         // an older schema was upgraded and committed
    std::vector<std::string> warnings;
    std::vector<std::string> rejected;  // "<file>: <why>" for each candidate that failed
    double composeMs = 0, hydrateMs = 0;
};

struct Head {
    std::string baseFile, baseSha;
    uint64_t baseSize = 0;
    std::string saveFile, saveSha;  // saveFile empty: never saved
    uint64_t saveSize = 0;
    // Recent saves, most recent first: (save file, sha256 of the base it diffs against).
    std::vector<std::pair<std::string, std::string>> history;
};
std::string formatHead(const Head& h);
bool parseHead(const std::string& text, Head& h, std::string* err);

// "<id>@<sha256>" where the hash covers every layer's bytes in order.
bool modId(const ModRef& m, std::string& out, std::string* err);

class Slot {
public:
    // Opens (creating if needed) and locks a slot directory. Fails if another
    // process holds the slot. Leftover temp files from a crash are deleted.
    static std::unique_ptr<Slot> open(const std::string& dir, Schema schema, Layers layers, std::string* err);
    ~Slot();

    // New game: commit `base` as the immutable base layer and point HEAD at it.
    // Refuses if the slot already has a HEAD.
    bool create(const world::World& base, std::string* err);

    // Load HEAD's save (see the guarantees above). On success the slot keeps
    // the baseline that later saves diff against.
    LoadResult load(const LoadOptions& opt = {});

    // Synchronous save of `current` (which must have the loaded schema).
    SaveResult save(const world::World& current, int64_t tick, const SaveOptions& opt = {});

    // Asynchronous save: copies `current` on the calling thread (the only
    // main-thread cost, reported as snapshotMs), then writes, verifies and
    // commits on the slot's worker thread. One save at a time: a call while one
    // is in flight returns a ready result with error "busy".
    std::future<SaveResult> saveAsync(const world::World& current, int64_t tick, SaveOptions opt = {});
    bool busy() const { return inFlight_.load(); }
    void waitIdle();

    const std::string& dir() const { return dir_; }
    int keep = 5;  // saves kept in history (older ones are pruned)

private:
    Slot() = default;
    struct Job {
        world::World snapshot;
        int64_t tick;
        SaveOptions opt;
        double snapshotMs;
        std::promise<SaveResult> done;
    };
    SaveResult saveImpl(const world::World& current, int64_t tick, const SaveOptions& opt);
    bool writeHead(const Head& h, const SaveOptions& opt, std::string* err);
    bool commitFile(const pxr::SdfLayerRefPtr& layer, const std::string& prefix, const world::World* expect,
                    const std::vector<std::string>& composeWith, const SaveOptions& opt, std::string& name,
                    std::string& sha, uint64_t& size, SaveResult* stats, std::string* err);
    void prune(const Head& h);
    void removeTemps();
    std::vector<std::string> modLayers() const;
    void workerLoop();

    std::string dir_;
    Schema schema_;
    Layers layers_;
    std::vector<std::string> modIds_;
    std::unique_ptr<fs::DirLock> lock_;
    std::shared_ptr<const world::World> baseline_;
    Head head_;
    bool loaded_ = false;
    uint64_t tmpCounter_ = 0;

    std::mutex mu_;
    std::condition_variable cv_;
    std::unique_ptr<Job> job_;
    bool stop_ = false;
    std::atomic<bool> inFlight_{false};
    std::thread worker_;
};

}  // namespace strata::save
