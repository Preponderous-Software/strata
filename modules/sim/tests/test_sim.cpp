// Unit tests for the simulation module (modules/sim).
#include <cmath>
#include <cstdint>
#include <vector>

#include "strata/sim/colormap.hpp"
#include "strata/sim/field.hpp"
#include "strata/sim/fixed_step.hpp"
#include "strata/sim/hash.hpp"
#include "strata/sim/registry.hpp"
#include "strata/sim/rng.hpp"
#include "test.hpp"

using namespace strata::sim;

// ---------------------------------------------------------------- Rng

TEST(sim_rng_matches_pcg32_reference) {
    // pcg32_srandom_r(&rng, 42u, 54u) from the PCG reference implementation.
    Rng rng(42u, 54u);
    const uint32_t expected[6] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (uint32_t e : expected) CHECK(rng.nextU32() == e);
}

TEST(sim_rng_helpers_stay_in_range) {
    Rng rng(7u);
    int counts[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 50000; ++i) {
        const uint32_t v = rng.below(5u);
        REQUIRE(v < 5u);
        ++counts[v];
        const float u = rng.uniform();
        CHECK(u >= 0.0f && u < 1.0f);
        const int32_t r = rng.range(-3, 3);
        CHECK(r >= -3 && r <= 3);
    }
    for (int c : counts) CHECK(c > 9000 && c < 11000);
    for (int i = 0; i < 1000; ++i) {
        float x = 0, y = 0;
        rng.unitVector(x, y);
        CHECK_NEAR(x * x + y * y, 1.0, 1e-5);
    }
    CHECK(rng.below(0u) == 0u);
}

TEST(sim_rng_same_seed_same_sequence) {
    Rng a(1234u), b(1234u), c(1235u);
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        const uint32_t va = a.nextU32();
        CHECK(va == b.nextU32());
        differs = differs || va != c.nextU32();
    }
    CHECK(differs);
}

// ---------------------------------------------------------------- Registry

namespace {
struct Pos {
    float x = 0, y = 0;
};
struct Tag {
    int value = 0;
};
using World = Registry<Pos, Tag>;
}  // namespace

TEST(sim_registry_create_destroy_and_generations) {
    World w;
    Entity a = w.create();
    Entity b = w.create();
    CHECK(w.size() == 2);
    w.add(a, Pos{1, 2});
    CHECK(w.has<Pos>(a));
    CHECK(!w.has<Tag>(a));
    CHECK(w.get<Pos>(a)->y == 2.0f);
    w.destroy(a);
    CHECK(!w.alive(a));
    CHECK(w.get<Pos>(a) == nullptr);
    CHECK(w.size() == 1);
    Entity c = w.create();  // recycles a's slot with a new generation
    CHECK(c.index == a.index);
    CHECK(c.generation != a.generation);
    CHECK(!w.has<Pos>(c));
    CHECK(!w.alive(a));
    CHECK(w.alive(b));
    w.destroy(a);  // stale handle: no effect
    CHECK(w.alive(c));
}

TEST(sim_registry_each_is_ordered_and_filtered) {
    World w;
    std::vector<Entity> es;
    for (int i = 0; i < 6; ++i) {
        Entity e = w.create();
        w.add(e, Pos{static_cast<float>(i), 0});
        if (i % 2 == 0) w.add(e, Tag{i});
        es.push_back(e);
    }
    std::vector<int> seen;
    w.each<Pos, Tag>([&](Entity, Pos& p, Tag& t) {
        CHECK(static_cast<int>(p.x) == t.value);
        seen.push_back(t.value);
    });
    REQUIRE(seen.size() == 3);
    CHECK(seen[0] == 0 && seen[1] == 2 && seen[2] == 4);
}

TEST(sim_registry_mutation_during_each_is_deterministic) {
    World w;
    for (int i = 0; i < 4; ++i) w.add(w.create(), Pos{static_cast<float>(i), 0});
    // Destroy slot 1 while visiting slot 0, create two while visiting slot 2.
    std::vector<int> visited;
    w.each<Pos>([&](Entity e, Pos& p) {
        visited.push_back(static_cast<int>(p.x));
        if (e.index == 0) w.destroy(Entity{1, 0});
        if (e.index == 2) {
            w.add(w.create(), Pos{10, 0});
            w.add(w.create(), Pos{11, 0});
        }
    });
    REQUIRE(visited.size() == 3);
    CHECK(visited[0] == 0 && visited[1] == 2 && visited[2] == 3);
    // Created inside each: appended, not recycled into the freed slot 1.
    CHECK(w.capacity() == 6);
    CHECK(w.size() == 5);
    // Outside each the lowest freed slot is reused.
    Entity e = w.create();
    CHECK(e.index == 1);
}

TEST(sim_registry_const_each_reads_in_order) {
    World w;
    for (int i = 0; i < 5; ++i) {
        Entity e = w.create();
        w.add(e, Pos{static_cast<float>(i), 0});
    }
    w.destroy(Entity{2, 0});
    const World& cw = w;
    std::vector<int> seen;
    cw.each<Pos>([&](Entity, const Pos& p) { seen.push_back(static_cast<int>(p.x)); });
    REQUIRE(seen.size() == 4);
    CHECK(seen[0] == 0 && seen[1] == 1 && seen[2] == 3 && seen[3] == 4);
}

// ---------------------------------------------------------------- Field2D

namespace {
Field2D randomField(int w, int h, uint64_t seed) {
    Field2D f(w, h);
    Rng rng(seed);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) f.set(x, y, rng.uniform(0.0f, 10.0f));
    return f;
}
}  // namespace

TEST(sim_field_diffusion_conserves_mass_with_walls) {
    const int w = 64, h = 48;
    Mask2D mask(w, h);
    mask.blockDisc(20.0f, 20.0f, 6.0f);
    mask.blockDisc(45.0f, 30.0f, 4.5f);
    for (int y = 0; y < 30; ++y) mask.setBlocked(32, y, true);  // a wall with a gap
    Field2D f = randomField(w, h, 99u);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (mask.blocked(x, y)) f.set(x, y, 0.0f);
    const double before = f.total();
    for (int i = 0; i < 2000; ++i) f.diffuse(0.24f, &mask);
    const double after = f.total();
    CHECK(std::fabs(after - before) / before < 1e-5);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (mask.blocked(x, y)) CHECK(f.at(x, y) == 0.0f);
}

TEST(sim_field_diffusion_smooths_a_point_source_symmetrically) {
    Field2D f(21, 21);
    f.set(10, 10, 100.0f);
    for (int i = 0; i < 30; ++i) f.diffuse(0.2f, nullptr, 2);
    CHECK(f.at(10, 10) < 100.0f);
    CHECK(f.at(10, 10) > f.at(13, 10));
    CHECK_NEAR(f.at(7, 10), f.at(13, 10), 1e-4);
    CHECK_NEAR(f.at(10, 7), f.at(10, 13), 1e-4);
    CHECK_NEAR(f.total(), 100.0, 1e-3);
}

TEST(sim_field_substeps_repeat_the_step) {
    Mask2D mask(24, 24);
    mask.blockDisc(12.0f, 12.0f, 3.0f);
    Field2D a = randomField(24, 24, 3u), b = randomField(24, 24, 3u);
    a.diffuse(0.2f, &mask, 3);
    for (int i = 0; i < 3; ++i) b.diffuse(0.2f, &mask);
    StateHasher ha, hb;
    a.hashInto(ha);
    b.hashInto(hb);
    CHECK(ha.value() == hb.value());
}

TEST(sim_field_decay_take_and_add) {
    Field2D f(4, 4, 2.0f);
    f.decay(0.25f);
    CHECK_NEAR(f.total(), 24.0, 1e-5);
    CHECK_NEAR(f.take(1, 1, 10.0f), 1.5, 1e-6);
    CHECK(f.at(1, 1) == 0.0f);
    f.add(1, 1, -3.0f);
    CHECK(f.at(1, 1) == 0.0f);
    f.add(2, 2, 1.0f);
    CHECK_NEAR(f.at(2, 2), 2.5, 1e-6);
    CHECK(f.at(-1, 0) == 0.0f);
}

TEST(sim_field_gradient_points_uphill_and_ignores_walls) {
    Field2D f(10, 10);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) f.set(x, y, static_cast<float>(x));
    float gx = 0, gy = 0;
    f.gradient(5.5f, 5.5f, nullptr, gx, gy);
    CHECK_NEAR(gx, 1.0, 1e-6);
    CHECK_NEAR(gy, 0.0, 1e-6);
    Mask2D mask(10, 10);
    mask.setBlocked(6, 5, true);
    f.gradient(5.5f, 5.5f, &mask, gx, gy);
    CHECK_NEAR(gx, 0.5, 1e-6);  // (5 - 4) / 2: the wall contributes the centre value
    CHECK_NEAR(f.sample(5.0f, 5.5f), 4.5, 1e-6);
    CHECK(mask.touchesSurface(5, 5));
    CHECK(!mask.touchesSurface(2, 2));
    CHECK(mask.touchesSurface(0, 4));  // the grid edge is a surface
}

TEST(sim_field_operations_are_deterministic) {
    Mask2D mask(32, 32);
    mask.blockDisc(16.0f, 16.0f, 5.0f);
    Field2D a = randomField(32, 32, 5u), b = randomField(32, 32, 5u);
    for (int i = 0; i < 200; ++i) {
        a.diffuse(0.2f, &mask, 2);
        a.decay(0.001f);
        b.diffuse(0.2f, &mask, 2);
        b.decay(0.001f);
    }
    StateHasher ha, hb;
    a.hashInto(ha);
    b.hashInto(hb);
    CHECK(ha.value() == hb.value());
    b.add(0, 0, 1e-3f);
    StateHasher hc;
    b.hashInto(hc);
    CHECK(hc.value() != ha.value());
}

// ---------------------------------------------------------------- FixedStep

TEST(sim_fixed_step_accumulates_and_caps) {
    FixedStep step(0.25, 4);
    CHECK(step.advance(0.6) == 2);
    CHECK_NEAR(step.alpha(), 0.4, 1e-9);
    CHECK(step.advance(0.0) == 0);
    CHECK(step.advance(10.0) == 4);  // capped; the backlog is dropped
    CHECK(step.advance(0.0) == 0);
    CHECK(step.ticks() == 6);
    step.setTimeScale(0.0);
    CHECK(step.advance(5.0) == 0);
}

// ---------------------------------------------------------------- colormap

TEST(sim_colormap_endpoints_and_layout) {
    float lo[3], hi[3];
    colormapRgb(Colormap::Grey, 0.0f, lo);
    colormapRgb(Colormap::Grey, 2.0f, hi);  // clamped
    CHECK(lo[0] == 0.0f && hi[0] == 1.0f);
    Field2D f(3, 2);
    f.set(2, 1, 1.0f);
    Mask2D mask(3, 2);
    mask.setBlocked(0, 0, true);
    const float wall[3] = {0.5f, 0.25f, 0.125f};
    std::vector<float> rgb;
    fieldToColors(f, Colormap::Grey, 0.0f, 1.0f, rgb, &mask, wall);
    REQUIRE(rgb.size() == 18);
    CHECK(rgb[0] == 0.5f && rgb[1] == 0.25f);
    CHECK(rgb[15] == 1.0f);  // cell (2, 1) is the sixth triple
    CHECK(rgb[3] == 0.0f);
}
