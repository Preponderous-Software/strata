#include "strata/save/slot.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <sstream>
#include <unistd.h>

#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/sdf/layer.h>

#include "strata/save/sha256.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace strata::save {

using world::World;

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

void setErr(std::string* e, const std::string& s) {
    if (e) *e = s;
}

bool isHex64(const std::string& s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

// save-<12-digit tick>-<sha>.usdc -> tick, sha
bool parseSaveName(const std::string& n, int64_t& tick, std::string& sha) {
    if (n.size() != 5 + 12 + 1 + 64 + 5 || n.compare(0, 5, "save-") != 0 || n[17] != '-' ||
        n.compare(n.size() - 5, 5, ".usdc") != 0)
        return false;
    const std::string t = n.substr(5, 12);
    if (t.find_first_not_of("0123456789") != std::string::npos) return false;
    sha = n.substr(18, 64);
    if (!isHex64(sha)) return false;
    tick = std::stoll(t);
    return true;
}

bool parseBaseName(const std::string& n, std::string& sha) {
    if (n.size() != 5 + 64 + 5 || n.compare(0, 5, "base-") != 0 || n.compare(n.size() - 5, 5, ".usdc") != 0)
        return false;
    sha = n.substr(5, 64);
    return isHex64(sha);
}

std::string saveName(int64_t tick, const std::string& sha) {
    char b[32];
    std::snprintf(b, sizeof b, "save-%012lld-", static_cast<long long>(tick));
    return b + sha + ".usdc";
}

enum class Fail { None, Integrity, Policy };

struct Attempt {
    Fail fail = Fail::None;
    std::string why;
};

std::string modName(const std::string& id) { return id.substr(0, id.find('@')); }

}  // namespace

// ---- HEAD ---------------------------------------------------------------

std::string formatHead(const Head& h) {
    std::ostringstream o;
    o << "strata-head 1\n";
    o << "base " << h.baseFile << " " << h.baseSha << " " << h.baseSize << "\n";
    if (h.saveFile.empty()) o << "save -\n";
    else o << "save " << h.saveFile << " " << h.saveSha << " " << h.saveSize << "\n";
    for (const auto& e : h.history) o << "hist " << e.first << " " << e.second << "\n";
    const std::string body = o.str();
    return body + "sum " + sha256Hex(body) + "\n";
}

bool parseHead(const std::string& text, Head& h, std::string* err) {
    h = Head();
    auto fail = [&](const std::string& s) {
        setErr(err, "HEAD: " + s);
        return false;
    };
    const size_t sumAt = text.rfind("sum ");
    if (sumAt == std::string::npos || (sumAt != 0 && text[sumAt - 1] != '\n')) return fail("no checksum line");
    const std::string body = text.substr(0, sumAt);
    const std::string sumLine = text.substr(sumAt);
    if (sumLine.size() != 4 + 64 + 1 || sumLine.back() != '\n' || !isHex64(sumLine.substr(4, 64)))
        return fail("malformed checksum line");
    if (sumLine.substr(4, 64) != sha256Hex(body)) return fail("checksum mismatch (torn or damaged)");
    std::istringstream in(body);
    std::string line;
    if (!std::getline(in, line) || line != "strata-head 1") return fail("unknown version");
    bool sawBase = false, sawSave = false;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string kind;
        ls >> kind;
        if (kind == "base" && !sawBase) {
            if (!(ls >> h.baseFile >> h.baseSha >> h.baseSize)) return fail("bad base line");
            std::string sha;
            if (!parseBaseName(h.baseFile, sha) || sha != h.baseSha) return fail("bad base name");
            sawBase = true;
        } else if (kind == "save" && sawBase && !sawSave) {
            std::string f;
            if (!(ls >> f)) return fail("bad save line");
            if (f != "-") {
                h.saveFile = f;
                int64_t tick;
                std::string sha;
                if (!(ls >> h.saveSha >> h.saveSize) || !parseSaveName(f, tick, sha) || sha != h.saveSha)
                    return fail("bad save line");
            }
            sawSave = true;
        } else if (kind == "hist" && sawSave) {
            std::string f, b, sha;
            int64_t tick;
            if (!(ls >> f >> b) || !parseSaveName(f, tick, sha) || !isHex64(b)) return fail("bad hist line");
            h.history.emplace_back(f, b);
        } else {
            return fail("unexpected line: " + line.substr(0, 40));
        }
        std::string extra;
        if (ls >> extra) return fail("trailing data on a line");
    }
    if (!sawBase || !sawSave) return fail("incomplete");
    return true;
}

bool modId(const ModRef& m, std::string& out, std::string* err) {
    Sha256 all;
    for (const auto& l : m.layers) {
        std::string hex;
        if (!sha256File(l, hex)) {
            setErr(err, "mod " + m.id + ": cannot read layer " + l);
            return false;
        }
        all.update(hex.data(), hex.size());
    }
    out = m.id + "@" + toHex(all.finish());
    return true;
}

// ---- Slot ---------------------------------------------------------------

std::unique_ptr<Slot> Slot::open(const std::string& dir, Schema schema, Layers layers, std::string* err) {
    if (!schema.templateFor) {
        setErr(err, "schema.templateFor is required");
        return nullptr;
    }
    if (!fs::makeDirs(dir)) {
        setErr(err, "cannot create slot directory " + dir);
        return nullptr;
    }
    std::unique_ptr<Slot> s(new Slot());
    s->dir_ = dir;
    s->schema_ = std::move(schema);
    s->layers_ = std::move(layers);
    s->lock_ = std::make_unique<fs::DirLock>(dir);
    if (!s->lock_->held()) {
        setErr(err, "save slot " + dir + " is in use by another process");
        return nullptr;
    }
    for (const auto& m : s->layers_.mods) {
        std::string id;
        if (!modId(m, id, err)) return nullptr;
        s->modIds_.push_back(id);
    }
    s->removeTemps();
    s->worker_ = std::thread([p = s.get()] { p->workerLoop(); });
    return s;
}

Slot::~Slot() {
    {
        std::lock_guard<std::mutex> g(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Slot::removeTemps() {
    // A temp file is a save that never committed: never trusted, always deleted.
    for (const auto& n : fs::list(dir_))
        if (n.compare(0, 5, ".tmp-") == 0 || n == "HEAD.tmp") fs::remove(fs::join(dir_, n));
}

std::vector<std::string> Slot::modLayers() const {
    std::vector<std::string> out;
    for (const auto& m : layers_.mods) out.insert(out.end(), m.layers.begin(), m.layers.end());
    return out;
}

bool Slot::commitFile(const SdfLayerRefPtr& layer, const std::string& prefix, const World* expect,
                      const std::vector<std::string>& composeWith, const SaveOptions& opt, std::string& name,
                      std::string& sha, uint64_t& size, SaveResult* stats, std::string* err) {
    const std::string tmp =
        fs::join(dir_, ".tmp-" + std::to_string(::getpid()) + "-" + std::to_string(++tmpCounter_) + ".usdc");
    auto t0 = Clock::now();
    {
        TfErrorMark mark;
        if (!layer->Export(tmp) || !mark.IsClean()) {
            mark.Clear();
            fs::remove(tmp);
            setErr(err, "export failed");
            return false;
        }
    }
    if (!fs::fsyncFile(tmp)) {
        fs::remove(tmp);
        setErr(err, "fsync failed");
        return false;
    }
    if (stats) stats->exportMs += msSince(t0);
    if (opt.tamper) opt.tamper(tmp);
    if (opt.crashAt == Crash::AfterTempWrite) {
        setErr(err, "simulated crash after temp write");
        return false;
    }
    t0 = Clock::now();
    if (!sha256File(tmp, sha, &size)) {
        fs::remove(tmp);
        setErr(err, "cannot hash temp file");
        return false;
    }
    if (expect) {
        // Verify before commit: the bytes on disk must compose and hydrate back
        // to exactly the state being saved, or they never replace anything.
        std::vector<std::string> stack = composeWith;
        stack.push_back(tmp);
        std::string why;
        World back = world::emptyLike(*expect);
        bool ok = false;
        try {
            auto stage = usd::compose(stack, &why);
            ok = stage && usd::hydrate(stage, back, &why) && world::equal(back, *expect, &why);
        } catch (const std::exception& e) {
            why = std::string("exception: ") + e.what();
        }
        if (!ok) {
            fs::remove(tmp);
            setErr(err, "verify refused commit: " + why);
            return false;
        }
        // The file must not have changed while it was being verified.
        std::string again;
        if (!sha256File(tmp, again) || again != sha) {
            fs::remove(tmp);
            setErr(err, "temp file changed during verification");
            return false;
        }
    }
    if (stats) stats->verifyMs += msSince(t0);
    if (opt.crashAt == Crash::AfterVerify) {
        setErr(err, "simulated crash after verify");
        return false;
    }
    t0 = Clock::now();
    name = prefix + sha + ".usdc";
    if (!fs::atomicReplace(tmp, fs::join(dir_, name)) || !fs::fsyncDir(dir_)) {
        fs::remove(tmp);
        setErr(err, "rename failed");
        return false;
    }
    if (stats) stats->commitMs += msSince(t0);
    if (opt.crashAt == Crash::AfterRename) {
        setErr(err, "simulated crash after rename");
        return false;
    }
    return true;
}

bool Slot::writeHead(const Head& h, const SaveOptions& opt, std::string* err) {
    const std::string headPath = fs::join(dir_, "HEAD"), tmp = fs::join(dir_, "HEAD.tmp"),
                      prev = fs::join(dir_, "HEAD.prev");
    if (!fs::writeDurable(tmp, formatHead(h))) {
        fs::remove(tmp);
        setErr(err, "cannot write HEAD.tmp");
        return false;
    }
    if (opt.crashAt == Crash::AfterHeadTemp) {
        setErr(err, "simulated crash after HEAD.tmp");
        return false;
    }
    if (fs::exists(headPath)) {
        // Keep the previous HEAD by hard link, so HEAD itself is never absent:
        // the rename below replaces it atomically.
        if (!fs::remove(prev) || !fs::hardLink(headPath, prev)) {
            setErr(err, "cannot keep HEAD.prev");
            return false;
        }
    }
    if (opt.crashAt == Crash::AfterPrevLink) {
        setErr(err, "simulated crash after HEAD.prev link");
        return false;
    }
    if (!fs::atomicReplace(tmp, headPath) || !fs::fsyncDir(dir_)) {
        setErr(err, "cannot replace HEAD");
        return false;
    }
    return true;
}

void Slot::prune(const Head& h) {
    std::set<std::string> keepSaves, keepBases{h.baseSha};
    if (!h.saveFile.empty()) keepSaves.insert(h.saveFile);
    for (const auto& e : h.history) {
        keepSaves.insert(e.first);
        keepBases.insert(e.second);
    }
    std::string text;
    Head prev;
    if (fs::readSmall(fs::join(dir_, "HEAD.prev"), text, 1 << 20) && parseHead(text, prev, nullptr)) {
        keepBases.insert(prev.baseSha);
        if (!prev.saveFile.empty()) keepSaves.insert(prev.saveFile);
        for (const auto& e : prev.history) {
            keepSaves.insert(e.first);
            keepBases.insert(e.second);
        }
    }
    for (const auto& n : fs::list(dir_)) {
        int64_t tick;
        std::string sha;
        if (parseSaveName(n, tick, sha) && !keepSaves.count(n)) fs::remove(fs::join(dir_, n));
        else if (parseBaseName(n, sha) && !keepBases.count(sha)) fs::remove(fs::join(dir_, n));
    }
}

bool Slot::create(const World& base, std::string* err) {
    waitIdle();
    if (fs::exists(fs::join(dir_, "HEAD"))) {
        setErr(err, "slot already holds a game; refusing to overwrite it");
        return false;
    }
    SdfLayerRefPtr layer;
    try {
        layer = usd::writeFull(base);
    } catch (const std::exception& e) {
        setErr(err, std::string("cannot write base: ") + e.what());
        return false;
    }
    Head h;
    if (!commitFile(layer, "base-", &base, layers_.content, SaveOptions(), h.baseFile, h.baseSha, h.baseSize, nullptr,
                    err))
        return false;
    if (!writeHead(h, SaveOptions(), err)) return false;
    head_ = h;
    baseline_.reset();
    loaded_ = false;
    return true;
}

LoadResult Slot::load(const LoadOptions& opt) {
    waitIdle();
    removeTemps();
    LoadResult res;
    loaded_ = false;
    baseline_.reset();

    struct Candidate {
        std::string file, sha;
        uint64_t size = 0;  // 0 = unknown
    };
    std::vector<Candidate> cands;
    std::set<std::string> seen;
    auto add = [&](const std::string& f, const std::string& sha, uint64_t size) {
        if (!f.empty() && seen.insert(f).second) cands.push_back({f, sha, size});
    };

    Head head;
    std::string text, why;
    const bool headOk = fs::readSmall(fs::join(dir_, "HEAD"), text, 1 << 20) && parseHead(text, head, &why);
    if (!headOk) res.rejected.push_back("HEAD: " + (why.empty() ? std::string("missing or unreadable") : why));

    const std::vector<std::string> mods = modLayers();

    // Verifies a base file by hash, then composes content + base + mods.
    auto baseStack = [&](const std::string& baseSha, std::vector<std::string>& stack, Attempt& a) {
        const std::string baseFile = "base-" + baseSha + ".usdc";
        std::string got;
        uint64_t size = 0;
        if (!sha256File(fs::join(dir_, baseFile), got, &size)) {
            a = {Fail::Integrity, "base layer " + baseFile + " is missing"};
            return false;
        }
        if (got != baseSha) {
            a = {Fail::Integrity, "base layer " + baseFile + " is damaged (checksum mismatch)"};
            return false;
        }
        stack = layers_.content;
        stack.push_back(fs::join(dir_, baseFile));
        stack.insert(stack.end(), mods.begin(), mods.end());
        return true;
    };

    auto hydrateStack = [&](const std::vector<std::string>& stack, World& out, std::string* e) {
        auto t0 = Clock::now();
        auto stage = usd::compose(stack, e);
        res.composeMs += msSince(t0);
        if (!stage) return false;
        t0 = Clock::now();
        const bool ok = usd::hydrate(stage, out, e);
        res.hydrateMs += msSince(t0);
        return ok;
    };

    auto upgradeChain = [&](World& w, int from, std::string* e) {
        for (int v = from; v < schema_.version; ++v) {
            auto it = schema_.upgrade.find(v);
            if (it == schema_.upgrade.end()) {
                setErr(e, "no migration from schema " + std::to_string(v));
                return false;
            }
            if (!it->second(w, e)) return false;
        }
        return true;
    };

    // Never-saved slot: HEAD names only the base.
    if (headOk && head.saveFile.empty()) {
        Attempt a;
        std::vector<std::string> stack;
        World base = schema_.templateFor(schema_.version);
        base = world::emptyLike(base);
        if (!baseStack(head.baseSha, stack, a)) {
            res.error = a.why;
            return res;
        }
        if (!hydrateStack(stack, base, &why)) {
            res.error = "base world does not load: " + why;
            return res;
        }
        res.current = base;
        baseline_ = std::make_shared<const World>(std::move(base));
        head_ = head;
        loaded_ = true;
        res.ok = true;
        return res;
    }

    auto addHead = [&](const Head& h) {
        add(h.saveFile, h.saveSha, h.saveSize);
        for (const auto& e : h.history) {
            int64_t t;
            std::string sha;
            parseSaveName(e.first, t, sha);
            add(e.first, sha, 0);
        }
    };
    // Every intact save file, newest tick first (each is self-verifying: its
    // name carries its hash).
    auto addScan = [&] {
        std::vector<std::pair<int64_t, std::string>> found;
        for (const auto& n : fs::list(dir_)) {
            int64_t t;
            std::string sha;
            if (parseSaveName(n, t, sha)) found.emplace_back(t, n);
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a > b; });
        for (const auto& f : found) {
            int64_t t;
            std::string sha;
            parseSaveName(f.second, t, sha);
            add(f.second, sha, 0);
        }
    };
    Head prev;
    const bool prevOk = fs::readSmall(fs::join(dir_, "HEAD.prev"), text, 1 << 20) && parseHead(text, prev, nullptr);
    if (headOk) {
        // HEAD's save, its history, then the previous HEAD, then anything else.
        addHead(head);
        if (prevOk) addHead(prev);
        addScan();
    } else {
        // HEAD itself is damaged: the newest intact save is the best evidence.
        addScan();
        if (prevOk) addHead(prev);
    }
    if (cands.empty()) {
        res.error = "no save in slot " + dir_;
        return res;
    }

    for (size_t ci = 0; ci < cands.size(); ++ci) {
        const Candidate& c = cands[ci];
        Attempt a;
        const std::string path = fs::join(dir_, c.file);
        LoadResult attempt;
        try {
            // 1. Checksum BEFORE the USD parser sees the file.
            std::string got;
            uint64_t size = 0;
            if (!sha256File(path, got, &size)) {
                a = {Fail::Integrity, "missing or unreadable"};
            } else if (got != c.sha || (c.size && size != c.size)) {
                a = {Fail::Integrity, "checksum mismatch (damaged file)"};
            }
            // 2. Manifest.
            usd::Manifest m;
            if (a.fail == Fail::None) {
                TfErrorMark mark;
                SdfLayerRefPtr l = SdfLayer::OpenAsAnonymous(path);
                if (!l || !mark.IsClean()) a = {Fail::Integrity, "unreadable save layer"};
                else if (!usd::readManifest(l, m, &why)) a = {Fail::Integrity, why};
                mark.Clear();
            }
            // 3. Policy: these never fall back to an older save.
            if (a.fail == Fail::None) {
                if (m.formatVersion > usd::kFormatVersion)
                    a = {Fail::Policy, "save is from a newer engine format (" + std::to_string(m.formatVersion) + ")"};
                else if (m.formatVersion < 1)
                    a = {Fail::Integrity, "bad format version"};
                else if (m.gameId != schema_.gameId)
                    a = {Fail::Policy, "save belongs to game \"" + m.gameId + "\""};
                else if (m.gameSchemaVersion > schema_.version)
                    a = {Fail::Policy, "save is from a newer game version (schema " +
                                           std::to_string(m.gameSchemaVersion) + ")"};
                else if (m.gameSchemaVersion < schema_.version && !opt.allowMigration)
                    a = {Fail::Policy, "save needs migration from schema " + std::to_string(m.gameSchemaVersion)};
                else if (!isHex64(m.baseHash))
                    a = {Fail::Integrity, "manifest has no valid base hash"};
            }
            if (a.fail == Fail::None) {
                bool namesMatch = m.mods.size() == modIds_.size();
                for (size_t i = 0; namesMatch && i < modIds_.size(); ++i)
                    namesMatch = modName(m.mods[i]) == modName(modIds_[i]);
                if (!namesMatch) {
                    std::string was, now;
                    for (const auto& x : m.mods) was += modName(x) + " ";
                    for (const auto& x : modIds_) now += modName(x) + " ";
                    if (!opt.allowModMismatch)
                        a = {Fail::Policy, "mod list differs: save=[" + was + "] now=[" + now + "]"};
                    else
                        attempt.warnings.push_back("loading with a different mod list: save=[" + was + "] now=[" +
                                                   now + "]");
                } else if (m.mods != modIds_) {
                    attempt.warnings.push_back("a mod's content changed since this save");
                }
            }
            // 4. Compose and hydrate baseline (no save) and current (with save).
            std::vector<std::string> stack;
            World baseline, current;
            if (a.fail == Fail::None && baseStack(m.baseHash, stack, a)) {
                World tmpl = world::emptyLike(schema_.templateFor(m.gameSchemaVersion));
                baseline = tmpl;
                current = tmpl;
                if (!hydrateStack(stack, baseline, &why)) {
                    a = {Fail::Policy, "game content, base or mods do not load: " + why};
                } else {
                    stack.push_back(path);
                    if (!hydrateStack(stack, current, &why)) a = {Fail::Integrity, "save does not load: " + why};
                }
            }
            // 5. Migrate an older schema on a copy, then commit it like a save.
            if (a.fail == Fail::None && m.gameSchemaVersion < schema_.version) {
                std::vector<std::string> baseOnly = layers_.content;
                baseOnly.push_back(fs::join(dir_, "base-" + m.baseHash + ".usdc"));
                World oldBase = world::emptyLike(schema_.templateFor(m.gameSchemaVersion));
                World newBase = oldBase;
                std::string e;
                if (!hydrateStack(baseOnly, newBase, &e) || !upgradeChain(newBase, m.gameSchemaVersion, &e) ||
                    !upgradeChain(current, m.gameSchemaVersion, &e)) {
                    a = {Fail::Policy, "migration failed: " + e};
                } else {
                    Head nh;
                    SdfLayerRefPtr bl = usd::writeFull(newBase);
                    if (!commitFile(bl, "base-", &newBase, layers_.content, SaveOptions(), nh.baseFile, nh.baseSha,
                                    nh.baseSize, nullptr, &e)) {
                        a = {Fail::Policy, "migration: " + e};
                    } else {
                        std::vector<std::string> ns = layers_.content;
                        ns.push_back(fs::join(dir_, nh.baseFile));
                        ns.insert(ns.end(), mods.begin(), mods.end());
                        World nb = world::emptyLike(schema_.templateFor(schema_.version));
                        if (!hydrateStack(ns, nb, &e)) {
                            a = {Fail::Policy, "migration: migrated base does not load: " + e};
                        } else {
                            usd::Manifest nm = m;
                            nm.formatVersion = usd::kFormatVersion;
                            nm.gameSchemaVersion = schema_.version;
                            nm.baseHash = nh.baseSha;
                            nm.mods = modIds_;
                            SdfLayerRefPtr dl = usd::writeDelta(nb, current, nm);
                            if (!commitFile(dl, saveName(m.tick, "").substr(0, 18), &current, ns, SaveOptions(),
                                            nh.saveFile, nh.saveSha, nh.saveSize, nullptr, &e)) {
                                a = {Fail::Policy, "migration: " + e};
                            } else {
                                nh.history.emplace_back(nh.saveFile, nh.baseSha);
                                if (headOk)
                                    for (const auto& h : head.history)
                                        if (h.first != nh.saveFile) nh.history.push_back(h);
                                if (nh.history.size() > static_cast<size_t>(keep)) nh.history.resize(keep);
                                if (!writeHead(nh, SaveOptions(), &e)) {
                                    a = {Fail::Policy, "migration: " + e};
                                } else {
                                    attempt.migrated = true;
                                    attempt.warnings.push_back("migrated from schema " +
                                                               std::to_string(m.gameSchemaVersion) + " to " +
                                                               std::to_string(schema_.version) +
                                                               "; the original save was kept");
                                    head = nh;
                                    m = nm;
                                    baseline = nb;
                                    attempt.saveFile = nh.saveFile;
                                    prune(nh);
                                }
                            }
                        }
                    }
                }
            }
            if (a.fail == Fail::None) {
                res.ok = true;
                res.current = std::move(current);
                res.manifest = m;
                if (attempt.saveFile.empty()) attempt.saveFile = c.file;
                res.saveFile = attempt.saveFile;
                res.migrated = attempt.migrated;
                res.warnings.insert(res.warnings.end(), attempt.warnings.begin(), attempt.warnings.end());
                res.usedFallback = !headOk || ci > 0;
                if (res.usedFallback)
                    res.warnings.push_back("the latest save could not be loaded; loaded " + res.saveFile +
                                           " instead (see rejected)");
                baseline_ = std::make_shared<const World>(std::move(baseline));
                // The next save extends this save's history.
                Head nh;
                nh.baseSha = m.baseHash;
                nh.baseFile = "base-" + m.baseHash + ".usdc";
                uint64_t bsz = 0;
                std::string bsha;
                sha256File(fs::join(dir_, nh.baseFile), bsha, &bsz);
                nh.baseSize = bsz;
                nh.saveFile = res.saveFile;
                {
                    int64_t t;
                    std::string sha;
                    parseSaveName(res.saveFile, t, sha);
                    nh.saveSha = sha;
                    std::string g;
                    uint64_t sz = 0;
                    sha256File(fs::join(dir_, res.saveFile), g, &sz);
                    nh.saveSize = sz;
                }
                nh.history = headOk && !res.usedFallback ? head.history
                                                         : std::vector<std::pair<std::string, std::string>>{
                                                               {res.saveFile, m.baseHash}};
                head_ = nh;
                loaded_ = true;
                return res;
            }
        } catch (const std::exception& e) {
            a = {Fail::Integrity, std::string("exception: ") + e.what()};
        }
        res.rejected.push_back(c.file + ": " + a.why);
        if (a.fail == Fail::Policy) {
            res.error = a.why;
            return res;
        }
        if (!opt.fallback) break;
    }
    res.error = "no loadable save: ";
    for (const auto& r : res.rejected) res.error += r + "; ";
    return res;
}

SaveResult Slot::saveImpl(const World& current, int64_t tick, const SaveOptions& opt) {
    SaveResult r;
    r.tick = tick;
    if (!loaded_ || !baseline_) {
        r.error = "load (or create + load) the slot before saving";
        return r;
    }
    if (tick < 0 || tick > 999999999999LL) {
        r.error = "tick out of range";
        return r;
    }
    removeTemps();
    usd::Manifest m;
    m.gameId = schema_.gameId;
    m.gameSchemaVersion = schema_.version;
    m.baseHash = head_.baseSha;
    m.mods = modIds_;
    m.tick = tick;
    SdfLayerRefPtr layer;
    const auto t0 = Clock::now();
    try {
        layer = usd::writeDelta(*baseline_, current, m);
    } catch (const std::exception& e) {
        r.error = std::string("cannot write save: ") + e.what();
        return r;
    }
    r.exportMs += msSince(t0);
    std::vector<std::string> stack = layers_.content;
    stack.push_back(fs::join(dir_, head_.baseFile));
    const auto mods = modLayers();
    stack.insert(stack.end(), mods.begin(), mods.end());
    std::string name, sha;
    uint64_t size = 0;
    const std::string prefix = saveName(tick, "").substr(0, 18);
    if (!commitFile(layer, prefix, &current, stack, opt, name, sha, size, &r, &r.error)) return r;
    const auto t1 = Clock::now();
    Head h = head_;
    h.saveFile = name;
    h.saveSha = sha;
    h.saveSize = size;
    h.history.clear();
    h.history.emplace_back(name, head_.baseSha);
    for (const auto& e : head_.history)
        if (e.first != name) h.history.push_back(e);
    if (h.history.size() > static_cast<size_t>(keep)) h.history.resize(keep);
    if (!writeHead(h, opt, &r.error)) return r;
    head_ = h;
    prune(h);
    r.commitMs += msSince(t1);
    r.ok = true;
    r.file = name;
    r.sha256 = sha;
    r.bytes = size;
    return r;
}

SaveResult Slot::save(const World& current, int64_t tick, const SaveOptions& opt) {
    waitIdle();
    return saveImpl(current, tick, opt);
}

std::future<SaveResult> Slot::saveAsync(const World& current, int64_t tick, SaveOptions opt) {
    bool expected = false;
    if (!inFlight_.compare_exchange_strong(expected, true)) {
        std::promise<SaveResult> p;
        SaveResult r;
        r.error = "busy";
        r.tick = tick;
        p.set_value(r);
        return p.get_future();
    }
    auto job = std::make_unique<Job>();
    const auto t0 = Clock::now();
    job->snapshot = current;  // the only main-thread cost
    job->snapshotMs = msSince(t0);
    job->tick = tick;
    job->opt = std::move(opt);
    auto fut = job->done.get_future();
    {
        std::lock_guard<std::mutex> g(mu_);
        job_ = std::move(job);
    }
    cv_.notify_all();
    return fut;
}

void Slot::waitIdle() {
    std::unique_lock<std::mutex> g(mu_);
    cv_.wait(g, [&] { return !inFlight_.load(); });
}

void Slot::workerLoop() {
    for (;;) {
        std::unique_ptr<Job> job;
        {
            std::unique_lock<std::mutex> g(mu_);
            cv_.wait(g, [&] { return stop_ || job_ != nullptr; });
            if (!job_ && stop_) return;
            job = std::move(job_);
        }
        SaveResult r;
        try {
            r = saveImpl(job->snapshot, job->tick, job->opt);
        } catch (const std::exception& e) {
            r.error = std::string("exception: ") + e.what();
        }
        r.snapshotMs = job->snapshotMs;
        {
            std::lock_guard<std::mutex> g(mu_);
            inFlight_ = false;
        }
        job->done.set_value(r);
        cv_.notify_all();
    }
}

}  // namespace strata::save
