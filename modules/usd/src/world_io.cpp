#include "strata/usd/world_io.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>

#include <pxr/base/tf/errorMark.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/variantSets.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace strata::usd {

using world::FieldType;
using world::Grid;
using world::Table;
using world::World;

namespace {

void setErr(std::string* e, const std::string& s) {
    if (e) *e = s;
}

std::string tfErrors(const TfErrorMark& m) {
    std::string s;
    for (auto it = m.GetBegin(); it != m.GetEnd(); ++it) s += it->GetCommentary() + "; ";
    return s;
}

bool fileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::u8path(p), ec);
}

std::string chunkName(int cx, int cy) {
    char b[32];
    std::snprintf(b, sizeof b, "C_%03d_%03d", cx, cy);
    return b;
}

const SdfValueTypeName& sdfType(FieldType t) {
    switch (t) {
        case FieldType::Int: return SdfValueTypeNames->Int;
        case FieldType::Int64: return SdfValueTypeNames->Int64;
        case FieldType::Double: break;
    }
    return SdfValueTypeNames->Double;
}

VtValue fieldValue(const Table& t, size_t f, size_t row) {
    switch (t.fields[f].type) {
        case FieldType::Int: {
            const int64_t v = t.ints[f][row];
            if (v < std::numeric_limits<int32_t>::min() || v > std::numeric_limits<int32_t>::max())
                throw std::runtime_error(t.collection + "." + t.fields[f].attr + " = " + std::to_string(v) +
                                         " does not fit an Int field");
            return VtValue(static_cast<int>(v));
        }
        case FieldType::Int64: return VtValue(t.ints[f][row]);
        case FieldType::Double: break;
    }
    return VtValue(t.dbls[f][row]);
}

void setAttr(const SdfPrimSpecHandle& p, const std::string& name, const SdfValueTypeName& type, const VtValue& v) {
    SdfAttributeSpecHandle a = SdfAttributeSpec::New(p, name, type);
    if (!a) throw std::runtime_error("cannot author attribute " + name + " on " + p->GetPath().GetString());
    a->SetDefaultValue(v);
}

SdfPrimSpecHandle ensurePrim(const SdfLayerRefPtr& L, const std::string& path, bool def, const std::string& type = "") {
    SdfPrimSpecHandle p = SdfCreatePrimInLayer(L, SdfPath(path));
    if (!p) throw std::runtime_error("cannot create prim " + path);
    if (def) {
        p->SetSpecifier(SdfSpecifierDef);
        if (!type.empty()) p->SetTypeName(type);
    }
    return p;
}

VtIntArray chunkArray(const Grid& g, size_t c, int cx, int cy) {
    VtIntArray a;
    a.reserve(static_cast<size_t>(g.chunk) * static_cast<size_t>(g.chunk));
    for (int y = cy * g.chunk; y < std::min(g.h, (cy + 1) * g.chunk); ++y)
        for (int x = cx * g.chunk; x < std::min(g.w, (cx + 1) * g.chunk); ++x)
            a.push_back(g.data[c][static_cast<size_t>(y) * g.w + x]);
    return a;
}

bool chunkEqual(const Grid& a, const Grid& b, size_t c, int cx, int cy) {
    const int x0 = cx * a.chunk, x1 = std::min(a.w, (cx + 1) * a.chunk);
    for (int y = cy * a.chunk; y < std::min(a.h, (cy + 1) * a.chunk); ++y) {
        const size_t o = static_cast<size_t>(y) * a.w;
        if (!std::equal(a.data[c].begin() + o + x0, a.data[c].begin() + o + x1, b.data[c].begin() + o + x0))
            return false;
    }
    return true;
}

bool sameDouble(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

SdfLayerRefPtr writeLayer(const World* base, const World& cur) {
    const bool full = base == nullptr;
    SdfLayerRefPtr L = SdfLayer::CreateAnonymous("world.usdc");
    SdfChangeBlock block;
    const Grid& g = cur.grid;
    if (full) {
        ensurePrim(L, "/World", true, "Xform");
        ensurePrim(L, "/World/Entities", true, "Scope");
        auto gp = ensurePrim(L, "/World/Grid", true, "Scope");
        setAttr(gp, "eng:grid:width", SdfValueTypeNames->Int, VtValue(static_cast<int>(g.w)));
        setAttr(gp, "eng:grid:height", SdfValueTypeNames->Int, VtValue(static_cast<int>(g.h)));
        setAttr(gp, "eng:grid:chunk", SdfValueTypeNames->Int, VtValue(static_cast<int>(g.chunk)));
        ensurePrim(L, "/World/Globals", true, "Scope");
        L->SetDefaultPrim(TfToken("World"));
    } else if (base->grid.w != g.w || base->grid.h != g.h || base->grid.chunk != g.chunk ||
               base->grid.channels != g.channels) {
        throw std::runtime_error("grid shape changed; delta saves cannot express that");
    }
    const int ncx = g.chunk > 0 ? (g.w + g.chunk - 1) / g.chunk : 0;
    const int ncy = g.chunk > 0 ? (g.h + g.chunk - 1) / g.chunk : 0;
    for (int cy = 0; cy < ncy; ++cy)
        for (int cx = 0; cx < ncx; ++cx) {
            SdfPrimSpecHandle p;
            for (size_t c = 0; c < g.channels.size(); ++c) {
                if (!full && chunkEqual(base->grid, g, c, cx, cy)) continue;
                if (!p) p = ensurePrim(L, "/World/Grid/" + chunkName(cx, cy), full, "Scope");
                setAttr(p, g.channels[c], SdfValueTypeNames->IntArray, VtValue(chunkArray(g, c, cx, cy)));
            }
        }
    SdfPrimSpecHandle gp;
    for (const auto& kv : cur.globals) {
        if (!full) {
            auto it = base->globals.find(kv.first);
            if (it != base->globals.end() && it->second == kv.second) continue;
        }
        if (!gp) gp = ensurePrim(L, "/World/Globals", full, "Scope");
        setAttr(gp, kv.first, SdfValueTypeNames->Int64, VtValue(kv.second));
    }
    if (!full)
        for (const auto& kv : base->globals)
            if (!cur.globals.count(kv.first))
                throw std::runtime_error("global " + kv.first + " was removed; delta saves cannot express that");
    for (size_t ti = 0; ti < cur.tables.size(); ++ti) {
        const Table& t = cur.tables[ti];
        const std::string coll = "/World/Entities/" + t.collection + "/";
        if (full) ensurePrim(L, "/World/Entities/" + t.collection, true, "Scope");
        auto writeNew = [&](size_t r) {
            auto p = ensurePrim(L, coll + t.primName(t.ids[r]), true, "Xform");
            if (!t.prefab[r].empty()) p->GetReferenceList().Prepend(SdfReference(std::string(), SdfPath(t.prefab[r])));
            for (size_t f = 0; f < t.fields.size(); ++f)
                setAttr(p, t.fields[f].attr, sdfType(t.fields[f].type), fieldValue(t, f, r));
        };
        if (full) {
            for (size_t r = 0; r < t.size(); ++r) writeNew(r);
            continue;
        }
        if (ti >= base->tables.size() || base->tables[ti].collection != t.collection)
            throw std::runtime_error("table layout differs from the baseline");
        const Table& b = base->tables[ti];
        size_t i = 0, j = 0;
        while (i < b.size() || j < t.size()) {
            if (j >= t.size() || (i < b.size() && b.ids[i] < t.ids[j])) {  // deleted
                ensurePrim(L, coll + b.primName(b.ids[i]), false)->SetActive(false);
                ++i;
            } else if (i >= b.size() || t.ids[j] < b.ids[i]) {  // new
                writeNew(j);
                ++j;
            } else {  // in both: changed fields only
                if (b.prefab[i] != t.prefab[j])
                    throw std::runtime_error("entity " + t.primName(t.ids[j]) + " changed prefab; not supported");
                SdfPrimSpecHandle p;
                for (size_t f = 0; f < t.fields.size(); ++f) {
                    const bool dbl = t.fields[f].type == FieldType::Double;
                    if (dbl ? sameDouble(b.dbls[f][i], t.dbls[f][j]) : b.ints[f][i] == t.ints[f][j]) continue;
                    if (!p) p = ensurePrim(L, coll + t.primName(t.ids[j]), false);
                    setAttr(p, t.fields[f].attr, sdfType(t.fields[f].type), fieldValue(t, f, j));
                }
                ++i;
                ++j;
            }
        }
    }
    for (const auto& kv : cur.variants) {
        if (kv.second.empty()) continue;
        if (!full) {
            auto it = base->variants.find(kv.first);
            if (it != base->variants.end() && it->second == kv.second) continue;
        }
        const auto bar = kv.first.find('|');
        if (bar == std::string::npos) throw std::runtime_error("variant key needs <prim>|<set>: " + kv.first);
        ensurePrim(L, kv.first.substr(0, bar), false)->SetVariantSelection(kv.first.substr(bar + 1), kv.second);
    }
    return L;
}

}  // namespace

UsdStageRefPtr compose(const std::vector<std::string>& layersWeakestFirst, std::string* err) {
    TfErrorMark mark;
    std::vector<SdfLayerRefPtr> held;
    std::vector<std::string> ids;
    for (auto it = layersWeakestFirst.rbegin(); it != layersWeakestFirst.rend(); ++it) {
        const std::string& p = *it;
        if (!fileExists(p)) {
            setErr(err, "missing layer " + p);
            return nullptr;
        }
        // Anonymous: never served from (or entered into) the Sdf registry, so a
        // stale in-memory copy can never stand in for the file on disk.
        SdfLayerRefPtr l = SdfLayer::OpenAsAnonymous(p);
        if (!l || !mark.IsClean()) {
            setErr(err, "unreadable layer " + p + ": " + tfErrors(mark));
            mark.Clear();
            return nullptr;
        }
        held.push_back(l);
        ids.push_back(l->GetIdentifier());
    }
    SdfLayerRefPtr root = SdfLayer::CreateAnonymous("root.usda");
    root->SetSubLayerPaths(ids);  // strongest first, as USD lists them
    UsdStageRefPtr stage = UsdStage::Open(root, UsdStage::LoadAll);
    if (!stage || !mark.IsClean()) {
        setErr(err, "compose failed: " + tfErrors(mark));
        mark.Clear();
        return nullptr;
    }
    // Belt and braces: every sublayer must actually be in the composed stack.
    if (stage->GetLayerStack(false).size() < held.size() + 1) {
        setErr(err, "compose dropped a layer");
        return nullptr;
    }
    return stage;
}

bool hydrate(const UsdStageRefPtr& stage, World& out, std::string* err) {
    TfErrorMark mark;
    auto fail = [&](const std::string& s) {
        setErr(err, s);
        mark.Clear();
        return false;
    };
    UsdPrim gp = stage->GetPrimAtPath(SdfPath("/World/Grid"));
    if (!gp) return fail("no /World/Grid");
    int w = 0, h = 0, chunk = 0;
    if (!gp.GetAttribute(TfToken("eng:grid:width")).Get(&w) || !gp.GetAttribute(TfToken("eng:grid:height")).Get(&h) ||
        !gp.GetAttribute(TfToken("eng:grid:chunk")).Get(&chunk))
        return fail("grid header missing");
    if (w < 0 || h < 0 || chunk <= 0 || w > 1 << 16 || h > 1 << 16 || chunk > 1024) return fail("bad grid header");
    out.grid.init(w, h, chunk, out.grid.channels);
    const int ncx = (w + chunk - 1) / chunk, ncy = (h + chunk - 1) / chunk;
    for (int cy = 0; cy < ncy; ++cy)
        for (int cx = 0; cx < ncx; ++cx) {
            UsdPrim cp = gp.GetChild(TfToken(chunkName(cx, cy)));
            if (!cp) return fail("missing chunk " + chunkName(cx, cy));
            const size_t want = static_cast<size_t>(std::min(w, (cx + 1) * chunk) - cx * chunk) *
                                static_cast<size_t>(std::min(h, (cy + 1) * chunk) - cy * chunk);
            for (size_t c = 0; c < out.grid.channels.size(); ++c) {
                VtIntArray a;
                if (!cp.GetAttribute(TfToken(out.grid.channels[c])).Get(&a))
                    return fail("missing channel " + out.grid.channels[c] + " in " + chunkName(cx, cy));
                if (a.size() != want) return fail("chunk " + chunkName(cx, cy) + " has the wrong size");
                size_t k = 0;
                for (int y = cy * chunk; y < std::min(h, (cy + 1) * chunk); ++y)
                    for (int x = cx * chunk; x < std::min(w, (cx + 1) * chunk); ++x)
                        out.grid.data[c][static_cast<size_t>(y) * w + x] = a[k++];
            }
        }
    out.globals.clear();
    if (UsdPrim glob = stage->GetPrimAtPath(SdfPath("/World/Globals")))
        for (const auto& a : glob.GetAuthoredAttributes()) {
            int64_t v = 0;
            if (!a.Get(&v)) return fail("global " + a.GetName().GetString() + " is not an int64");
            out.globals[a.GetName().GetString()] = v;
        }
    for (auto& t : out.tables) {
        t.clearRows();
        UsdPrim cp = stage->GetPrimAtPath(SdfPath("/World/Entities/" + t.collection));
        if (!cp) continue;
        std::vector<std::pair<int64_t, UsdPrim>> rows;
        for (const UsdPrim& p : cp.GetChildren()) {  // active children only: deleted entities are inactive
            const std::string& n = p.GetName().GetString();
            if (n.compare(0, t.idPrefix.size(), t.idPrefix) != 0) return fail("unexpected prim " + p.GetPath().GetString());
            const std::string digits = n.substr(t.idPrefix.size());
            if (digits.empty() || digits.size() > 18 || digits.find_first_not_of("0123456789") != std::string::npos)
                return fail("bad entity name " + n);
            rows.emplace_back(std::stoll(digits), p);
        }
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t k = 1; k < rows.size(); ++k)
            if (rows[k].first == rows[k - 1].first) return fail("duplicate entity id in " + t.collection);
        std::vector<TfToken> names;
        for (const auto& f : t.fields) names.emplace_back(f.attr);
        for (const auto& rp : rows) {
            std::string prefab;
            for (const SdfPrimSpecHandle& s : rp.second.GetPrimStack()) {
                auto items = s->GetReferenceList().GetPrependedItems();
                if (!items.empty()) {
                    prefab = SdfReference(items[0]).GetPrimPath().GetString();
                    break;
                }
            }
            const size_t r = t.addRow(rp.first, prefab);
            for (size_t f = 0; f < t.fields.size(); ++f) {
                UsdAttribute a = rp.second.GetAttribute(names[f]);
                bool ok = false;
                switch (t.fields[f].type) {
                    case FieldType::Int: { int v = 0; ok = a.Get(&v); t.ints[f][r] = v; break; }
                    case FieldType::Int64: { int64_t v = 0; ok = a.Get(&v); t.ints[f][r] = v; break; }
                    case FieldType::Double: { double v = 0; ok = a.Get(&v); t.dbls[f][r] = v; break; }
                }
                if (!ok) return fail("missing or mistyped " + t.fields[f].attr + " on " + rp.second.GetPath().GetString());
            }
        }
    }
    for (auto& kv : out.variants) {
        const auto bar = kv.first.find('|');
        if (bar == std::string::npos) return fail("variant key needs <prim>|<set>: " + kv.first);
        UsdPrim p = stage->GetPrimAtPath(SdfPath(kv.first.substr(0, bar)));
        kv.second = p ? p.GetVariantSet(kv.first.substr(bar + 1)).GetVariantSelection() : "";
    }
    if (!mark.IsClean()) return fail("hydrate errors: " + tfErrors(mark));
    return true;
}

void writeManifest(const SdfLayerRefPtr& layer, const Manifest& m) {
    VtDictionary d;
    d["eng:formatVersion"] = VtValue(m.formatVersion);
    d["eng:gameId"] = VtValue(m.gameId);
    d["eng:gameSchemaVersion"] = VtValue(m.gameSchemaVersion);
    d["eng:baseHash"] = VtValue(m.baseHash);
    d["eng:mods"] = VtValue(VtStringArray(m.mods.begin(), m.mods.end()));
    d["eng:tick"] = VtValue(m.tick);
    layer->SetCustomLayerData(d);
}

bool readManifest(const SdfLayerRefPtr& layer, Manifest& m, std::string* err) {
    const VtDictionary d = layer->GetCustomLayerData();
    auto get = [&](const char* k) {
        auto it = d.find(k);
        return it == d.end() ? VtValue() : it->second;
    };
    // Bind each value to a named VtValue: UncheckedGet on a temporary takes
    // VtValue's move path, which GCC 13 flags as maybe-uninitialized.
    const VtValue format = get("eng:formatVersion"), game = get("eng:gameId"), schema = get("eng:gameSchemaVersion"),
                  base = get("eng:baseHash"), mods = get("eng:mods"), tick = get("eng:tick");
    if (!format.IsHolding<int>() || !game.IsHolding<std::string>() || !schema.IsHolding<int>() ||
        !base.IsHolding<std::string>() || !mods.IsHolding<VtStringArray>() || !tick.IsHolding<int64_t>()) {
        setErr(err, "no valid save manifest in " + layer->GetIdentifier());
        return false;
    }
    m.formatVersion = format.UncheckedGet<int>();
    m.gameId = game.UncheckedGet<std::string>();
    m.gameSchemaVersion = schema.UncheckedGet<int>();
    m.baseHash = base.UncheckedGet<std::string>();
    const auto& list = mods.UncheckedGet<VtStringArray>();
    m.mods.assign(list.begin(), list.end());
    m.tick = tick.UncheckedGet<int64_t>();
    return true;
}

SdfLayerRefPtr writeFull(const World& w) { return writeLayer(nullptr, w); }

SdfLayerRefPtr writeDelta(const World& baseline, const World& current, const Manifest& m) {
    SdfLayerRefPtr L = writeLayer(&baseline, current);
    writeManifest(L, m);
    return L;
}

}  // namespace strata::usd
