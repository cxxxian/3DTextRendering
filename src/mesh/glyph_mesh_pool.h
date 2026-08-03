#pragma once
/*
 * #9 CPU Mesh 池：同字共享 + 跨次 rebuild 复用最终网格。
 * 键含完整 3D/scale 参数；值用 shared_ptr，多实例共一份几何。
 */

#include "mesh/glyph_geometry_cache.h"
#include "mesh/mesh_types.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>

namespace text3d {

struct MeshKey {
    OutlineKey outline;
    TessMode tess_mode = TessMode::Auto;
    EdgeMode edge_mode = EdgeMode::None;
    std::uint32_t depth_q = 0;
    std::uint32_t strength_q = 0;
    std::uint32_t inflate_q = 0;
    std::uint32_t scale_q = 0;

    bool operator==(const MeshKey& o) const {
        return outline == o.outline && tess_mode == o.tess_mode && edge_mode == o.edge_mode &&
               depth_q == o.depth_q && strength_q == o.strength_q && inflate_q == o.inflate_q &&
               scale_q == o.scale_q;
    }
};

struct MeshKeyHash {
    std::size_t operator()(const MeshKey& k) const noexcept {
        std::size_t h = OutlineKeyHash{}(k.outline);
        h ^= std::hash<int>{}(static_cast<int>(k.tess_mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(k.edge_mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(k.depth_q) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(k.strength_q) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(k.inflate_q) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(k.scale_q) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/* 池条目：网格 + 构建时元数据（命中时可跳过 planar/3d 仍能报 R/H） */
struct PooledGlyphMesh {
    std::shared_ptr<const Mesh> mesh;
    float applied_radius = 0.f;
    float applied_inflate_h = 0.f;
    float safe_radius_cap = 0.f;
    float layout_cx = 0.f;  // 居中前 bbox 中心（与 rest 计算一致，字体单位）
    float layout_cy = 0.f;
};

std::uint32_t quantize_inflate(float inflate_01);
std::uint32_t quantize_scale(float scale);

MeshKey make_mesh_key(const OutlineKey& outline, TessMode tess, float depth, float bevel,
                      float fillet, float inflate, float scale);

std::size_t estimate_mesh_bytes(const Mesh& m);

class GlyphMeshPool {
public:
    explicit GlyphMeshPool(std::size_t byte_limit = 64u * 1024u * 1024u);

    void set_byte_limit(std::size_t limit);
    void clear();
    CacheLayerStats stats() const;

    const PooledGlyphMesh* find(const MeshKey& key);
    const PooledGlyphMesh* put(const MeshKey& key, PooledGlyphMesh entry);

private:
    struct Entry {
        MeshKey key;
        PooledGlyphMesh value;
        std::size_t bytes = 0;
    };
    using List = std::list<Entry>;
    using Map = std::unordered_map<MeshKey, List::iterator, MeshKeyHash>;

    List order_;
    Map index_;
    std::size_t bytes_used_ = 0;
    std::size_t byte_limit_ = 0;
    std::uint64_t lookups_ = 0;
    std::uint64_t hits_ = 0;

    void evict_until(std::size_t need_extra);
};

}  // namespace text3d
