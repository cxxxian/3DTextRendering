#pragma once
/*
 * 结构化构建结果：失败阶段 + 可选回退原因 + 网格统计。
 */

#include <cstdint>
#include <string>

namespace text3d {

enum class BuildStage : std::uint8_t {
    None = 0,
    Shape,
    Outline,
    Tessellate,
    Extrude,
    Merge,
    Upload,
};

struct BuildResult {
    bool ok = false;
    BuildStage stage = BuildStage::None;
    std::string fallback_reason;  // 成功亦可填：如 tess 回退、部分字跳过
    int verts = 0;
    int tris = 0;
    unsigned int glyph_index = 0;  // 0 = 未绑定到单个 glyph

    void reset();
    void set_ok(int vertex_count, int triangle_count);
    void set_fail(BuildStage fail_stage, const char* reason = nullptr,
                  unsigned int glyph = 0);
};

const char* build_stage_name(BuildStage stage);
std::string build_result_format(const BuildResult& r);

}  // namespace text3d
