/*
 * CPU Mesh 池 LRU 实现。
 */

#include "mesh/glyph_mesh_pool.h"

#include <algorithm>
#include <cmath>

namespace text3d {

std::uint32_t quantize_inflate(float inflate_01) {
    return quantize_strength(inflate_01);
}

std::uint32_t quantize_scale(float scale) {
    return static_cast<std::uint32_t>(std::lround(std::max(0.f, scale) * 1.0e6f));
}

MeshKey make_mesh_key(const OutlineKey& outline, TessMode tess, float depth, float bevel,
                      float fillet, float inflate, float scale) {
    MeshKey k;
    k.outline = outline;
    k.tess_mode = tess;
    k.edge_mode = edge_mode_from_extrude(bevel, fillet);
    k.depth_q = quantize_depth(depth);
    if (k.edge_mode == EdgeMode::None) {
        k.strength_q = 0;
    } else {
        const float strength = (k.edge_mode == EdgeMode::Fillet) ? fillet : bevel;
        k.strength_q = quantize_strength(strength);
    }
    k.inflate_q = quantize_inflate(inflate);
    k.scale_q = quantize_scale(scale);
    return k;
}

std::size_t estimate_mesh_bytes(const Mesh& m) {
    return m.vertices.size() * sizeof(Vertex) + m.indices.size() * sizeof(unsigned int) + 128;
}

GlyphMeshPool::GlyphMeshPool(std::size_t byte_limit) : byte_limit_(byte_limit) {}

void GlyphMeshPool::set_byte_limit(std::size_t limit) {
    byte_limit_ = limit;
    evict_until(0);
}

void GlyphMeshPool::clear() {
    order_.clear();
    index_.clear();
    bytes_used_ = 0;
}

CacheLayerStats GlyphMeshPool::stats() const {
    CacheLayerStats s;
    s.lookups = lookups_;
    s.hits = hits_;
    s.entries = index_.size();
    s.bytes = bytes_used_;
    s.byte_limit = byte_limit_;
    return s;
}

void GlyphMeshPool::evict_until(std::size_t need_extra) {
    while (!order_.empty() && bytes_used_ + need_extra > byte_limit_) {
        auto last = std::prev(order_.end());
        // 仍有外部 shared_ptr 时也能踢出索引；几何由 shared_ptr 续命
        bytes_used_ -= last->bytes;
        index_.erase(last->key);
        order_.erase(last);
    }
}

const PooledGlyphMesh* GlyphMeshPool::find(const MeshKey& key) {
    ++lookups_;
    auto it = index_.find(key);
    if (it == index_.end()) {
        return nullptr;
    }
    ++hits_;
    order_.splice(order_.begin(), order_, it->second);
    return &it->second->value;
}

const PooledGlyphMesh* GlyphMeshPool::put(const MeshKey& key, PooledGlyphMesh entry) {
    const std::size_t bytes =
        entry.mesh ? estimate_mesh_bytes(*entry.mesh) : sizeof(PooledGlyphMesh);
    auto it = index_.find(key);
    if (it != index_.end()) {
        bytes_used_ -= it->second->bytes;
        it->second->value = std::move(entry);
        it->second->bytes = bytes;
        bytes_used_ += bytes;
        order_.splice(order_.begin(), order_, it->second);
        evict_until(0);
        return &it->second->value;
    }
    evict_until(bytes);
    if (bytes > byte_limit_) {
        order_.clear();
        index_.clear();
        bytes_used_ = 0;
    }
    order_.push_front(Entry{key, std::move(entry), bytes});
    index_[key] = order_.begin();
    bytes_used_ += bytes;
    evict_until(0);
    auto jt = index_.find(key);
    return jt == index_.end() ? nullptr : &jt->second->value;
}

}  // namespace text3d
