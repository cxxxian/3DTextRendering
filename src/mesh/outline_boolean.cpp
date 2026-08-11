/*
 * Clipper2 轮廓布尔实现。
 */

#include "mesh/outline_boolean.h"

#include "mesh/contour_clean.h"

#include "clipper2/clipper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace text3d {
namespace {

constexpr double kClipScale = 100.0;  // float → int64；0.01px 精度

using Clipper2Lib::FillRule;
using Clipper2Lib::JoinType;
using Clipper2Lib::EndType;
using Clipper2Lib::Path64;
using Clipper2Lib::Paths64;
using Clipper2Lib::Point64;

int64_t to_clip(float v) {
    return static_cast<int64_t>(std::llround(static_cast<double>(v) * kClipScale));
}

float from_clip(int64_t v) {
    return static_cast<float>(static_cast<double>(v) / kClipScale);
}

/* 工程 CW外/CCW孔 → Clipper NonZero（翻转后 CCW外/CW孔） */
Path64 contour_to_clipper(const Contour& c) {
    Path64 path;
    path.reserve(c.points.size());
    for (const Vec2& p : c.points) {
        path.emplace_back(to_clip(p.x), to_clip(p.y));
    }
    std::reverse(path.begin(), path.end());
    return path;
}

Paths64 outline_to_clipper(const GlyphOutline& outline) {
    Paths64 paths;
    paths.reserve(outline.contours.size());
    for (const Contour& c : outline.contours) {
        if (c.points.size() < 3) {
            continue;
        }
        paths.push_back(contour_to_clipper(c));
    }
    return paths;
}

bool clipper_to_outline(const Paths64& paths, GlyphOutline& out) {
    out.contours.clear();
    out.contours.reserve(paths.size());
    for (const Path64& path : paths) {
        if (path.size() < 3) {
            continue;
        }
        Contour c;
        c.points.reserve(path.size());
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            c.points.push_back(Vec2{from_clip(it->x), from_clip(it->y)});
        }
        out.contours.push_back(std::move(c));
    }
    if (out.contours.empty()) {
        return false;
    }
    return clean_glyph_outline(out);
}

double paths_abs_area_font_units(const Paths64& paths) {
    const double a = Clipper2Lib::Area(paths);
    return std::abs(a) / (kClipScale * kClipScale);
}

}  // namespace

Aabb2 compute_outline_aabb(const GlyphOutline& outline) {
    Aabb2 box;
    bool any = false;
    for (const Contour& c : outline.contours) {
        for (const Vec2& p : c.points) {
            if (!any) {
                box.min_x = box.max_x = p.x;
                box.min_y = box.max_y = p.y;
                any = true;
            } else {
                box.min_x = std::min(box.min_x, p.x);
                box.min_y = std::min(box.min_y, p.y);
                box.max_x = std::max(box.max_x, p.x);
                box.max_y = std::max(box.max_y, p.y);
            }
        }
    }
    return box;
}

bool aabb_overlaps(const Aabb2& a, const Aabb2& b, float eps) {
    return a.min_x <= b.max_x + eps && a.max_x + eps >= b.min_x && a.min_y <= b.max_y + eps &&
           a.max_y + eps >= b.min_y;
}

void translate_outline(GlyphOutline& outline, float dx, float dy) {
    if (dx == 0.f && dy == 0.f) {
        return;
    }
    for (Contour& c : outline.contours) {
        for (Vec2& p : c.points) {
            p.x += dx;
            p.y += dy;
        }
    }
}

double outline_intersection_area(const GlyphOutline& a, const GlyphOutline& b) {
    const Paths64 pa = outline_to_clipper(a);
    const Paths64 pb = outline_to_clipper(b);
    if (pa.empty() || pb.empty()) {
        return 0.0;
    }
    const Paths64 hit = Clipper2Lib::Intersect(pa, pb, FillRule::NonZero);
    return paths_abs_area_font_units(hit);
}

double outline_abs_area(const GlyphOutline& outline) {
    return paths_abs_area_font_units(outline_to_clipper(outline));
}

bool union_outlines(const std::vector<const GlyphOutline*>& parts, GlyphOutline& out) {
    if (parts.empty()) {
        return false;
    }
    if (parts.size() == 1) {
        if (!parts[0]) {
            return false;
        }
        out = *parts[0];
        return clean_glyph_outline(out) || !out.contours.empty();
    }

    Paths64 all;
    for (const GlyphOutline* p : parts) {
        if (!p) {
            continue;
        }
        Paths64 one = outline_to_clipper(*p);
        all.insert(all.end(), one.begin(), one.end());
    }
    if (all.empty()) {
        return false;
    }

    const Paths64 result = Clipper2Lib::Union(all, FillRule::NonZero);
    GlyphOutline local;
    if (parts[0]) {
        local.advance_x = parts[0]->advance_x;
        local.bearing_x = parts[0]->bearing_x;
        local.bearing_y = parts[0]->bearing_y;
    }
    if (!clipper_to_outline(result, local)) {
        return false;
    }
    out = std::move(local);
    return true;
}

bool difference_outlines(const GlyphOutline& subject,
                         const std::vector<const GlyphOutline*>& clips, GlyphOutline& out,
                         float clip_inflate_delta) {
    if (clips.empty()) {
        out = subject;
        return clean_glyph_outline(out) || !out.contours.empty();
    }

    Paths64 subj = outline_to_clipper(subject);
    if (subj.empty()) {
        return false;
    }

    Paths64 clip_paths;
    for (const GlyphOutline* clip : clips) {
        if (!clip) {
            continue;
        }
        Paths64 one = outline_to_clipper(*clip);
        clip_paths.insert(clip_paths.end(), one.begin(), one.end());
    }
    if (clip_paths.empty()) {
        out = subject;
        return !out.contours.empty();
    }

    if (clips.size() > 1 || clip_paths.size() > 1) {
        clip_paths = Clipper2Lib::Union(clip_paths, FillRule::NonZero);
    }

    if (clip_inflate_delta > 0.f) {
        const double delta = static_cast<double>(clip_inflate_delta) * kClipScale;
        clip_paths = Clipper2Lib::InflatePaths(clip_paths, delta, JoinType::Round, EndType::Polygon);
    }

    const Paths64 result = Clipper2Lib::Difference(subj, clip_paths, FillRule::NonZero);
    GlyphOutline local;
    local.advance_x = subject.advance_x;
    local.bearing_x = subject.bearing_x;
    local.bearing_y = subject.bearing_y;
    if (!clipper_to_outline(result, local)) {
        return false;
    }
    out = std::move(local);
    return true;
}

}  // namespace text3d
