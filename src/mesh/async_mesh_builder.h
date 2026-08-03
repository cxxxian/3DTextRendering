#pragma once
/*
 * #10 异步 Mesh 构建：单工作线程 CPU layout；latest-only 丢弃过期结果。
 * GlyphGeometryCache / GlyphMeshPool / FontFace 由本类独占，仅 worker 访问。
 */

#include "mesh/build_result.h"
#include "mesh/glyph_geometry_cache.h"
#include "mesh/glyph_mesh_pool.h"
#include "mesh/mesh_offset_tess.h"
#include "mesh/mesh_types.h"
#include "perf/perf_stats.h"
#include "text/ft_outline.h"
#include "text/text_layout.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace text3d {

struct AsyncMeshBuildRequest {
    std::uint64_t generation = 0;
    std::string text;
    std::string font_path;
    float depth = 24.f;
    float bevel = 0.f;
    float fillet = 0.f;
    float inflate = 0.f;
    TessMode tess_mode = TessMode::Auto;
    float flatness = 0.4f;
    float scale = 1.f / 128.f;
    bool per_glyph = false;
    bool write_cache = true;
};

struct AsyncMeshBuildResult {
    std::uint64_t generation = 0;
    bool ok = false;
    BuildResult build;
    RebuildTimings timings;
    bool use_per_glyph = false;
    std::vector<GlyphInstance> glyphs;
    Mesh merged_mesh;
    float applied_r_min = 0.f;
    float applied_r_max = 0.f;
    float safe_r_min = 0.f;
    float inflate_h_max = 0.f;
    CacheLayerPerf cache_outline;
    CacheLayerPerf cache_planar;
    CacheLayerPerf cache_mesh;
    std::string text;
    float depth = 0.f;
    float bevel = 0.f;
    float fillet = 0.f;
    float inflate = 0.f;
    int tess_backend = 0;
};

class AsyncMeshBuilder {
public:
    AsyncMeshBuilder() = default;
    ~AsyncMeshBuilder();

    AsyncMeshBuilder(const AsyncMeshBuilder&) = delete;
    AsyncMeshBuilder& operator=(const AsyncMeshBuilder&) = delete;

    void start();
    void stop();

    std::uint64_t submit(AsyncMeshBuildRequest req);
    bool poll_result(AsyncMeshBuildResult& out);

    bool is_busy() const { return busy_.load(std::memory_order_acquire); }
    std::uint64_t latest_generation() const {
        return latest_generation_.load(std::memory_order_acquire);
    }

private:
    void worker_loop();
    AsyncMeshBuildResult build_on_worker(const AsyncMeshBuildRequest& req);
    void fill_cache_snapshot(AsyncMeshBuildResult& out) const;

    FontFace font_;
    std::string loaded_font_path_;
    GlyphGeometryCache glyph_cache_;
    GlyphMeshPool mesh_pool_;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = false;

    AsyncMeshBuildRequest pending_;
    bool has_pending_ = false;

    AsyncMeshBuildResult completed_;
    bool has_completed_ = false;

    std::atomic<std::uint64_t> generation_counter_{0};
    std::atomic<std::uint64_t> latest_generation_{0};
    std::atomic<bool> busy_{false};
};

}  // namespace text3d
