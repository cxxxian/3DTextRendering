/*
 * BenchRunner：7 场景状态机 + 百分位汇总。
 */

#include "bench/bench_runner.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace text3d {
namespace {

float percentile_sorted(const std::vector<float>& sorted, float p) {
    if (sorted.empty()) {
        return 0.f;
    }
    if (sorted.size() == 1) {
        return sorted[0];
    }
    const float idx = p * static_cast<float>(sorted.size() - 1);
    const size_t lo = static_cast<size_t>(idx);
    const size_t hi = std::min(lo + 1, sorted.size() - 1);
    const float t = idx - static_cast<float>(lo);
    return sorted[lo] * (1.f - t) + sorted[hi] * t;
}

void set_text(EditParams& edit, const char* s) {
    std::strncpy(edit.text, s, sizeof(edit.text) - 1);
    edit.text[sizeof(edit.text) - 1] = '\0';
}

const char* kTypingSteps[] = {"H", "He", "Hel", "Hell", "Hello", "Hello!"};
constexpr int kTypingStepCount = 6;

struct GeomStep {
    float depth;
    float inflate;
};
const GeomStep kGeomSteps[] = {
    {12.f, 0.f}, {24.f, 0.f}, {40.f, 0.f}, {24.f, 0.15f}, {24.f, 0.3f},
};
constexpr int kGeomStepCount = 5;

}  // namespace

PercentileStats percentile_stats(std::vector<float> values) {
    PercentileStats out;
    out.count = static_cast<int>(values.size());
    if (values.empty()) {
        return out;
    }
    float sum = 0.f;
    out.min_v = values[0];
    out.max_v = values[0];
    for (float v : values) {
        sum += v;
        out.min_v = std::min(out.min_v, v);
        out.max_v = std::max(out.max_v, v);
    }
    out.mean = sum / static_cast<float>(values.size());
    std::sort(values.begin(), values.end());
    out.p50 = percentile_sorted(values, 0.50f);
    out.p95 = percentile_sorted(values, 0.95f);
    out.p99 = percentile_sorted(values, 0.99f);
    return out;
}

void BenchRunner::configure(const BenchOptions& opt) {
    opt_ = opt;
}

void BenchRunner::start() {
    scene_ids_ = {"plain", "complex", "typing", "geom", "xform", "canvas_1080", "anim"};
    scene_index_ = 0;
    phase_ = Phase::Enter;
    phase_frames_ = 0;
    typing_step_ = 0;
    geom_step_ = 0;
    pending_latency_ = false;
    finished_ = false;
    results_.clear();
    current_ = {};
    frames_.clear();
    submits_.clear();
    draws_.clear();
    presents_.clear();
    latencies_.clear();
    uploads_.clear();
    applies_ = 0;
}

void BenchRunner::apply_scene_edit_(BenchFrameContext& ctx) {
    EditParams& e = *ctx.edit;
    e.use_offscreen_canvas = true;
    e.unlock_vsync = true;
    e.bevel = 0.f;
    e.fillet = 0.f;
    e.cache_write_geometry = true;
    e.shading = ShadingModel::Pbr;

    const std::string& id = scene_ids_[static_cast<size_t>(scene_index_)];
    if (id == "plain" || id == "xform" || id == "canvas_1080") {
        set_text(e, "Hello");
        e.depth = 24.f;
        e.inflate = 0.f;
        e.anim_index = 0;
        e.canvas_size_index = (id == "canvas_1080") ? 1 : 0;
        e.albedo[0] = 0.92f;
        e.albedo[1] = 0.88f;
        e.albedo[2] = 0.78f;
    } else if (id == "complex") {
        set_text(e, u8"赢田回国日");
        e.depth = 24.f;
        e.inflate = 0.f;
        e.anim_index = 0;
        e.canvas_size_index = 0;
    } else if (id == "typing") {
        setup_typing_step_(ctx);
        e.depth = 24.f;
        e.inflate = 0.f;
        e.anim_index = 0;
        e.canvas_size_index = 0;
    } else if (id == "geom") {
        setup_geom_step_(ctx);
        set_text(e, "Hello");
        e.anim_index = 0;
        e.canvas_size_index = 0;
    } else if (id == "anim") {
        set_text(e, "Hello");
        e.depth = 24.f;
        e.inflate = 0.f;
        e.anim_index = 1;
        e.canvas_size_index = 0;
        e.request_play = true;
    }

    if (ctx.reload_mesh) {
        *ctx.reload_mesh = true;
    }
    e.anim_changed = true;
}

void BenchRunner::setup_typing_step_(BenchFrameContext& ctx) {
    const int i = std::min(typing_step_, kTypingStepCount - 1);
    set_text(*ctx.edit, kTypingSteps[i]);
}

void BenchRunner::setup_geom_step_(BenchFrameContext& ctx) {
    const int i = std::min(geom_step_, kGeomStepCount - 1);
    ctx.edit->depth = kGeomSteps[i].depth;
    ctx.edit->inflate = kGeomSteps[i].inflate;
}

void BenchRunner::enter_scene_(BenchFrameContext& ctx) {
    const std::string& id = scene_ids_[static_cast<size_t>(scene_index_)];
    current_ = {};
    current_.id = id;
    if (id == "plain") {
        current_.title = "普通文字 Hello @720p";
    } else if (id == "complex") {
        current_.title = "复杂文字";
    } else if (id == "typing") {
        current_.title = "连续输入";
    } else if (id == "geom") {
        current_.title = "几何参数阶梯";
    } else if (id == "xform") {
        current_.title = "材质与变换（无 rebuild）";
    } else if (id == "canvas_1080") {
        current_.title = "Hello @1080p 画布";
    } else if (id == "anim") {
        current_.title = "Appear Spin 动画";
    }

    frames_.clear();
    submits_.clear();
    draws_.clear();
    presents_.clear();
    latencies_.clear();
    uploads_.clear();
    applies_ = 0;
    phase_frames_ = 0;
    typing_step_ = 0;
    geom_step_ = 0;
    pending_latency_ = false;

    apply_scene_edit_(ctx);
    pending_latency_ = (id == "typing" || id == "geom");
    latency_t0_ = ctx.now;
    phase_ = Phase::WaitReady;
    std::cout << "[bench] scene " << id << " enter\n";
}

bool BenchRunner::scene_ready_(const BenchFrameContext& ctx) const {
    if (ctx.mesh_busy) {
        return false;
    }
    const std::string& id = scene_ids_[static_cast<size_t>(scene_index_)];
    if (id == "typing") {
        return ctx.applied_text == ctx.edit->text;
    }
    if (id == "geom") {
        return ctx.applied_text == ctx.edit->text &&
               std::fabs(ctx.applied_depth - ctx.edit->depth) < 1e-3f &&
               std::fabs(ctx.applied_inflate - ctx.edit->inflate) < 1e-3f;
    }
    return ctx.applied_text == ctx.edit->text &&
           ctx.applied_anim_index == ctx.edit->anim_index;
}

void BenchRunner::push_sample_(const BenchFrameContext& ctx) {
    if (!ctx.perf) {
        return;
    }
    frames_.push_back(ctx.perf->ui_frame_ms);
    submits_.push_back(ctx.perf->render_submit_ms);
    draws_.push_back(ctx.perf->text_draw_ms);
    presents_.push_back(ctx.perf->present_ms);
    uploads_.push_back(static_cast<float>(ctx.perf->upload_bytes));
    current_.draw_batches = ctx.perf->draw_batches;
    current_.gl_draw_calls = ctx.perf->gl_draw_calls;
    current_.outline_hit = ctx.perf->cache_outline.hit_rate;
    current_.planar_hit = ctx.perf->cache_planar.hit_rate;
    current_.mesh_hit = ctx.perf->cache_mesh.hit_rate;
    if (ctx.just_applied) {
        ++applies_;
    }
}

void BenchRunner::finalize_scene_() {
    current_.frame_ms = percentile_stats(frames_);
    current_.render_submit_ms = percentile_stats(submits_);
    current_.text_draw_ms = percentile_stats(draws_);
    current_.present_ms = percentile_stats(presents_);
    current_.latency_ms = percentile_stats(latencies_);
    current_.upload_bytes_mean = percentile_stats(uploads_).mean;
    current_.applies_during_sample = applies_;
    if (current_.frame_ms.p50 > 1e-4f) {
        current_.fps_from_p50 = 1000.f / current_.frame_ms.p50;
    }
    if (current_.id == "xform" && applies_ > 0) {
        current_.ok = false;
        current_.note = "sample 期间发生 mesh apply，期望 0";
    }
    results_.push_back(current_);
    std::cout << "[bench] scene " << current_.id << " done frame_p95="
              << current_.frame_ms.p95 << "ms fps~" << current_.fps_from_p50 << "\n";
}

BenchGateResult BenchRunner::evaluate_gates() const {
    BenchGateResult g;
    for (const auto& r : results_) {
        if (r.id == "plain") {
            g.frame_p95 = r.frame_ms.p95;
            g.submit_p95 = r.render_submit_ms.p95;
            g.fps = r.fps_from_p50;
            g.frame_p95_ok = r.frame_ms.p95 <= 16.6f;
            g.submit_p95_ok = r.render_submit_ms.p95 <= 4.f;
            g.fps_ok = r.fps_from_p50 >= 55.f;
            break;
        }
    }
    return g;
}

bool BenchRunner::tick(BenchFrameContext& ctx) {
    if (finished_) {
        return false;
    }
    if (scene_index_ >= static_cast<int>(scene_ids_.size())) {
        finished_ = true;
        return false;
    }

    if (phase_ == Phase::Enter) {
        enter_scene_(ctx);
        return true;
    }

    const std::string& id = scene_ids_[static_cast<size_t>(scene_index_)];

    if (pending_latency_ && ctx.just_applied) {
        const float ms = static_cast<float>((ctx.now - latency_t0_) * 1000.0);
        latencies_.push_back(ms);
        pending_latency_ = false;
    }

    if (phase_ == Phase::WaitReady) {
        ++phase_frames_;
        if (phase_frames_ > 600) {
            current_.ok = false;
            current_.note = "WaitReady timeout";
            finalize_scene_();
            phase_ = Phase::Advance;
            return true;
        }
        if (scene_ready_(ctx)) {
            phase_frames_ = 0;
            if (id == "typing" && typing_step_ < kTypingStepCount - 1) {
                // 多步输入：就绪后立刻下一步，只采 latency；最后一步再 warmup/sample
                ++typing_step_;
                setup_typing_step_(ctx);
                if (ctx.reload_mesh) {
                    *ctx.reload_mesh = true;
                }
                pending_latency_ = true;
                latency_t0_ = ctx.now;
                phase_frames_ = 0;
                return true;
            }
            if (id == "geom" && geom_step_ < kGeomStepCount - 1) {
                ++geom_step_;
                setup_geom_step_(ctx);
                if (ctx.reload_mesh) {
                    *ctx.reload_mesh = true;
                }
                pending_latency_ = true;
                latency_t0_ = ctx.now;
                phase_frames_ = 0;
                return true;
            }
            phase_ = Phase::Warmup;
            phase_frames_ = 0;
            if (id == "anim") {
                ctx.edit->request_play = true;
            }
        }
        return true;
    }

    if (phase_ == Phase::Warmup) {
        ++phase_frames_;
        if (id == "xform" && ctx.request_orbit) {
            *ctx.request_orbit = true;
        }
        if (phase_frames_ >= opt_.warmup_frames) {
            phase_ = Phase::Sample;
            phase_frames_ = 0;
            frames_.clear();
            submits_.clear();
            draws_.clear();
            presents_.clear();
            uploads_.clear();
            applies_ = 0;
        }
        return true;
    }

    if (phase_ == Phase::Sample) {
        push_sample_(ctx);
        ++phase_frames_;
        if (id == "xform") {
            if (ctx.request_orbit) {
                *ctx.request_orbit = true;
            }
            // 轻微改 albedo，不应触发 mesh rebuild
            ctx.edit->albedo[0] = 0.85f + 0.1f * std::sin(static_cast<float>(phase_frames_) * 0.1f);
            ctx.edit->albedo[1] = 0.80f;
            ctx.edit->albedo[2] = 0.70f;
        }
        if (id == "anim") {
            ctx.edit->request_play = false;
        }
        if (phase_frames_ >= opt_.sample_frames) {
            finalize_scene_();
            phase_ = Phase::Advance;
        }
        return true;
    }

    if (phase_ == Phase::Advance) {
        ++scene_index_;
        if (scene_index_ >= static_cast<int>(scene_ids_.size())) {
            finished_ = true;
            return false;
        }
        phase_ = Phase::Enter;
        return true;
    }

    return true;
}

}  // namespace text3d
