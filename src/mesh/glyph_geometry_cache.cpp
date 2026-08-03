/*
 * OutlineCache + PlanarCache 双表 LRU 实现。
 */

#include "mesh/glyph_geometry_cache.h"

#include <algorithm>
#include <cmath>

namespace text3d {
namespace {

constexpr float kEps = 1e-6f;

}  // namespace

std::uint32_t quantize_flatness(float flatness) {
    return static_cast<std::uint32_t>(std::lround(std::max(0.f, flatness) * 1000.f));
}

std::uint32_t quantize_depth(float depth) {
    return static_cast<std::uint32_t>(std::lround(std::max(0.f, depth) * 10.f));
}

std::uint32_t quantize_strength(float strength_01) {
    const float t = std::max(0.f, std::min(1.f, strength_01));
    return static_cast<std::uint32_t>(std::lround(t * 100.f));
}

EdgeMode edge_mode_from_extrude(float bevel, float fillet) {
    if (fillet > kEps) {
        return EdgeMode::Fillet;
    }
    if (bevel > kEps) {
        return EdgeMode::Bevel;
    }
    return EdgeMode::None;
}

PlanarKey make_planar_key(const OutlineKey& outline, TessMode tess, float depth, float bevel,
                          float fillet) {
    PlanarKey k;
    k.outline = outline;
    k.tess_mode = tess;
    k.edge_mode = edge_mode_from_extrude(bevel, fillet);
    if (k.edge_mode == EdgeMode::None) {
        k.depth_q = 0;
        k.strength_q = 0;
    } else {
        k.depth_q = quantize_depth(depth);
        const float strength = (k.edge_mode == EdgeMode::Fillet) ? fillet : bevel;
        k.strength_q = quantize_strength(strength);
    }
    return k;
}

std::size_t estimate_outline_bytes(const GlyphOutline& o) {
    std::size_t n = sizeof(GlyphOutline);
    for (const Contour& c : o.contours) {
        n += sizeof(Contour) + c.points.size() * sizeof(Vec2);
    }
    return n;
}

std::size_t estimate_planar_bytes(const GlyphPlanar2D& p) {
    return estimate_outline_bytes(p.cleaned) + estimate_outline_bytes(p.inner) +
           p.cap_xy.size() * sizeof(float) + p.cap_tris.size() * sizeof(unsigned int) +
           p.tess_fallback.size() + 64;
}

std::uint64_t hash_font_path(const std::string& path) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : path) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

GlyphGeometryCache::GlyphGeometryCache(std::size_t outline_byte_limit,
                                       std::size_t planar_byte_limit) {
    outlines_.byte_limit = outline_byte_limit;
    planars_.byte_limit = planar_byte_limit;
}

void GlyphGeometryCache::set_byte_limits(std::size_t outline_limit, std::size_t planar_limit) {
    outlines_.byte_limit = outline_limit;
    planars_.byte_limit = planar_limit;
    evict_outline_until(0);
    evict_planar_until(0);
}

void GlyphGeometryCache::clear() {
    outlines_.order.clear();
    outlines_.index.clear();
    outlines_.bytes_used = 0;
    planars_.order.clear();
    planars_.index.clear();
    planars_.bytes_used = 0;
}

GlyphCacheStats GlyphGeometryCache::stats() const {
    GlyphCacheStats s;
    s.outline.lookups = outlines_.lookups;
    s.outline.hits = outlines_.hits;
    s.outline.entries = outlines_.index.size();
    s.outline.bytes = outlines_.bytes_used;
    s.outline.byte_limit = outlines_.byte_limit;
    s.planar.lookups = planars_.lookups;
    s.planar.hits = planars_.hits;
    s.planar.entries = planars_.index.size();
    s.planar.bytes = planars_.bytes_used;
    s.planar.byte_limit = planars_.byte_limit;
    return s;
}

void GlyphGeometryCache::evict_outline_until(std::size_t need_extra) {
    while (!outlines_.order.empty() &&
           outlines_.bytes_used + need_extra > outlines_.byte_limit) {
        auto last = std::prev(outlines_.order.end());
        outlines_.bytes_used -= last->bytes;
        outlines_.index.erase(last->key);
        outlines_.order.erase(last);
    }
}

void GlyphGeometryCache::evict_planar_until(std::size_t need_extra) {
    while (!planars_.order.empty() && planars_.bytes_used + need_extra > planars_.byte_limit) {
        auto last = std::prev(planars_.order.end());
        planars_.bytes_used -= last->bytes;
        planars_.index.erase(last->key);
        planars_.order.erase(last);
    }
}

const GlyphOutline* GlyphGeometryCache::find_outline(const OutlineKey& key) {
    ++outlines_.lookups;
    auto it = outlines_.index.find(key);
    if (it == outlines_.index.end()) {
        return nullptr;
    }
    ++outlines_.hits;
    outlines_.order.splice(outlines_.order.begin(), outlines_.order, it->second);
    return &it->second->value;
}

const GlyphOutline* GlyphGeometryCache::put_outline(const OutlineKey& key, GlyphOutline outline) {
    const std::size_t bytes = estimate_outline_bytes(outline);
    auto it = outlines_.index.find(key);
    if (it != outlines_.index.end()) {
        outlines_.bytes_used -= it->second->bytes;
        it->second->value = std::move(outline);
        it->second->bytes = bytes;
        outlines_.bytes_used += bytes;
        outlines_.order.splice(outlines_.order.begin(), outlines_.order, it->second);
        evict_outline_until(0);
        return &it->second->value;
    }
    evict_outline_until(bytes);
    if (bytes > outlines_.byte_limit) {
        outlines_.order.clear();
        outlines_.index.clear();
        outlines_.bytes_used = 0;
    }
    outlines_.order.push_front(
        LruTable<OutlineKey, GlyphOutline, OutlineKeyHash>::Entry{key, std::move(outline), bytes});
    outlines_.index[key] = outlines_.order.begin();
    outlines_.bytes_used += bytes;
    evict_outline_until(0);
    // 可能被踢掉自己（极端超大单条）；再查一次
    auto jt = outlines_.index.find(key);
    return jt == outlines_.index.end() ? nullptr : &jt->second->value;
}

const GlyphPlanar2D* GlyphGeometryCache::find_planar(const PlanarKey& key) {
    ++planars_.lookups;
    auto it = planars_.index.find(key);
    if (it == planars_.index.end()) {
        return nullptr;
    }
    ++planars_.hits;
    planars_.order.splice(planars_.order.begin(), planars_.order, it->second);
    return &it->second->value;
}

const GlyphPlanar2D* GlyphGeometryCache::put_planar(const PlanarKey& key, GlyphPlanar2D planar) {
    const std::size_t bytes = estimate_planar_bytes(planar);
    auto it = planars_.index.find(key);
    if (it != planars_.index.end()) {
        planars_.bytes_used -= it->second->bytes;
        it->second->value = std::move(planar);
        it->second->bytes = bytes;
        planars_.bytes_used += bytes;
        planars_.order.splice(planars_.order.begin(), planars_.order, it->second);
        evict_planar_until(0);
        return &it->second->value;
    }
    evict_planar_until(bytes);
    if (bytes > planars_.byte_limit) {
        planars_.order.clear();
        planars_.index.clear();
        planars_.bytes_used = 0;
    }
    planars_.order.push_front(
        LruTable<PlanarKey, GlyphPlanar2D, PlanarKeyHash>::Entry{key, std::move(planar), bytes});
    planars_.index[key] = planars_.order.begin();
    planars_.bytes_used += bytes;
    evict_planar_until(0);
    auto jt = planars_.index.find(key);
    return jt == planars_.index.end() ? nullptr : &jt->second->value;
}

}  // namespace text3d
