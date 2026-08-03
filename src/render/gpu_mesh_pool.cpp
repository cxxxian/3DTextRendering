/*
 * GpuMeshPool：fingerprint → 共享 GPU mesh。
 */

#include "render/gpu_mesh_pool.h"

#include <cstddef>

#include <glad/gl.h>

namespace text3d {
namespace {

void setup_mesh_attribs() {
    const int stride = static_cast<int>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, px)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, nx)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(Vertex, u)));
}

}  // namespace

GpuMeshPool::~GpuMeshPool() {
    clear();
}

void GpuMeshPool::destroy_entry_(GpuMeshEntry& e) {
    if (e.ebo) {
        glDeleteBuffers(1, &e.ebo);
        e.ebo = 0;
    }
    if (e.vbo) {
        glDeleteBuffers(1, &e.vbo);
        e.vbo = 0;
    }
    if (e.vao) {
        glDeleteVertexArrays(1, &e.vao);
        e.vao = 0;
    }
    e.index_count = 0;
    e.fingerprint = 0;
}

void GpuMeshPool::upload_entry_(GpuMeshEntry& e, const Mesh& mesh) {
    e.index_count = static_cast<int>(mesh.indices.size());
    glBindVertexArray(e.vao);
    glBindBuffer(GL_ARRAY_BUFFER, e.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
                 mesh.vertices.empty() ? nullptr : mesh.vertices.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, e.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned int)),
                 mesh.indices.empty() ? nullptr : mesh.indices.data(),
                 GL_STATIC_DRAW);
    setup_mesh_attribs();
    glBindVertexArray(0);
}

GpuMeshEntry* GpuMeshPool::acquire(std::uint64_t fingerprint, const Mesh& mesh,
                                   bool* out_uploaded) {
    auto it = entries_.find(fingerprint);
    if (it != entries_.end()) {
        if (out_uploaded) {
            *out_uploaded = false;
        }
        return &it->second;
    }

    GpuMeshEntry e;
    e.fingerprint = fingerprint;
    glGenVertexArrays(1, &e.vao);
    glGenBuffers(1, &e.vbo);
    glGenBuffers(1, &e.ebo);
    upload_entry_(e, mesh);

    auto [ins, _] = entries_.emplace(fingerprint, e);
    if (out_uploaded) {
        *out_uploaded = true;
    }
    return &ins->second;
}

GpuMeshEntry* GpuMeshPool::find(std::uint64_t fingerprint) {
    auto it = entries_.find(fingerprint);
    return it == entries_.end() ? nullptr : &it->second;
}

const GpuMeshEntry* GpuMeshPool::find(std::uint64_t fingerprint) const {
    auto it = entries_.find(fingerprint);
    return it == entries_.end() ? nullptr : &it->second;
}

void GpuMeshPool::retain_only(const std::unordered_set<std::uint64_t>& used) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (used.count(it->first) == 0) {
            destroy_entry_(it->second);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void GpuMeshPool::clear() {
    for (auto& kv : entries_) {
        destroy_entry_(kv.second);
    }
    entries_.clear();
}

}  // namespace text3d
