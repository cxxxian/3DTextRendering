/*
 * BuildResult 实现。
 */

#include "mesh/build_result.h"

#include <sstream>

namespace text3d {

void BuildResult::reset() {
    ok = false;
    stage = BuildStage::None;
    fallback_reason.clear();
    verts = 0;
    tris = 0;
    glyph_index = 0;
}

void BuildResult::set_ok(int vertex_count, int triangle_count) {
    ok = true;
    stage = BuildStage::None;
    // 保留 fallback_reason（部分字形失败/跳过时仍有用）
    verts = vertex_count;
    tris = triangle_count;
}

void BuildResult::set_fail(BuildStage fail_stage, const char* reason, unsigned int glyph) {
    ok = false;
    stage = fail_stage;
    fallback_reason = reason ? reason : "";
    verts = 0;
    tris = 0;
    glyph_index = glyph;
}

const char* build_stage_name(BuildStage stage) {
    switch (stage) {
        case BuildStage::Shape:
            return "Shape";
        case BuildStage::Outline:
            return "Outline";
        case BuildStage::Tessellate:
            return "Tessellate";
        case BuildStage::Extrude:
            return "Extrude";
        case BuildStage::Merge:
            return "Merge";
        case BuildStage::Upload:
            return "Upload";
        case BuildStage::None:
        default:
            return "None";
    }
}

std::string build_result_format(const BuildResult& r) {
    std::ostringstream oss;
    if (r.ok) {
        oss << "ok verts=" << r.verts << " tris=" << r.tris;
        if (!r.fallback_reason.empty()) {
            oss << " fallback=" << r.fallback_reason;
        }
    } else {
        oss << "FAIL stage=" << build_stage_name(r.stage);
        if (r.glyph_index != 0) {
            oss << " glyph=" << r.glyph_index;
        }
        if (!r.fallback_reason.empty()) {
            oss << " reason=" << r.fallback_reason;
        }
    }
    return oss.str();
}

}  // namespace text3d
