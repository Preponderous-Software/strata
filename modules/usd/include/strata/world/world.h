// Strata flat world: the game-agnostic runtime state that the USD sync points
// (modules/usd) and the save core (modules/save) move to and from USD.
//
// RFC 0019 §1: a world is structure-of-arrays entity tables, a chunked grid of
// int channels, int64 globals and variant selections. The engine knows none of
// the names; a game declares them (in its own USD namespace, e.g. "colony:x")
// and converts its components to and from this shape at sync points.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace strata::world {

enum class FieldType { Int, Int64, Double };

struct FieldDef {
    std::string attr;  // full USD attribute name, e.g. "colony:x"
    FieldType type;
};

// One collection of entities stored as columns. Rows are sorted by id, and an
// id is never reused, so deltas can merge-join on ids.
struct Table {
    std::string collection;  // child prim of /World/Entities, e.g. "Settlers"
    std::string idPrefix;    // prim-name prefix, e.g. "S_"
    std::vector<FieldDef> fields;

    std::vector<int64_t> ids;
    std::vector<std::string> prefab;          // per row: referenced prefab path, e.g. "/Prefabs/Settler"
    std::vector<std::vector<int64_t>> ints;   // [field][row]; empty for Double fields
    std::vector<std::vector<double>> dbls;    // [field][row]; empty for Int/Int64 fields

    size_t size() const { return ids.size(); }
    int field(const std::string& attr) const;  // throws std::out_of_range
    void initColumns();
    size_t addRow(int64_t id, const std::string& prefabPath);  // id must exceed every existing id
    void removeRow(size_t row);
    void clearRows();
    std::string primName(int64_t id) const;  // idPrefix + zero-padded id ("S_000042")
};

struct Grid {
    int32_t w = 0, h = 0, chunk = 16;
    std::vector<std::string> channels;       // attr names, e.g. "colony:terrain"
    std::vector<std::vector<int32_t>> data;  // [channel][y * w + x]
    int channel(const std::string& attr) const;  // throws std::out_of_range
    void init(int32_t width, int32_t height, int32_t chunkSize, std::vector<std::string> channelNames);
};

struct World {
    Grid grid;
    std::vector<Table> tables;
    std::map<std::string, int64_t> globals;       // attr -> value, on /World/Globals
    std::map<std::string, std::string> variants;  // "<primPath>|<variantSet>" -> selection ("" = none)

    Table& table(const std::string& collection);  // throws std::out_of_range
    const Table& table(const std::string& collection) const;
};

// Exact equality: every id, prefab, field (doubles compared BIT-wise, so -0.0,
// NaN payloads and infinities must survive a round trip), grid cell, global
// and variant selection. On a difference, `why` names the first one.
bool equal(const World& a, const World& b, std::string* why = nullptr);

// Same schema (tables, fields, channels, variant keys) with no rows, no
// globals and empty variant selections: the template hydrate fills.
World emptyLike(const World& w);

}  // namespace strata::world
