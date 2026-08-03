#pragma once
/*
 * #12 GPU Mesh 池：按 fingerprint 共享一份 VAO/VBO/EBO。
 * 槽只引用；apply 结束用 retain_only 丢掉未引用 entry。
 */

#include "mesh/mesh_types.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace text3d {

struct GpuMeshEntry {
    std::uint64_t fingerprint = 0;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    int index_count = 0;
};

class GpuMeshPool {
public:
    GpuMeshPool() = default;
    ~GpuMeshPool();

    GpuMeshPool(const GpuMeshPool&) = delete;
    GpuMeshPool& operator=(const GpuMeshPool&) = delete;

    /* miss 时创建并 upload；hit 时 out_uploaded=false */
    GpuMeshEntry* acquire(std::uint64_t fingerprint, const Mesh& mesh, bool* out_uploaded);

    GpuMeshEntry* find(std::uint64_t fingerprint);
    const GpuMeshEntry* find(std::uint64_t fingerprint) const;

    void retain_only(const std::unordered_set<std::uint64_t>& used);
    void clear();

    int size() const { return static_cast<int>(entries_.size()); }

private:
    std::unordered_map<std::uint64_t, GpuMeshEntry> entries_;

    static void destroy_entry_(GpuMeshEntry& e);
    static void upload_entry_(GpuMeshEntry& e, const Mesh& mesh);
};

}  // namespace text3d
