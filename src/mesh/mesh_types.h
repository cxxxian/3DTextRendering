#pragma once
/*
 * 可上传 GPU 的网格数据类型。
 * 挤出 / 排版 / Renderer 共用；不含生成逻辑。
 */

#include <vector>

namespace text3d {

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

/* 三角网格：indices 每 3 个一组 */
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

}  // namespace text3d
