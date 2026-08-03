#pragma once
/*
 * 分阶段性能统计：重建流水线拆分 + 每帧 UI / 渲染提交耗时。
 */

#include <chrono>
#include <cstdint>
#include <string>

namespace text3d {

struct RebuildTimings {
    float stage_2d_ms = 0.f;     // shape + outline + 内缩/三角化
    float stage_3d_ms = 0.f;     // 挤出、侧墙、倒角/圆角几何
    float stage_merge_ms = 0.f;  // 整句 mesh 合并
    float stage_upload_ms = 0.f; // GPU buffer 上传
    float total_ms = 0.f;        // rebuild 端到端

    void reset();
    float stages_sum_ms() const;
};

struct CacheLayerPerf {
    float hit_rate = 0.f;
    std::uint64_t lookups = 0;
    std::uint64_t hits = 0;
    int entries = 0;
    float mb_used = 0.f;
    float mb_limit = 0.f;
};

struct PerfStats {
    float ui_fps = 0.f;
    float ui_frame_ms = 0.f;
    float render_submit_ms = 0.f;
    float text_draw_ms = 0.f;

    RebuildTimings rebuild;

    float offscreen_pass_ms = 0.f;     // #14：ensure/bind/clear
    float offscreen_compose_ms = 0.f;  // #14：底图合成（本轮空白画布恒 0）
    float present_ms = 0.f;            // #14：Screen Pass 贴窗

    CacheLayerPerf cache_outline;
    CacheLayerPerf cache_planar;
    CacheLayerPerf cache_mesh;

    int upload_slots_skipped = 0;
    int upload_slots_uploaded = 0;       // #12：唯一几何上传数
    int upload_gpu_unique_buffers = 0;   // #12：当前活跃唯一 GPU Mesh
    int draw_batches = 0;                // #13：逻辑 Draw Call（Glass 计 1）
    int gl_draw_calls = 0;               // #13：真实 glDraw*（Glass ×2）
    std::uint64_t upload_bytes = 0;

    int verts = 0;
    int tris = 0;
};

class ScopedTimer {
public:
    explicit ScopedTimer(float* dest_ms);
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    float* dest_ms_;
    std::chrono::steady_clock::time_point t0_;
};

std::string perf_stats_format_rebuild_log(const PerfStats& perf);
std::string perf_stats_format_frame_log(const PerfStats& perf);
std::string perf_stats_format_full_log(const PerfStats& perf);

}  // namespace text3d
