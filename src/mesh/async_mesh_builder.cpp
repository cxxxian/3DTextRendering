/*
 * #10 异步 Mesh 构建：单 worker + latest-only 完成槽。
 */

#include "mesh/async_mesh_builder.h"

#include <chrono>
#include <utility>

namespace text3d {
namespace {

int tess_backend_index(TessMode mode) {
    if (mode == TessMode::Earcut) {
        return 1;
    }
    if (mode == TessMode::Libtess2) {
        return 2;
    }
    return 0;
}

void fill_layer_perf(CacheLayerPerf& dst, const CacheLayerStats& src) {
    dst.hit_rate = src.hit_rate();
    dst.lookups = src.lookups;
    dst.hits = src.hits;
    dst.entries = static_cast<int>(src.entries);
    dst.mb_used = static_cast<float>(src.bytes) / (1024.f * 1024.f);
    dst.mb_limit = static_cast<float>(src.byte_limit) / (1024.f * 1024.f);
}

}  // namespace

AsyncMeshBuilder::~AsyncMeshBuilder() {
    stop();
}

void AsyncMeshBuilder::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;
    worker_ = std::thread(&AsyncMeshBuilder::worker_loop, this);
}

void AsyncMeshBuilder::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
        cv_.notify_all();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::uint64_t AsyncMeshBuilder::submit(AsyncMeshBuildRequest req) {
    const std::uint64_t gen = generation_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    req.generation = gen;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_generation_.store(gen, std::memory_order_release);
        pending_ = std::move(req);
        has_pending_ = true;
        has_completed_ = false;
    }
    cv_.notify_one();
    return gen;
}

bool AsyncMeshBuilder::poll_result(AsyncMeshBuildResult& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_completed_) {
        return false;
    }
    const std::uint64_t latest = latest_generation_.load(std::memory_order_relaxed);
    if (completed_.generation != latest) {
        has_completed_ = false;
        return false;
    }
    out = std::move(completed_);
    has_completed_ = false;
    return true;
}

void AsyncMeshBuilder::fill_cache_snapshot(AsyncMeshBuildResult& out) const {
    const GlyphCacheStats cs = glyph_cache_.stats();
    fill_layer_perf(out.cache_outline, cs.outline);
    fill_layer_perf(out.cache_planar, cs.planar);
    fill_layer_perf(out.cache_mesh, mesh_pool_.stats());
}

AsyncMeshBuildResult AsyncMeshBuilder::build_on_worker(const AsyncMeshBuildRequest& req) {
    const auto t0 = std::chrono::steady_clock::now();

    AsyncMeshBuildResult out;
    out.generation = req.generation;
    out.text = req.text;
    out.depth = req.depth;
    out.bevel = req.bevel;
    out.fillet = req.fillet;
    out.inflate = req.inflate;
    out.tess_backend = tess_backend_index(req.tess_mode);
    out.use_per_glyph = req.per_glyph;
    out.timings.reset();
    out.build.reset();

    if (req.font_path != loaded_font_path_) {
        if (!font_.load(req.font_path, 128)) {
            out.ok = false;
            out.build.set_fail(BuildStage::Outline, "worker font load failed");
            fill_cache_snapshot(out);
            const auto t1 = std::chrono::steady_clock::now();
            out.timings.total_ms =
                std::chrono::duration<float, std::milli>(t1 - t0).count();
            return out;
        }
        loaded_font_path_ = req.font_path;
        font_.dump_info();
    }

    LayoutOptions opt;
    opt.extrude.depth = req.depth;
    opt.extrude.bevel = req.bevel;
    opt.extrude.fillet = req.fillet;
    opt.extrude.inflate = req.inflate;
    opt.extrude.tess_mode = req.tess_mode;
    opt.flatness = req.flatness;
    opt.scale = req.scale;
    opt.timings = &out.timings;
    opt.cache = &glyph_cache_;
    opt.mesh_pool = &mesh_pool_;
    opt.write_planar_cache = req.write_cache;
    opt.write_mesh_pool = req.write_cache;
    opt.out_applied_radius_min = &out.applied_r_min;
    opt.out_applied_radius_max = &out.applied_r_max;
    opt.out_safe_radius_min = &out.safe_r_min;
    opt.out_applied_inflate_h_max = &out.inflate_h_max;

    if (req.text.empty()) {
        out.ok = true;
        out.build.set_ok(0, 0);
        out.applied_r_min = out.applied_r_max = out.safe_r_min = out.inflate_h_max = 0.f;
    } else if (req.per_glyph) {
        out.ok = layout_text_glyphs(font_, req.text, opt, out.glyphs, &out.build);
        if (!out.ok) {
            out.glyphs.clear();
        }
    } else {
        out.ok = layout_text(font_, req.text, opt, out.merged_mesh, &out.build);
        if (!out.ok) {
            out.merged_mesh = Mesh{};
        }
    }

    fill_cache_snapshot(out);

    const auto t1 = std::chrono::steady_clock::now();
    out.timings.total_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return out;
}

void AsyncMeshBuilder::worker_loop() {
    for (;;) {
        AsyncMeshBuildRequest req;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || has_pending_; });
            if (!running_ && !has_pending_) {
                break;
            }
            req = std::move(pending_);
            has_pending_ = false;
        }

        for (;;) {
            busy_.store(true, std::memory_order_release);
            AsyncMeshBuildResult result = build_on_worker(req);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (result.generation == latest_generation_.load(std::memory_order_relaxed)) {
                    completed_ = std::move(result);
                    has_completed_ = true;
                }
                if (has_pending_) {
                    req = std::move(pending_);
                    has_pending_ = false;
                    continue;
                }
                busy_.store(false, std::memory_order_release);
                break;
            }
        }
    }

    busy_.store(false, std::memory_order_release);
}

}  // namespace text3d
