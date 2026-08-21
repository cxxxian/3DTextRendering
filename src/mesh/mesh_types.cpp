/*
 * Mesh 分区范围与查询。
 */

#include "mesh/mesh_types.h"

#include <cmath>
#include <sstream>
#include <vector>

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

void compute_mesh_tangents(Mesh& mesh) {
    const size_t n = mesh.vertices.size();
    if (n == 0 || mesh.indices.size() < 3) {
        return;
    }

    std::vector<float> tan1(n * 3, 0.f);
    std::vector<float> tan2(n * 3, 0.f);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const unsigned i0 = mesh.indices[i];
        const unsigned i1 = mesh.indices[i + 1];
        const unsigned i2 = mesh.indices[i + 2];
        if (i0 >= n || i1 >= n || i2 >= n) {
            continue;
        }
        const Vertex& v0 = mesh.vertices[i0];
        const Vertex& v1 = mesh.vertices[i1];
        const Vertex& v2 = mesh.vertices[i2];

        const float x1 = v1.px - v0.px, y1 = v1.py - v0.py, z1 = v1.pz - v0.pz;
        const float x2 = v2.px - v0.px, y2 = v2.py - v0.py, z2 = v2.pz - v0.pz;
        const float s1 = v1.u - v0.u, t1 = v1.v - v0.v;
        const float s2 = v2.u - v0.u, t2 = v2.v - v0.v;
        const float denom = s1 * t2 - s2 * t1;
        if (std::fabs(denom) < 1e-12f) {
            continue;
        }
        const float r = 1.f / denom;
        const float tx = (t2 * x1 - t1 * x2) * r;
        const float ty = (t2 * y1 - t1 * y2) * r;
        const float tz = (t2 * z1 - t1 * z2) * r;
        const float bx = (s1 * x2 - s2 * x1) * r;
        const float by = (s1 * y2 - s2 * y1) * r;
        const float bz = (s1 * z2 - s2 * z1) * r;

        tan1[i0 * 3] += tx;
        tan1[i0 * 3 + 1] += ty;
        tan1[i0 * 3 + 2] += tz;
        tan1[i1 * 3] += tx;
        tan1[i1 * 3 + 1] += ty;
        tan1[i1 * 3 + 2] += tz;
        tan1[i2 * 3] += tx;
        tan1[i2 * 3 + 1] += ty;
        tan1[i2 * 3 + 2] += tz;

        tan2[i0 * 3] += bx;
        tan2[i0 * 3 + 1] += by;
        tan2[i0 * 3 + 2] += bz;
        tan2[i1 * 3] += bx;
        tan2[i1 * 3 + 1] += by;
        tan2[i1 * 3 + 2] += bz;
        tan2[i2 * 3] += bx;
        tan2[i2 * 3 + 1] += by;
        tan2[i2 * 3 + 2] += bz;
    }

    for (size_t i = 0; i < n; ++i) {
        Vertex& v = mesh.vertices[i];
        float nx = v.nx, ny = v.ny, nz = v.nz;
        float tx = tan1[i * 3], ty = tan1[i * 3 + 1], tz = tan1[i * 3 + 2];

        // Gram-Schmidt：T 对 N 正交
        const float ndott = nx * tx + ny * ty + nz * tz;
        tx -= nx * ndott;
        ty -= ny * ndott;
        tz -= nz * ndott;
        float tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (tlen > 1e-8f) {
            tx /= tlen;
            ty /= tlen;
            tz /= tlen;
        } else {
            // 退化 UV：造一个与 N 垂直的任意切线
            if (std::fabs(nx) < 0.9f) {
                tx = 0.f;
                ty = nz;
                tz = -ny;
            } else {
                tx = -nz;
                ty = 0.f;
                tz = nx;
            }
            tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (tlen > 1e-8f) {
                tx /= tlen;
                ty /= tlen;
                tz /= tlen;
            } else {
                tx = 1.f;
                ty = tz = 0.f;
            }
        }

        const float bx = tan2[i * 3], by = tan2[i * 3 + 1], bz = tan2[i * 3 + 2];
        // handedness：cross(N,T)·B
        const float cx = ny * tz - nz * ty;
        const float cy = nz * tx - nx * tz;
        const float cz = nx * ty - ny * tx;
        const float w = (cx * bx + cy * by + cz * bz) < 0.f ? -1.f : 1.f;

        v.tx = tx;
        v.ty = ty;
        v.tz = tz;
        v.tw = w;
    }
}

}  // namespace text3d
