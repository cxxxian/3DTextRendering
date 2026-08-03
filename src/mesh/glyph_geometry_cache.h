#pragma once
/*
 * #8 双表 LRU：OutlineClean (L0) + PlanarTess (L1)。
 * 主线程同步使用；不做线程安全。
 */

#include "mesh/glyph_planar.h"
#include "mesh/mesh_offset_tess.h"
#include "text/ft_outline.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

namespace text3d {

struct ExtrudeOptions;

enum class EdgeMode : std::uint8_t {
    None = 0,
    Bevel = 1,
    Fillet = 2,
};

struct OutlineKey {
    std::uint64_t font_id = 0;
    std::uint32_t glyph_index = 0;
    std::uint32_t flatness_q = 0;  // flatness * 1000

    bool operator==(const OutlineKey& o) const {
        return font_id == o.font_id && glyph_index == o.glyph_index && flatness_q == o.flatness_q;
    }
};

struct PlanarKey {
    OutlineKey outline;
    TessMode tess_mode = TessMode::Auto;
    EdgeMode edge_mode = EdgeMode::None;
    std::uint32_t depth_q = 0;     // depth * 10；edge_mode==None 时为 0
    std::uint32_t strength_q = 0;  // strength * 100；edge_mode==None 时为 0

    bool operator==(const PlanarKey& o) const {
        return outline == o.outline && tess_mode == o.tess_mode && edge_mode == o.edge_mode &&
               depth_q == o.depth_q && strength_q == o.strength_q;
    }
};

struct OutlineKeyHash {
    std::size_t operator()(const OutlineKey& k) const noexcept {
        std::size_t h = std::hash<std::uint64_t>{}(k.font_id);
        h ^= std::hash<std::uint32_t>{}(k.glyph_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(k.flatness_q) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct PlanarKeyHash {
    std::size_t operator()(const PlanarKey& k) const noexcept {
        std::size_t h = OutlineKeyHash{}(k.outline);
        h ^= std::hash<int>{}(static_cast<int>(k.tess_mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(k.edge_mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(k.depth_q) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(k.strength_q) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct CacheLayerStats {
    std::uint64_t lookups = 0;
    std::uint64_t hits = 0;
    std::size_t entries = 0;
    std::size_t bytes = 0;
    std::size_t byte_limit = 0;

    float hit_rate() const {
        return lookups == 0 ? 0.f : static_cast<float>(hits) / static_cast<float>(lookups);
    }
};

struct GlyphCacheStats {
    CacheLayerStats outline;
    CacheLayerStats planar;
};

std::uint32_t quantize_flatness(float flatness);
std::uint32_t quantize_depth(float depth);
std::uint32_t quantize_strength(float strength_01);
EdgeMode edge_mode_from_extrude(float bevel, float fillet);
PlanarKey make_planar_key(const OutlineKey& outline, TessMode tess, float depth, float bevel,
                          float fillet);

std::size_t estimate_outline_bytes(const GlyphOutline& o);
std::size_t estimate_planar_bytes(const GlyphPlanar2D& p);

/* FNV-1a 64：字体路径 → font_id */
std::uint64_t hash_font_path(const std::string& path);

class GlyphGeometryCache {
public:
    explicit GlyphGeometryCache(std::size_t outline_byte_limit = 16u * 1024u * 1024u,
                                std::size_t planar_byte_limit = 32u * 1024u * 1024u);

    void set_byte_limits(std::size_t outline_limit, std::size_t planar_limit);
    void clear();
    GlyphCacheStats stats() const;

    /* 命中返回指针（调用方勿长期持有跨 put）；未命中返回 nullptr 并计 lookup */
    const GlyphOutline* find_outline(const OutlineKey& key);
    const GlyphOutline* put_outline(const OutlineKey& key, GlyphOutline outline);

    const GlyphPlanar2D* find_planar(const PlanarKey& key);
    const GlyphPlanar2D* put_planar(const PlanarKey& key, GlyphPlanar2D planar);

private:
    template <typename Key, typename Value, typename Hash>
    struct LruTable {
        struct Entry {
            Key key;
            Value value;
            std::size_t bytes = 0;
        };
        using List = std::list<Entry>;
        using Map = std::unordered_map<Key, typename List::iterator, Hash>;

        List order;
        Map index;
        std::size_t bytes_used = 0;
        std::size_t byte_limit = 0;
        std::uint64_t lookups = 0;
        std::uint64_t hits = 0;
    };

    LruTable<OutlineKey, GlyphOutline, OutlineKeyHash> outlines_;
    LruTable<PlanarKey, GlyphPlanar2D, PlanarKeyHash> planars_;

    void evict_outline_until(std::size_t need_extra);
    void evict_planar_until(std::size_t need_extra);
};

}  // namespace text3d
