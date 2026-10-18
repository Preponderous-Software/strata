#include "toy_game.h"

#include <algorithm>

using strata::world::FieldType;
using strata::world::Table;
using strata::world::World;

namespace toy {

namespace {
const char* kMood = "/Prefabs/Agent|mood";
uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
uint64_t next(World& w) {
    auto& s = w.globals["toy:rng"];
    s = static_cast<int64_t>(static_cast<uint64_t>(s) * 6364136223846793005ULL + 1442695040888963407ULL);
    return mix(static_cast<uint64_t>(s));
}
}  // namespace

std::string dataDir() { return STRATA_SAVE_TEST_DATA; }

World schema(int version) {
    World w;
    w.grid.init(0, 0, 16, {"toy:height", "toy:kind"});
    Table a;
    a.collection = "Agents";
    a.idPrefix = "A_";
    a.fields = {{"toy:x", FieldType::Int}, {"toy:y", FieldType::Int}, {"toy:energy", FieldType::Double}};
    if (version >= 2) a.fields.push_back({"toy:tag", FieldType::Int64});
    a.initColumns();
    Table p;
    p.collection = "Props";
    p.idPrefix = "P_";
    p.fields = {{"toy:kind", FieldType::Int}};
    p.initColumns();
    w.tables = {a, p};
    w.variants[kMood] = "";
    return w;
}

World generate(uint64_t seed, int gw, int gh, int agents, int version) {
    World w = schema(version);
    w.grid.init(gw, gh, 16, w.grid.channels);
    w.globals["toy:tick"] = 0;
    w.globals["toy:rng"] = static_cast<int64_t>(mix(seed));
    for (size_t i = 0; i < w.grid.data[0].size(); ++i) {
        w.grid.data[0][i] = static_cast<int32_t>(next(w) % 100);
        w.grid.data[1][i] = static_cast<int32_t>(next(w) % 4);
    }
    Table& A = w.table("Agents");
    for (int k = 0; k < agents; ++k) {
        const size_t r = A.addRow(k, "/Prefabs/Agent");
        A.ints[0][r] = static_cast<int64_t>(next(w) % static_cast<uint64_t>(gw));
        A.ints[1][r] = static_cast<int64_t>(next(w) % static_cast<uint64_t>(gh));
        A.dbls[2][r] = static_cast<double>(next(w) % 1000) / 7.0;
        if (version >= 2) A.ints[3][r] = static_cast<int64_t>(next(w));
    }
    w.globals["toy:nextAgent"] = agents;
    Table& P = w.table("Props");
    for (int k = 0; k < 3; ++k) {
        const size_t r = P.addRow(k, "/Prefabs/Prop");
        P.ints[0][r] = k;
    }
    w.globals["toy:nextProp"] = 3;
    if (version >= 2) w.globals["toy:migratedAt"] = -1;
    w.variants[kMood] = "calm";
    return w;
}

void step(World& w) {
    Table& A = w.table("Agents");
    const bool v2 = A.fields.size() > 3;
    const int64_t tick = w.globals["toy:tick"];
    for (size_t i = 0; i < A.size(); ++i) {
        const uint64_t r = next(w);
        A.ints[0][i] = std::clamp<int64_t>(A.ints[0][i] + static_cast<int64_t>(r % 3) - 1, 0, w.grid.w - 1);
        A.ints[1][i] = std::clamp<int64_t>(A.ints[1][i] + static_cast<int64_t>((r >> 8) % 3) - 1, 0, w.grid.h - 1);
        A.dbls[2][i] = A.dbls[2][i] * 0.99 + 0.25;
        if (v2) A.ints[3][i] ^= static_cast<int64_t>(r);
    }
    if (tick % 7 == 0) {
        const size_t r = A.addRow(w.globals["toy:nextAgent"]++, "/Prefabs/Agent");
        A.dbls[2][r] = 1.0 / 3.0;
    }
    if (tick % 11 == 0 && A.size() > 3) A.removeRow(next(w) % A.size());
    if (tick % 13 == 0) {
        Table& P = w.table("Props");
        const size_t r = P.addRow(w.globals["toy:nextProp"]++, "/Prefabs/Prop");
        P.ints[0][r] = static_cast<int64_t>(next(w) % 5);
    }
    if (!w.grid.data.empty() && !w.grid.data[0].empty())
        w.grid.data[0][next(w) % w.grid.data[0].size()] = static_cast<int32_t>(tick % 1000);
    if (tick % 50 == 49) w.variants[kMood] = w.variants[kMood] == "calm" ? "angry" : "calm";
    w.globals["toy:tick"] = tick + 1;
}

strata::save::Schema saveSchema(int version) {
    strata::save::Schema s;
    s.gameId = kGameId;
    s.version = version;
    s.templateFor = [](int v) { return schema(v); };
    s.upgrade[1] = [](World& w, std::string* err) {
        // v1 -> v2: agents gain a tag (derived from the id), the world records when it migrated.
        Table* A = nullptr;
        for (auto& t : w.tables)
            if (t.collection == "Agents") A = &t;
        if (!A || A->fields.size() != 3) {
            if (err) *err = "not a v1 world";
            return false;
        }
        A->fields.push_back({"toy:tag", FieldType::Int64});
        A->ints.emplace_back();
        A->dbls.emplace_back();
        for (size_t r = 0; r < A->size(); ++r) A->ints[3].push_back(A->ids[r] * 1000003 + 7);
        w.globals["toy:migratedAt"] = w.globals.count("toy:tick") ? w.globals["toy:tick"] : 0;
        return true;
    };
    return s;
}

strata::save::Layers layers(bool fast, bool heavy) {
    strata::save::Layers l;
    l.content = {dataDir() + "/prefabs.usda"};
    if (fast) l.mods.push_back({"fast", {dataDir() + "/mods/fast.usda"}});
    if (heavy) l.mods.push_back({"heavy", {dataDir() + "/mods/heavy.usda"}});
    return l;
}

}  // namespace toy
