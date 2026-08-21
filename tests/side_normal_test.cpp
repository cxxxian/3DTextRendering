/*
 * 侧面法线与三角面几何法线一致性检查。
 */
#include "mesh/contour_clean.h"
#include "mesh/mesh_extrude.h"
#include "mesh/mesh_types.h"
#include "text/ft_outline.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

using namespace text3d;

void face_normal(const Mesh& m, unsigned i0, unsigned i1, unsigned i2, float& nx, float& ny,
                 float& nz) {
    const Vertex& a = m.vertices[i0];
    const Vertex& b = m.vertices[i1];
    const Vertex& c = m.vertices[i2];
    const float e1x = b.px - a.px, e1y = b.py - a.py, e1z = b.pz - a.pz;
    const float e2x = c.px - a.px, e2y = c.py - a.py, e2z = c.pz - a.pz;
    nx = e1y * e2z - e1z * e2y;
    ny = e1z * e2x - e1x * e2z;
    nz = e1x * e2y - e1y * e2x;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-8f) {
        nx /= len;
        ny /= len;
        nz /= len;
    }
}

bool check_part(const Mesh& m, MeshPart part, int& bad_tris, float& min_dot) {
    bad_tris = 0;
    min_dot = 1.f;
    const IndexRange r = m.part_range(part);
    if (r.empty()) {
        return true;
    }
    for (unsigned ii = r.begin; ii + 2 < r.end; ii += 3) {
        const unsigned i0 = m.indices[ii];
        const unsigned i1 = m.indices[ii + 1];
        const unsigned i2 = m.indices[ii + 2];
        float fnx = 0.f, fny = 0.f, fnz = 0.f;
        face_normal(m, i0, i1, i2, fnx, fny, fnz);
        const Vertex& v = m.vertices[i0];
        const float dot = v.nx * fnx + v.ny * fny + v.nz * fnz;
        min_dot = std::min(min_dot, dot);
        if (dot < 0.5f) {
            ++bad_tris;
            if (part == MeshPart::Side || part == MeshPart::Bevel) {
                std::cout << "  BAD tri@" << ii << " dot=" << dot << " vN=(" << v.nx << "," << v.ny
                          << "," << v.nz << ") fN=(" << fnx << "," << fny << "," << fnz << ")\n";
            }
        }
    }
    return bad_tris == 0;
}

bool check_side_outward(const Mesh& mesh, const GlyphOutline& outline) {
    float cx = 0.f, cy = 0.f;
    int count = 0;
    for (const Contour& c : outline.contours) {
        const float a2 = contour_signed_area2(c);
        if (std::fabs(a2) < 1e-6f) {
            continue;
        }
        for (const Vec2& p : c.points) {
            cx += p.x;
            cy += p.y;
            ++count;
        }
    }
    if (count <= 0) {
        return true;
    }
    cx /= static_cast<float>(count);
    cy /= static_cast<float>(count);

    const IndexRange side = mesh.part_range(MeshPart::Side);
    if (side.empty()) {
        return true;
    }
    int bad = 0;
    float min_dot = 1.f;
    for (unsigned ii = side.begin; ii + 2 < side.end; ii += 3) {
        const Vertex& v0 = mesh.vertices[mesh.indices[ii]];
        const Vertex& v1 = mesh.vertices[mesh.indices[ii + 1]];
        const Vertex& v2 = mesh.vertices[mesh.indices[ii + 2]];
        const float px = (v0.px + v1.px + v2.px) * (1.f / 3.f);
        const float py = (v0.py + v1.py + v2.py) * (1.f / 3.f);
        const float vx = px - cx;
        const float vy = py - cy;
        const float vlen = std::sqrt(vx * vx + vy * vy);
        if (vlen < 1e-6f) {
            continue;
        }
        const float dot =
            (v0.nx * vx + v0.ny * vy) / vlen;  // 外环应 >0；孔洞边可能 <0
        min_dot = std::min(min_dot, dot);
        if (dot < -0.1f) {
            ++bad;
        }
    }
    std::cout << "  Side outward heuristic: min_dot=" << min_dot << " bad=" << bad
              << (bad == 0 ? " OK" : " WARN") << "\n";
    return bad == 0;
}

GlyphOutline rect_with_hole() {
    GlyphOutline o;
    Contour outer;
    outer.points = {{0.f, 0.f}, {0.f, 10.f}, {10.f, 10.f}, {10.f, 0.f}};
    Contour hole;
    hole.points = {{3.f, 3.f}, {7.f, 3.f}, {7.f, 7.f}, {3.f, 7.f}};
    o.contours.push_back(std::move(outer));
    o.contours.push_back(std::move(hole));
    clean_glyph_outline(o);
    return o;
}

bool check_mesh(const Mesh& mesh, const GlyphOutline* outline, const char* label, bool side_only) {
    std::cout << "=== " << label << " ===\n";
    bool ok = true;
    for (std::uint8_t pi = 0; pi < static_cast<std::uint8_t>(MeshPart::Count); ++pi) {
        const MeshPart p = static_cast<MeshPart>(pi);
        if (side_only && p != MeshPart::Side && p != MeshPart::Bevel && p != MeshPart::Rounded) {
            continue;
        }
        int bad = 0;
        float min_dot = 1.f;
        const bool part_ok = check_part(mesh, p, bad, min_dot);
        if (!mesh.part_range(p).empty()) {
            std::cout << "  " << mesh_part_name(p) << ": min_dot=" << min_dot
                      << " bad=" << bad << (part_ok ? " OK" : " FAIL") << "\n";
        }
        ok = ok && part_ok;
    }
    if (outline) {
        ok = check_side_outward(mesh, *outline) && ok;
    }
    return ok;
}

bool run_case(const char* label, const ExtrudeOptions& opt, bool side_only = false) {
    GlyphOutline o = rect_with_hole();
    Mesh mesh;
    if (!build_extruded_mesh(o, opt, mesh)) {
        std::cerr << "[FAIL] " << label << " build failed\n";
        return false;
    }
    return check_mesh(mesh, &o, label, side_only);
}

bool run_font_glyph(FontFace& face, char32_t cp, const ExtrudeOptions& opt) {
    GlyphOutline o;
    if (!face.load_glyph_outline(cp, o)) {
        return true;
    }
    Mesh mesh;
    if (!build_extruded_mesh(o, opt, mesh)) {
        std::cerr << "[FAIL] U+" << std::hex << cp << std::dec << " build failed\n";
        return false;
    }
    char label[64];
    std::snprintf(label, sizeof(label), "glyph U+%04X", static_cast<unsigned>(cp));
    return check_mesh(mesh, &o, label, /*side_only=*/true);
}

}  // namespace

int main() {
    bool all_ok = true;
    ExtrudeOptions straight;
    straight.depth = 20.f;
    all_ok = run_case("rect straight", straight, true) && all_ok;

    ExtrudeOptions bevel = straight;
    bevel.bevel = 1.f;
    all_ok = run_case("rect bevel", bevel, true) && all_ok;

    ExtrudeOptions fillet = straight;
    fillet.fillet = 1.f;
    all_ok = run_case("rect fillet", fillet, true) && all_ok;

#ifdef TEXT3D_DEFAULT_FONT
    FontFace face;
    if (face.load(TEXT3D_DEFAULT_FONT, 64)) {
        std::cout << "Font: " << TEXT3D_DEFAULT_FONT << "\n";
        const char32_t glyphs[] = {U'H', U'e', U'l', U'o', U'O', U'B', U'日', U'国', 0x628a};
        for (char32_t cp : glyphs) {
            all_ok = run_font_glyph(face, cp, straight) && all_ok;
            all_ok = run_font_glyph(face, cp, bevel) && all_ok;
        }
    }
#endif

    std::cout << (all_ok ? "ALL OK\n" : "FAILURES\n");
    return all_ok ? 0 : 1;
}
