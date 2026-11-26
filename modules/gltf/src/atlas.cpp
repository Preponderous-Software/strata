#include "atlas.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

#include "strata/gltf/model.hpp"

namespace strata::gltf {

AtlasImage resizeImage(const AtlasImage& in, int width, int height) {
    AtlasImage out;
    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
    const double sx = static_cast<double>(in.width) / width, sy = static_cast<double>(in.height) / height;
    for (int y = 0; y < height; ++y) {
        int y0 = static_cast<int>(std::floor(y * sy));
        int y1 = std::max(y0 + 1, static_cast<int>(std::ceil((y + 1) * sy)));
        y0 = std::clamp(y0, 0, in.height - 1);
        y1 = std::clamp(y1, y0 + 1, in.height);
        for (int x = 0; x < width; ++x) {
            int x0 = static_cast<int>(std::floor(x * sx));
            int x1 = std::max(x0 + 1, static_cast<int>(std::ceil((x + 1) * sx)));
            x0 = std::clamp(x0, 0, in.width - 1);
            x1 = std::clamp(x1, x0 + 1, in.width);
            uint32_t sum[4] = {0, 0, 0, 0};
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t* row = &in.rgba[(static_cast<size_t>(yy) * static_cast<size_t>(in.width)) * 4];
                for (int xx = x0; xx < x1; ++xx) {
                    for (size_t k = 0; k < 4; ++k) sum[k] += row[static_cast<size_t>(xx) * 4 + k];
                }
            }
            const uint32_t n = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
            uint8_t* o = &out.rgba[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4];
            for (size_t k = 0; k < 4; ++k) o[k] = static_cast<uint8_t>((sum[k] + n / 2) / n);
        }
    }
    return out;
}

namespace {

int nextPowerOfTwo(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

struct Packed {
    std::vector<AtlasPlacement> placements;
    std::vector<std::pair<int, int>> used;  // per page: furthest x, y reached
};

// Shelf packing at one scale. Returns false when more than maxPages are needed.
bool shelfPack(const std::vector<AtlasInput>& inputs, int pageSize, int maxPages, double scale, Packed& out) {
    const int g = kAtlasGutter;
    const int inner = pageSize - 2 * g;
    out.placements.assign(inputs.size(), AtlasPlacement{});
    for (size_t i = 0; i < inputs.size(); ++i) {
        const AtlasImage& im = *inputs[i].image;
        out.placements[i].width = std::clamp(static_cast<int>(std::lround(im.width * scale)), 1, inner);
        out.placements[i].height = std::clamp(static_cast<int>(std::lround(im.height * scale)), 1, inner);
    }
    std::vector<size_t> order(inputs.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return out.placements[a].height > out.placements[b].height;
    });
    out.used.assign(1, {0, 0});
    int page = 0, x = 0, y = 0, shelf = 0;
    for (size_t i : order) {
        AtlasPlacement& p = out.placements[i];
        const int w = p.width + 2 * g, h = p.height + 2 * g;
        if (x + w > pageSize) {  // next shelf
            y += shelf;
            x = 0;
            shelf = 0;
        }
        if (y + h > pageSize) {  // next page
            if (++page >= maxPages) return false;
            out.used.emplace_back(0, 0);
            x = y = shelf = 0;
        }
        p.page = page;
        p.x = x + g;
        p.y = y + g;
        x += w;
        shelf = std::max(shelf, h);
        out.used[static_cast<size_t>(page)].first = std::max(out.used[static_cast<size_t>(page)].first, x);
        out.used[static_cast<size_t>(page)].second = std::max(out.used[static_cast<size_t>(page)].second, y + h);
    }
    return true;
}

}  // namespace

AtlasResult packAtlas(const std::vector<AtlasInput>& inputs, int pageSize, int maxPages) {
    AtlasResult result;
    Packed packed;
    double scale = 1.0;
    while (!shelfPack(inputs, pageSize, maxPages, scale, packed)) {
        scale *= 0.8;
        if (scale < 1e-6) {
            throw Error(Error::Kind::Unsupported, std::to_string(inputs.size()) + " textures do not fit on " +
                                                      std::to_string(maxPages) + " atlas page(s) even at one texel each");
        }
    }
    result.scale = static_cast<float>(scale);
    result.placements = packed.placements;
    result.pages.resize(packed.used.size());
    for (size_t p = 0; p < packed.used.size(); ++p) {
        AtlasImage& page = result.pages[p];
        page.width = std::min(pageSize, nextPowerOfTwo(packed.used[p].first));
        page.height = std::min(pageSize, nextPowerOfTwo(packed.used[p].second));
        page.rgba.assign(static_cast<size_t>(page.width) * static_cast<size_t>(page.height) * 4, 0);
    }
    const int g = kAtlasGutter;
    for (size_t i = 0; i < inputs.size(); ++i) {
        const AtlasPlacement& pl = result.placements[i];
        const AtlasImage& src = *inputs[i].image;
        const AtlasImage scaled = (pl.width == src.width && pl.height == src.height) ? src : resizeImage(src, pl.width, pl.height);
        AtlasImage& page = result.pages[static_cast<size_t>(pl.page)];
        // The image and its gutter, the gutter repeating the nearest edge texel.
        for (int y = -g; y < pl.height + g; ++y) {
            const int sy = std::clamp(y, 0, pl.height - 1);
            const int py = pl.y + y;
            if (py < 0 || py >= page.height) continue;
            for (int x = -g; x < pl.width + g; ++x) {
                const int sx = std::clamp(x, 0, pl.width - 1);
                const int px = pl.x + x;
                if (px < 0 || px >= page.width) continue;
                const uint8_t* s = &scaled.rgba[(static_cast<size_t>(sy) * static_cast<size_t>(pl.width) + static_cast<size_t>(sx)) * 4];
                uint8_t* d = &page.rgba[(static_cast<size_t>(py) * static_cast<size_t>(page.width) + static_cast<size_t>(px)) * 4];
                std::copy(s, s + 4, d);
            }
        }
    }
    return result;
}

}  // namespace strata::gltf
