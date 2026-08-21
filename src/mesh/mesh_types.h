#pragma once
/*
 * 可上传 GPU 的网格数据类型。
 * 挤出 / 排版 / Renderer 共用；不含生成逻辑。
 * MeshPart：记录各分区在 indices 中的半开区间 [begin, end)。
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace text3d {

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float tx = 1.f, ty = 0.f, tz = 0.f, tw = 1.f;  // tangent + handedness（法线贴图 TBN）
};

/* 挤出分区：帽面 / 侧墙 / 倒角棱 / 圆角棱 */
enum class MeshPart : std::uint8_t {
    Front = 0,    // 正面帽（+Z；仅 Inflate>0 时鼓包）
    Back,         // 背面帽（-Z）
    Side,         // 直立侧墙（直边或倒角/圆角后的中段墙）
    Bevel,        // 平倒角斜面棱带
    Rounded,      // 真圆角过渡棱带（不含帽面）
    Count
};

struct IndexRange {
    unsigned begin = 0;  // indices 下标，含
    unsigned end = 0;    // 不含
    bool empty() const { return begin >= end; }
    unsigned count() const { return end > begin ? end - begin : 0; }
};

/* 三角网格：indices 每 3 个一组；parts 为各分区 index 范围（可空） */
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    IndexRange parts[static_cast<std::size_t>(MeshPart::Count)]{};

    IndexRange part_range(MeshPart p) const;
    void set_part(MeshPart p, std::size_t begin, std::size_t end);
    void clear_parts();
};

const char* mesh_part_name(MeshPart p);
std::string mesh_parts_format(const Mesh& mesh);

/* 正面分区顶点法线是否接近平整 +Z（Inflate 鼓包会返回 false） */
bool mesh_front_normals_are_flat(const Mesh& mesh, float eps = 1e-3f);

/* 按三角 UV 累加切线并正交化；挤出完成后调用，供 PBR 法线贴图 */
void compute_mesh_tangents(Mesh& mesh);

}  // namespace text3d
