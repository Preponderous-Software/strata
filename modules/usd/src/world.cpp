#include "strata/world/world.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace strata::world {

namespace {
bool sameBits(const std::vector<double>& a, const std::vector<double>& b, size_t* at) {
    if (a.size() != b.size()) {
        *at = std::min(a.size(), b.size());
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(double)) != 0) {
            *at = i;
            return false;
        }
    return true;
}
}  // namespace

int Table::field(const std::string& attr) const {
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i].attr == attr) return static_cast<int>(i);
    throw std::out_of_range("no field " + attr + " in table " + collection);
}

void Table::initColumns() {
    ints.assign(fields.size(), {});
    dbls.assign(fields.size(), {});
}

size_t Table::addRow(int64_t id, const std::string& prefabPath) {
    if (!ids.empty() && id <= ids.back()) throw std::logic_error("table " + collection + ": ids must increase");
    if (ints.size() != fields.size()) initColumns();
    ids.push_back(id);
    prefab.push_back(prefabPath);
    for (size_t f = 0; f < fields.size(); ++f) {
        if (fields[f].type == FieldType::Double) dbls[f].push_back(0.0);
        else ints[f].push_back(0);
    }
    return ids.size() - 1;
}

void Table::removeRow(size_t row) {
    const auto r = static_cast<std::ptrdiff_t>(row);
    ids.erase(ids.begin() + r);
    prefab.erase(prefab.begin() + r);
    for (size_t f = 0; f < fields.size(); ++f) {
        if (fields[f].type == FieldType::Double) dbls[f].erase(dbls[f].begin() + r);
        else ints[f].erase(ints[f].begin() + r);
    }
}

void Table::clearRows() {
    ids.clear();
    prefab.clear();
    initColumns();
}

std::string Table::primName(int64_t id) const {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%06lld", static_cast<long long>(id));
    return idPrefix + buf;
}

int Grid::channel(const std::string& attr) const {
    for (size_t i = 0; i < channels.size(); ++i)
        if (channels[i] == attr) return static_cast<int>(i);
    throw std::out_of_range("no grid channel " + attr);
}

void Grid::init(int32_t width, int32_t height, int32_t chunkSize, std::vector<std::string> channelNames) {
    if (width < 0 || height < 0 || chunkSize <= 0) throw std::invalid_argument("bad grid shape");
    w = width;
    h = height;
    chunk = chunkSize;
    channels = std::move(channelNames);
    data.assign(channels.size(), std::vector<int32_t>(static_cast<size_t>(w) * static_cast<size_t>(h), 0));
}

Table& World::table(const std::string& c) {
    for (auto& t : tables)
        if (t.collection == c) return t;
    throw std::out_of_range("no table " + c);
}

const Table& World::table(const std::string& c) const {
    for (const auto& t : tables)
        if (t.collection == c) return t;
    throw std::out_of_range("no table " + c);
}

bool equal(const World& a, const World& b, std::string* why) {
    auto fail = [&](const std::string& s) {
        if (why) *why = s;
        return false;
    };
    if (a.grid.w != b.grid.w || a.grid.h != b.grid.h || a.grid.chunk != b.grid.chunk ||
        a.grid.channels != b.grid.channels)
        return fail("grid shape");
    for (size_t c = 0; c < a.grid.data.size(); ++c)
        if (a.grid.data[c] != b.grid.data[c]) {
            for (size_t i = 0; i < a.grid.data[c].size(); ++i)
                if (a.grid.data[c][i] != b.grid.data[c][i]) {
                    std::ostringstream o;
                    o << "grid " << a.grid.channels[c] << " cell " << i << ": " << a.grid.data[c][i] << " vs "
                      << b.grid.data[c][i];
                    return fail(o.str());
                }
            return fail("grid " + a.grid.channels[c] + " size");
        }
    if (a.globals != b.globals) {
        for (const auto& kv : a.globals) {
            auto it = b.globals.find(kv.first);
            if (it == b.globals.end()) return fail("global " + kv.first + " missing");
            if (it->second != kv.second)
                return fail("global " + kv.first + ": " + std::to_string(kv.second) + " vs " +
                            std::to_string(it->second));
        }
        return fail("globals: extra key");
    }
    if (a.variants != b.variants) {
        for (const auto& kv : a.variants) {
            auto it = b.variants.find(kv.first);
            if (it == b.variants.end() || it->second != kv.second) return fail("variant " + kv.first);
        }
        return fail("variants: extra key");
    }
    if (a.tables.size() != b.tables.size()) return fail("table count");
    for (size_t t = 0; t < a.tables.size(); ++t) {
        const Table& x = a.tables[t];
        const Table& y = b.tables[t];
        if (x.collection != y.collection || x.idPrefix != y.idPrefix) return fail("table identity at " + std::to_string(t));
        if (x.fields.size() != y.fields.size()) return fail(x.collection + ": field count");
        for (size_t f = 0; f < x.fields.size(); ++f)
            if (x.fields[f].attr != y.fields[f].attr || x.fields[f].type != y.fields[f].type)
                return fail(x.collection + ": field " + std::to_string(f) + " definition");
        if (x.ids != y.ids)
            return fail(x.collection + ".ids (" + std::to_string(x.ids.size()) + " vs " + std::to_string(y.ids.size()) +
                        " rows)");
        if (x.prefab != y.prefab) return fail(x.collection + ".prefab");
        for (size_t f = 0; f < x.fields.size(); ++f) {
            if (x.fields[f].type == FieldType::Double) {
                size_t at = 0;
                if (!sameBits(x.dbls[f], y.dbls[f], &at))
                    return fail(x.collection + "." + x.fields[f].attr + " row " + std::to_string(at) + " (bits)");
            } else if (x.ints[f] != y.ints[f]) {
                return fail(x.collection + "." + x.fields[f].attr);
            }
        }
    }
    return true;
}

World emptyLike(const World& w) {
    World e;
    e.grid.init(0, 0, w.grid.chunk, w.grid.channels);
    for (const auto& t : w.tables) {
        Table c;
        c.collection = t.collection;
        c.idPrefix = t.idPrefix;
        c.fields = t.fields;
        c.initColumns();
        e.tables.push_back(std::move(c));
    }
    for (const auto& kv : w.variants) e.variants[kv.first] = "";
    return e;
}

}  // namespace strata::world
