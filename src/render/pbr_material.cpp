/*
 * PBR 材质扫描与加载：按文件名约定找贴图，分拆 metallic/roughness 时合成 ORM。
 */

#include "render/pbr_material.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <vector>

#include <glad/gl.h>

#include "stb_image.h"

namespace text3d {
namespace {

namespace fs = std::filesystem;

const std::array<const char*, 3> kImgExt = {".png", ".jpg", ".jpeg"};

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/* stem 精确匹配（忽略大小写），先试常见扩展再扫目录 */
fs::path find_named_image(const fs::path& dir, const char* stem) {
    if (!fs::is_directory(dir)) {
        return {};
    }
    const std::string want = to_lower(stem);
    for (const char* ext : kImgExt) {
        fs::path p = dir / (std::string(stem) + ext);
        if (fs::is_regular_file(p)) {
            return p;
        }
    }
    for (const auto& ent : fs::directory_iterator(dir)) {
        if (!ent.is_regular_file()) {
            continue;
        }
        if (to_lower(ent.path().stem().string()) != want) {
            continue;
        }
        const std::string el = to_lower(ent.path().extension().string());
        if (el == ".png" || el == ".jpg" || el == ".jpeg") {
            return ent.path();
        }
    }
    return {};
}

fs::path find_first_named(const fs::path& dir, std::initializer_list<const char*> stems) {
    for (const char* s : stems) {
        fs::path p = find_named_image(dir, s);
        if (!p.empty()) {
            return p;
        }
    }
    return {};
}

/* 文件名需同时包含所有关键词（如 ao + rough + metal） */
fs::path find_by_keywords(const fs::path& dir, std::initializer_list<const char*> keywords_all) {
    if (!fs::is_directory(dir)) {
        return {};
    }
    for (const auto& ent : fs::directory_iterator(dir)) {
        if (!ent.is_regular_file()) {
            continue;
        }
        const std::string el = to_lower(ent.path().extension().string());
        if (el != ".png" && el != ".jpg" && el != ".jpeg") {
            continue;
        }
        const std::string stem = to_lower(ent.path().stem().string());
        bool ok = true;
        for (const char* kw : keywords_all) {
            if (stem.find(to_lower(kw)) == std::string::npos) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return ent.path();
        }
    }
    return {};
}

bool has_color_map(const fs::path& dir) {
    return !find_first_named(dir, {"diffuse", "albedo", "basecolor", "base_color"}).empty();
}

}  // namespace

void PbrMaterialGpu::destroy() {
    albedo.destroy();
    orm.destroy();
    normal.destroy();
    has_albedo = false;
    has_orm = false;
    has_normal = false;
    packed_layout = PackedMrLayout::Orm;
    name.clear();
}

std::vector<PbrMaterialEntry> scan_pbr_materials(const std::string& root) {
    std::vector<PbrMaterialEntry> out;
    const fs::path root_path(root);
    if (!fs::is_directory(root_path)) {
        std::cerr << "[pbr_material] 目录不存在: " << root << "\n";
        return out;
    }

    for (const auto& ent : fs::directory_iterator(root_path)) {
        if (!ent.is_directory()) {
            continue;
        }
        if (!has_color_map(ent.path())) {
            continue;
        }
        PbrMaterialEntry e;
        e.name = ent.path().filename().string();
        e.dir = ent.path().string();
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const PbrMaterialEntry& a, const PbrMaterialEntry& b) { return a.name < b.name; });
    std::cout << "[pbr_material] 找到 " << out.size() << " 套材质 @ " << root << "\n";
    return out;
}

bool load_pbr_material(const std::string& dir, const std::string& name, PbrMaterialGpu& out) {
    out.destroy();
    out.name = name;
    const fs::path d(dir);

    const fs::path albedo_path =
        find_first_named(d, {"diffuse", "albedo", "basecolor", "base_color"});
    if (albedo_path.empty()) {
        std::cerr << "[pbr_material] 缺少 diffuse/albedo: " << dir << "\n";
        return false;
    }
    if (!load_texture_2d(albedo_path.string(), out.albedo, /*srgb=*/true)) {
        out.destroy();
        return false;
    }
    out.has_albedo = true;

    // 优先 ORM（素材站 AO/Rough/Metal）
    fs::path orm_path = find_first_named(
        d, {"ao_rough_metal", "ao_roughness_metallic", "aoroughmetal", "orm", "arm"});
    if (orm_path.empty()) {
        orm_path = find_by_keywords(d, {"ao", "rough", "metal"});
    }
    if (!orm_path.empty()) {
        if (load_texture_2d(orm_path.string(), out.orm, /*srgb=*/false)) {
            out.has_orm = true;
            out.packed_layout = PackedMrLayout::Orm;
            std::cout << "[pbr_material] ORM (AO/Rough/Metal): " << orm_path << "\n";
        }
    }

    // 回退：旧 glTF metallic_roughness
    if (!out.has_orm) {
        const fs::path gltf = find_named_image(d, "metallic_roughness");
        if (!gltf.empty() && load_texture_2d(gltf.string(), out.orm, /*srgb=*/false)) {
            out.has_orm = true;
            out.packed_layout = PackedMrLayout::GltfMr;
            std::cout << "[pbr_material] glTF MR: " << gltf << "\n";
        }
    }

    // 再回退：分拆 metallic + roughness → 合成 ORM
    if (!out.has_orm) {
        const fs::path metal_p = find_first_named(d, {"metallic", "metalness", "metal"});
        const fs::path rou_p = find_first_named(d, {"roughness", "rough"});
        if (!metal_p.empty() || !rou_p.empty()) {
            int mw = 0, mh = 0, mn = 0, rw = 0, rh = 0, rn = 0;
            unsigned char* met = nullptr;
            unsigned char* rou = nullptr;
            stbi_set_flip_vertically_on_load(1);
            if (!metal_p.empty()) {
                met = stbi_load(metal_p.string().c_str(), &mw, &mh, &mn, 1);
            }
            if (!rou_p.empty()) {
                rou = stbi_load(rou_p.string().c_str(), &rw, &rh, &rn, 1);
            }
            const int w = met ? mw : rw;
            const int h = met ? mh : rh;
            if (w > 0 && h > 0 && (!met || !rou || (mw == rw && mh == rh))) {
                std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
                for (int i = 0; i < w * h; ++i) {
                    const unsigned char m = met ? met[i] : static_cast<unsigned char>(0);
                    const unsigned char r =
                        rou ? rou[i] : static_cast<unsigned char>(128);
                    px[static_cast<size_t>(i) * 4 + 0] = 255;  // AO
                    px[static_cast<size_t>(i) * 4 + 1] = r;    // Roughness
                    px[static_cast<size_t>(i) * 4 + 2] = m;    // Metallic
                    px[static_cast<size_t>(i) * 4 + 3] = 255;
                }
                unsigned int tex = 0;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             px.data());
                glGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glBindTexture(GL_TEXTURE_2D, 0);
                out.orm.id = tex;
                out.orm.width = w;
                out.orm.height = h;
                out.orm.channels = 4;
                out.has_orm = true;
                out.packed_layout = PackedMrLayout::Orm;
            }
            if (met) stbi_image_free(met);
            if (rou) stbi_image_free(rou);
        }
    }

    // 法线：优先 OpenGL 约定（Y 向上）
    fs::path nrm = find_first_named(d, {"normal_gl", "normalgl", "nor_gl", "normal"});
    if (nrm.empty()) {
        nrm = find_by_keywords(d, {"normal", "gl"});
    }
    if (nrm.empty()) {
        nrm = find_by_keywords(d, {"normal"});  // 可能是 DX，仍尝试
    }
    if (!nrm.empty() && load_texture_2d(nrm.string(), out.normal, /*srgb=*/false)) {
        out.has_normal = true;
        std::cout << "[pbr_material] normal: " << nrm << "\n";
    }

    return true;
}

}  // namespace text3d
