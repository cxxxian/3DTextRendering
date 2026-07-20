#pragma once
/*
 * 扫描 assets/textures/materials/<name>/ 并加载 albedo / ORM / normal。
 * 文件名约定：diffuse|albedo 必需；ao_rough_metal|orm|arm / metallic_roughness /
 * metallic+roughness / normal_gl|normal 可选（扩展名 .png/.jpg/.jpeg，大小写不敏感）。
 */

#include "render/texture.h"

#include <string>
#include <vector>

namespace text3d {

enum class PackedMrLayout {
    Orm,     // R=AO, G=Roughness, B=Metallic
    GltfMr,  // G=Metallic, B=Roughness（旧 glTF）
};

struct PbrMaterialGpu {
    std::string name;
    Texture2D albedo;
    Texture2D orm;  // 通道含义见 packed_layout
    Texture2D normal;
    PackedMrLayout packed_layout = PackedMrLayout::Orm;
    bool has_albedo = false;
    bool has_orm = false;
    bool has_normal = false;

    void destroy();
};

struct PbrMaterialEntry {
    std::string name;
    std::string dir;
};

std::vector<PbrMaterialEntry> scan_pbr_materials(const std::string& root);
bool load_pbr_material(const std::string& dir, const std::string& name, PbrMaterialGpu& out);

}  // namespace text3d
