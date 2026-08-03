/*
 * 性能统计实现：计时辅助与日志格式化。
 */

#include "perf/perf_stats.h"

#include <sstream>
#include <iomanip>

namespace text3d {

void RebuildTimings::reset() {
    stage_2d_ms = 0.f;
    stage_3d_ms = 0.f;
    stage_merge_ms = 0.f;
    stage_upload_ms = 0.f;
    total_ms = 0.f;
}

float RebuildTimings::stages_sum_ms() const {
    return stage_2d_ms + stage_3d_ms + stage_merge_ms + stage_upload_ms;
}

ScopedTimer::ScopedTimer(float* dest_ms) : dest_ms_(dest_ms), t0_(std::chrono::steady_clock::now()) {}

ScopedTimer::~ScopedTimer() {
    if (!dest_ms_) {
        return;
    }
    const auto t1 = std::chrono::steady_clock::now();
    *dest_ms_ += std::chrono::duration<float, std::milli>(t1 - t0_).count();
}

namespace {

std::string fmt_ms(float v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

}  // namespace

std::string perf_stats_format_rebuild_log(const PerfStats& perf) {
    const auto& r = perf.rebuild;
    std::ostringstream oss;
    oss << "rebuild_total=" << fmt_ms(r.total_ms) << "ms"
        << " 2d=" << fmt_ms(r.stage_2d_ms) << "ms"
        << " 3d=" << fmt_ms(r.stage_3d_ms) << "ms"
        << " merge=" << fmt_ms(r.stage_merge_ms) << "ms"
        << " upload=" << fmt_ms(r.stage_upload_ms) << "ms"
        << " verts=" << perf.verts << " tris=" << perf.tris
        << " outline_hit=" << fmt_ms(perf.cache_outline.hit_rate * 100.f) << "%"
        << "(" << perf.cache_outline.hits << "/" << perf.cache_outline.lookups << ")"
        << " planar_hit=" << fmt_ms(perf.cache_planar.hit_rate * 100.f) << "%"
        << "(" << perf.cache_planar.hits << "/" << perf.cache_planar.lookups << ")"
        << " mesh_hit=" << fmt_ms(perf.cache_mesh.hit_rate * 100.f) << "%"
        << "(" << perf.cache_mesh.hits << "/" << perf.cache_mesh.lookups << ")"
        << " cache_mb=" << fmt_ms(perf.cache_outline.mb_used + perf.cache_planar.mb_used +
                                  perf.cache_mesh.mb_used)
        << " gpu_up_skip=" << perf.upload_slots_skipped
        << " gpu_up=" << perf.upload_slots_uploaded
        << " gpu_unique=" << perf.upload_gpu_unique_buffers
        << " gpu_bytes=" << perf.upload_bytes
        << " draw_call=" << perf.draw_batches
        << " gl_draws=" << perf.gl_draw_calls;
    return oss.str();
}

std::string perf_stats_format_frame_log(const PerfStats& perf) {
    std::ostringstream oss;
    oss << "ui_fps=" << fmt_ms(perf.ui_fps)
        << " ui_frame=" << fmt_ms(perf.ui_frame_ms) << "ms"
        << " render_submit=" << fmt_ms(perf.render_submit_ms) << "ms"
        << " text_draw=" << fmt_ms(perf.text_draw_ms) << "ms"
        << " offscreen_pass=" << fmt_ms(perf.offscreen_pass_ms) << "ms"
        << " offscreen_compose=" << fmt_ms(perf.offscreen_compose_ms) << "ms"
        << " present=" << fmt_ms(perf.present_ms) << "ms";
    return oss.str();
}

std::string perf_stats_format_full_log(const PerfStats& perf) {
    std::ostringstream oss;
    oss << perf_stats_format_frame_log(perf) << " | "
        << perf_stats_format_rebuild_log(perf);
    return oss.str();
}

}  // namespace text3d
