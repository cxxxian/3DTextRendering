/*
 * Bench Markdown 报告写出。
 */

#include "bench/bench_runner.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace text3d {
namespace {

std::string fmt2(float v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

const char* yn(bool ok) {
    return ok ? "PASS" : "FAIL";
}

}  // namespace

bool write_bench_report_markdown(const std::string& path, const BenchOptions& opt,
                                 const std::vector<BenchSceneResult>& results,
                                 const BenchGateResult& gates) {
    std::ofstream ofs(path);
    if (!ofs) {
        return false;
    }

    ofs << "# 固定场景性能报告 (#15)\n\n";
    ofs << "- Build: **" << opt.build_type << "**\n";
    ofs << "- VSync: **off**（bench 默认，便于量真耗时）\n";
    ofs << "- Warmup / Sample: " << opt.warmup_frames << " / " << opt.sample_frames << "\n";
    ofs << "- Canvas: offscreen blank（#14）\n\n";

    ofs << "## Release 门槛（以 `plain` @720p 为准）\n\n";
    ofs << "| 门槛 | 实测 | 结果 |\n|------|------|------|\n";
    ofs << "| frame P95 ≤ 16.6ms | " << fmt2(gates.frame_p95) << " ms | "
        << yn(gates.frame_p95_ok) << " |\n";
    ofs << "| render_submit P95 ≤ 4ms | " << fmt2(gates.submit_p95) << " ms | "
        << yn(gates.submit_p95_ok) << " |\n";
    ofs << "| FPS(from frame P50) ≥ 55 | " << fmt2(gates.fps) << " | " << yn(gates.fps_ok)
        << " |\n\n";

    ofs << "## 分场景\n\n";
    ofs << "| 场景 | frame P50/P95/P99 | FPS | submit P95 | draw P95 | DC "
           "logic/gl | upload∅ | cache o/p/m | latency P95 | note |\n";
    ofs << "|------|-------------------|-----|------------|----------|-------------|"
           "---------|-------------|-------------|------|\n";

    for (const auto& r : results) {
        ofs << "| `" << r.id << "` " << r.title << " | " << fmt2(r.frame_ms.p50) << " / "
            << fmt2(r.frame_ms.p95) << " / " << fmt2(r.frame_ms.p99) << " | "
            << fmt2(r.fps_from_p50) << " | " << fmt2(r.render_submit_ms.p95) << " | "
            << fmt2(r.text_draw_ms.p95) << " | " << r.draw_batches << "/" << r.gl_draw_calls
            << " | " << fmt2(r.upload_bytes_mean) << " | " << fmt2(r.outline_hit * 100.f)
            << "%/" << fmt2(r.planar_hit * 100.f) << "%/" << fmt2(r.mesh_hit * 100.f) << "% | ";
        if (r.latency_ms.count > 0) {
            ofs << fmt2(r.latency_ms.p95);
        } else {
            ofs << "-";
        }
        ofs << " | ";
        if (!r.ok) {
            ofs << "FAIL: " << r.note;
        } else if (r.id == "xform") {
            ofs << "applies=" << r.applies_during_sample;
        } else if (!r.note.empty()) {
            ofs << r.note;
        } else {
            ofs << "OK";
        }
        ofs << " |\n";
    }

    ofs << "\n## 未达标说明\n\n";
    const bool all_ok = gates.frame_p95_ok && gates.submit_p95_ok && gates.fps_ok;
    if (all_ok) {
        ofs << "本次 `plain` 门槛全部 PASS。\n\n";
    } else {
        ofs << "| 项 | 内容 |\n|----|------|\n";
        ofs << "| 优化前 | N/A（本轮首次自动化基线） |\n";
        ofs << "| 优化后 | 见上表本次 " << opt.build_type << " |\n";
        ofs << "| 剩余瓶颈 | ";
        if (!gates.frame_p95_ok) {
            ofs << "frame P95 超标；";
        }
        if (!gates.submit_p95_ok) {
            ofs << "render_submit P95 超标；";
        }
        if (!gates.fps_ok) {
            ofs << "FPS 偏低；";
        }
        ofs << " |\n";
        ofs << "| 下一步 | 对照 Text Draw / Present / Upload；#11 容量复用；复杂字缓存命中 |\n\n";
    }

    ofs << "## 备注\n\n";
    ofs << "- 主线程阻塞代理：稳态 `render_submit_ms`（含文字 draw + present；不含 worker 上 2d/3d）。\n";
    ofs << "- `typing` / `geom` 的 latency = submit/reload 到 apply 墙钟时间。\n";
    return true;
}

}  // namespace text3d
