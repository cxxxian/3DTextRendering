#pragma once
/*
 * #15 固定场景跑分：状态机驱动 EditParams，采样 PerfStats，汇总百分位。
 */

#include "perf/perf_stats.h"
#include "ui/debug_ui.h"

#include <string>
#include <vector>

namespace text3d {

struct BenchOptions {
    int warmup_frames = 30;
    int sample_frames = 120;
    std::string out_path;
    std::string build_type = "Unknown";
};

struct PercentileStats {
    float p50 = 0.f;
    float p95 = 0.f;
    float p99 = 0.f;
    float mean = 0.f;
    float min_v = 0.f;
    float max_v = 0.f;
    int count = 0;
};

struct BenchSceneResult {
    std::string id;
    std::string title;
    PercentileStats frame_ms;
    PercentileStats render_submit_ms;
    PercentileStats text_draw_ms;
    PercentileStats present_ms;
    PercentileStats latency_ms;
    float fps_from_p50 = 0.f;
    int draw_batches = 0;
    int gl_draw_calls = 0;
    float outline_hit = 0.f;
    float planar_hit = 0.f;
    float mesh_hit = 0.f;
    float upload_bytes_mean = 0.f;
    int applies_during_sample = 0;  // xform 期望 0
    bool ok = true;
    std::string note;
};

struct BenchGateResult {
    bool frame_p95_ok = false;
    bool submit_p95_ok = false;
    bool fps_ok = false;
    float frame_p95 = 0.f;
    float submit_p95 = 0.f;
    float fps = 0.f;
};

struct BenchFrameContext {
    EditParams* edit = nullptr;
    const PerfStats* perf = nullptr;
    double now = 0.0;
    bool mesh_busy = false;
    std::string applied_text;
    float applied_depth = 0.f;
    float applied_inflate = 0.f;
    int applied_anim_index = -1;
    bool just_applied = false;
    float apply_upload_ms = 0.f;
    bool* reload_mesh = nullptr;
    bool* request_orbit = nullptr;  // main 微转相机
};

PercentileStats percentile_stats(std::vector<float> values);

class BenchRunner {
public:
    void configure(const BenchOptions& opt);
    void start();
    /* 每帧在 render 指标写完后调用；返回 false 表示全部结束 */
    bool tick(BenchFrameContext& ctx);
    bool finished() const { return finished_; }
    const std::vector<BenchSceneResult>& results() const { return results_; }
    const BenchOptions& options() const { return opt_; }
    BenchGateResult evaluate_gates() const;

private:
    enum class Phase { Enter, WaitReady, Warmup, Sample, Advance };

    void enter_scene_(BenchFrameContext& ctx);
    void apply_scene_edit_(BenchFrameContext& ctx);
    bool scene_ready_(const BenchFrameContext& ctx) const;
    void push_sample_(const BenchFrameContext& ctx);
    void finalize_scene_();
    void setup_typing_step_(BenchFrameContext& ctx);
    void setup_geom_step_(BenchFrameContext& ctx);

    BenchOptions opt_;
    std::vector<std::string> scene_ids_;
    int scene_index_ = 0;
    Phase phase_ = Phase::Enter;
    int phase_frames_ = 0;
    int typing_step_ = 0;
    int geom_step_ = 0;
    bool pending_latency_ = false;
    double latency_t0_ = 0.0;
    bool finished_ = false;

    BenchSceneResult current_;
    std::vector<float> frames_;
    std::vector<float> submits_;
    std::vector<float> draws_;
    std::vector<float> presents_;
    std::vector<float> latencies_;
    std::vector<float> uploads_;
    int applies_ = 0;

    std::vector<BenchSceneResult> results_;
};

bool write_bench_report_markdown(const std::string& path, const BenchOptions& opt,
                                 const std::vector<BenchSceneResult>& results,
                                 const BenchGateResult& gates);

}  // namespace text3d
