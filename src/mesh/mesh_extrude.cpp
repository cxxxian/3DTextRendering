/*
 * 挤出编排：clean → planar → 3D；对外仍提供 build_extruded_mesh 薄包装。
 */

#include "mesh/mesh_extrude.h"

#include "mesh/glyph_planar.h"

#include <cstdint>
#include <vector>

namespace text3d {

bool build_extruded_mesh(const GlyphOutline& outline, const ExtrudeOptions& opt, Mesh& out,
                         BuildResult* result) {
    out = Mesh{};
    if (outline.contours.empty()) {
        if (result) {
            result->set_fail(BuildStage::Outline, "empty contours");
        }
        return false;
    }

    GlyphOutline cleaned;
    if (!build_glyph_cleaned_outline(outline, cleaned, result, opt.timings)) {
        return false;
    }

    GlyphPlanar2D planar;
    if (!build_glyph_planar_from_cleaned(cleaned, opt, planar, result)) {
        return false;
    }

    return build_glyph_3d_from_planar(planar, opt, out, result);
}

void translate_mesh(Mesh& mesh, float dx, float dy, float dz) {
    for (Vertex& v : mesh.vertices) {
        v.px += dx;
        v.py += dy;
        v.pz += dz;
    }
}

void scale_mesh(Mesh& mesh, float s) {
    for (Vertex& v : mesh.vertices) {
        v.px *= s;
        v.py *= s;
        v.pz *= s;
    }
}

Mesh merge_meshes(const std::vector<Mesh>& parts) {
    Mesh out;
    std::vector<unsigned int> vertex_base;
    vertex_base.reserve(parts.size());
    for (const Mesh& part : parts) {
        vertex_base.push_back(static_cast<unsigned>(out.vertices.size()));
        out.vertices.insert(out.vertices.end(), part.vertices.begin(), part.vertices.end());
    }

    // 按分区收集索引，保证合并后每个 MeshPart 仍是连续区间
    for (std::uint8_t pi = 0; pi < static_cast<std::uint8_t>(MeshPart::Count); ++pi) {
        const MeshPart mp = static_cast<MeshPart>(pi);
        const std::size_t begin = out.indices.size();
        for (size_t mi = 0; mi < parts.size(); ++mi) {
            const IndexRange r = parts[mi].part_range(mp);
            if (r.empty()) {
                continue;
            }
            const unsigned base = vertex_base[mi];
            for (unsigned i = r.begin; i < r.end; ++i) {
                if (i >= parts[mi].indices.size()) {
                    break;
                }
                out.indices.push_back(base + parts[mi].indices[i]);
            }
        }
        if (out.indices.size() > begin) {
            out.set_part(mp, begin, out.indices.size());
        }
    }

    // 无分区信息的旧 mesh：整段追加，避免丢面
    for (size_t mi = 0; mi < parts.size(); ++mi) {
        bool any_part = false;
        for (std::uint8_t pi = 0; pi < static_cast<std::uint8_t>(MeshPart::Count); ++pi) {
            if (!parts[mi].part_range(static_cast<MeshPart>(pi)).empty()) {
                any_part = true;
                break;
            }
        }
        if (any_part) {
            continue;
        }
        const unsigned base = vertex_base[mi];
        for (unsigned int idx : parts[mi].indices) {
            out.indices.push_back(base + idx);
        }
    }
    return out;
}

}  // namespace text3d
