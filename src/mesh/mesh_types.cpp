/*
 * Mesh 分区范围与查询。
 */

#include "mesh/mesh_types.h"

#include <cmath>
#include <sstream>

namespace text3d {

IndexRange Mesh::part_range(MeshPart p) const {
    const auto i = static_cast<std::size_t>(p);
    if (i >= static_cast<std::size_t>(MeshPart::Count)) {
        return {};
    }
    return parts[i];
}

void Mesh::set_part(MeshPart p, std::size_t begin, std::size_t end) {
    const auto i = static_cast<std::size_t>(p);
    if (i >= static_cast<std::size_t>(MeshPart::Count)) {
        return;
    }
    parts[i].begin = static_cast<unsigned>(begin);
    parts[i].end = static_cast<unsigned>(end);
}

void Mesh::clear_parts() {
    for (auto& r : parts) {
        r = {};
    }
}

const char* mesh_part_name(MeshPart p) {
    switch (p) {
        case MeshPart::Front:
            return "Front";
        case MeshPart::Back:
            return "Back";
        case MeshPart::Side:
            return "Side";
        case MeshPart::Bevel:
            return "Bevel";
        case MeshPart::Rounded:
            return "Rounded";
        case MeshPart::Count:
        default:
            return "Unknown";
    }
}

std::string mesh_parts_format(const Mesh& mesh) {
    std::ostringstream oss;
    bool first = true;
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(MeshPart::Count); ++i) {
        const MeshPart p = static_cast<MeshPart>(i);
        const IndexRange r = mesh.part_range(p);
        if (r.empty()) {
            continue;
        }
        if (!first) {
            oss << ' ';
        }
        first = false;
        oss << mesh_part_name(p) << "[" << r.begin << "," << r.end << ")"
            << " tris=" << (r.count() / 3);
    }
    if (first) {
        oss << "(none)";
    }
    return oss.str();
}

bool mesh_front_normals_are_flat(const Mesh& mesh, float eps) {
    const IndexRange r = mesh.part_range(MeshPart::Front);
    if (r.empty()) {
        return false;
    }
    for (unsigned i = r.begin; i < r.end; ++i) {
        if (i >= mesh.indices.size()) {
            return false;
        }
        const unsigned vi = mesh.indices[i];
        if (vi >= mesh.vertices.size()) {
            return false;
        }
        const Vertex& v = mesh.vertices[vi];
        if (std::fabs(v.nx) > eps || std::fabs(v.ny) > eps || std::fabs(v.nz - 1.f) > eps) {
            return false;
        }
    }
    return true;
}

}  // namespace text3d
