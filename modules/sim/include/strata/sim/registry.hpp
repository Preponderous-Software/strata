// strata/sim/registry.hpp -- a small entity-component registry with a
// deterministic iteration order.
//
//   using World = strata::sim::Registry<Position, Energy, Species>;
//   World world;
//   strata::sim::Entity e = world.create();
//   world.add(e, Position{1, 2});
//   world.each<Position, Energy>([&](strata::sim::Entity e, Position& p, Energy& en) { ... });
//
// The component types are fixed at compile time, so there is no RTTI and no
// type map. Each component lives in a dense array indexed by entity slot.
//
// Determinism rules:
//   - each() visits entities in ascending slot order, every time.
//   - Entities created inside each() are appended after every existing slot,
//     so the pass that created them never visits them.
//   - Entities destroyed inside each() are skipped from that point on. Their
//     slots are recycled (lowest freed slot first) only by create() calls made
//     outside each().
//   - create() inside each() may grow the component arrays, which invalidates
//     the references the callback received. Do not touch them after calling
//     create(); collecting new entities and creating them after the pass is
//     usually simpler.
// Together these make a tick's result depend only on the registry's state and
// the order of the calls made on it.
//
// Copyright (c) 2026 Daniel McCoy Stephenson. MIT licence; see LICENSE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

namespace strata::sim {

struct Entity {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool valid() const { return index != UINT32_MAX; }
    friend bool operator==(Entity a, Entity b) { return a.index == b.index && a.generation == b.generation; }
    friend bool operator!=(Entity a, Entity b) { return !(a == b); }
};

namespace detail {
template <class T, class... Ts>
struct IndexOf;
template <class T, class... Ts>
struct IndexOf<T, T, Ts...> : std::integral_constant<size_t, 0> {};
template <class T, class U, class... Ts>
struct IndexOf<T, U, Ts...> : std::integral_constant<size_t, 1 + IndexOf<T, Ts...>::value> {};

template <class T>
struct Pool {
    std::vector<T> values;
    std::vector<uint8_t> present;
};
}  // namespace detail

template <class... Components>
class Registry {
    static_assert(sizeof...(Components) > 0, "a registry needs at least one component type");

public:
    // A new entity with no components.
    Entity create() {
        uint32_t index;
        if (iterating_ == 0 && !free_.empty()) {
            index = free_.top();
            free_.pop();
        } else {
            index = static_cast<uint32_t>(generations_.size());
            generations_.push_back(0);
            alive_.push_back(0);
            growPools(generations_.size());
        }
        alive_[index] = 1;
        ++aliveCount_;
        return Entity{index, generations_[index]};
    }

    // Removes the entity and all its components. Destroying a dead or stale
    // handle does nothing.
    void destroy(Entity e) {
        if (!alive(e)) return;
        clearComponents(e.index);
        alive_[e.index] = 0;
        ++generations_[e.index];
        --aliveCount_;
        free_.push(e.index);
    }

    bool alive(Entity e) const {
        return e.index < generations_.size() && alive_[e.index] != 0 && generations_[e.index] == e.generation;
    }

    template <class C>
    C& add(Entity e, C value) {
        requireAlive(e);
        auto& pool = poolOf<C>();
        pool.values[e.index] = std::move(value);
        pool.present[e.index] = 1;
        return pool.values[e.index];
    }

    template <class C>
    void remove(Entity e) {
        if (!alive(e)) return;
        auto& pool = poolOf<C>();
        pool.present[e.index] = 0;
        pool.values[e.index] = C{};
    }

    template <class C>
    bool has(Entity e) const {
        return alive(e) && poolOf<C>().present[e.index] != 0;
    }

    template <class C>
    C* get(Entity e) {
        return has<C>(e) ? &poolOf<C>().values[e.index] : nullptr;
    }

    template <class C>
    const C* get(Entity e) const {
        return has<C>(e) ? &poolOf<C>().values[e.index] : nullptr;
    }

    // Calls fn(Entity, Q&...) for every living entity that has all of Q, in
    // ascending slot order. See the determinism rules at the top.
    template <class... Q, class Fn>
    void each(Fn&& fn) {
        const size_t end = generations_.size();
        ++iterating_;
        try {
            for (size_t i = 0; i < end; ++i) {
                if (alive_[i] == 0 || !hasAll<Q...>(i)) continue;
                fn(Entity{static_cast<uint32_t>(i), generations_[i]}, poolOf<Q>().values[i]...);
            }
        } catch (...) {
            --iterating_;
            throw;
        }
        --iterating_;
    }

    // Read-only visit: fn(Entity, const Q&...), same order. The registry
    // cannot be changed from inside it.
    template <class... Q, class Fn>
    void each(Fn&& fn) const {
        const size_t end = generations_.size();
        for (size_t i = 0; i < end; ++i) {
            if (alive_[i] == 0 || !hasAll<Q...>(i)) continue;
            fn(Entity{static_cast<uint32_t>(i), generations_[i]}, poolOf<Q>().values[i]...);
        }
    }

    // Number of living entities.
    size_t size() const { return aliveCount_; }
    // Number of slots ever allocated (living or free).
    size_t capacity() const { return generations_.size(); }

    // Destroys every entity and forgets every slot.
    void clear() {
        generations_.clear();
        alive_.clear();
        free_ = {};
        aliveCount_ = 0;
        clearPools(std::index_sequence_for<Components...>{});
    }

private:
    template <class C>
    detail::Pool<C>& poolOf() {
        return std::get<detail::IndexOf<C, Components...>::value>(pools_);
    }
    template <class C>
    const detail::Pool<C>& poolOf() const {
        return std::get<detail::IndexOf<C, Components...>::value>(pools_);
    }

    template <class... Q>
    bool hasAll(size_t i) const {
        return ((poolOf<Q>().present[i] != 0) && ...);
    }

    void requireAlive(Entity e) const {
        if (!alive(e)) throw std::invalid_argument("strata::sim::Registry: entity is not alive");
    }

    void growPools(size_t n) { growPools(n, std::index_sequence_for<Components...>{}); }
    template <size_t... I>
    void growPools(size_t n, std::index_sequence<I...>) {
        ((std::get<I>(pools_).values.resize(n), std::get<I>(pools_).present.resize(n, 0)), ...);
    }
    void clearComponents(uint32_t index) {
        ((poolOf<Components>().present[index] = 0, poolOf<Components>().values[index] = Components{}), ...);
    }
    template <size_t... I>
    void clearPools(std::index_sequence<I...>) {
        ((std::get<I>(pools_).values.clear(), std::get<I>(pools_).present.clear()), ...);
    }

    std::tuple<detail::Pool<Components>...> pools_;
    std::vector<uint32_t> generations_;
    std::vector<uint8_t> alive_;
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> free_;
    size_t aliveCount_ = 0;
    int iterating_ = 0;
};

}  // namespace strata::sim
