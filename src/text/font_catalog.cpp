/*
 * 字体包扫描：读 material.json 昵称，定位 package 内 ttf/otf。
 */

#include "text/font_catalog.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace text3d {
namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

/* 不引 JSON 库：只抠 "nickname" : "..." */
std::string parse_nickname(const std::string& json) {
    const std::string key = "\"nickname\"";
    const auto key_pos = json.find(key);
    if (key_pos == std::string::npos) {
        return {};
    }
    const auto colon = json.find(':', key_pos + key.size());
    if (colon == std::string::npos) {
        return {};
    }
    const auto q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) {
        return {};
    }
    const auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) {
        return {};
    }
    return json.substr(q1 + 1, q2 - q1 - 1);
}

fs::path find_font_file(const fs::path& package_dir) {
    if (!fs::is_directory(package_dir)) {
        return {};
    }
    for (const auto& ent : fs::directory_iterator(package_dir)) {
        if (!ent.is_regular_file()) {
            continue;
        }
        const auto ext = ent.path().extension().string();
        if (ext == ".ttf" || ext == ".otf" || ext == ".TTF" || ext == ".OTF") {
            return ent.path();
        }
    }
    return {};
}

void scan_one_root(const fs::path& fonts_root, std::vector<FontEntry>& out) {
    if (!fs::is_directory(fonts_root)) {
        return;
    }
    std::error_code ec;
    for (const auto& ent : fs::directory_iterator(fonts_root, ec)) {
        if (!ent.is_directory()) {
            continue;
        }
        const fs::path dir = ent.path();
        const fs::path material = dir / "material.json";
        const fs::path font_file = find_font_file(dir / "package");
        if (font_file.empty()) {
            continue;
        }

        FontEntry fe;
        fe.id = dir.filename().string();
        // 统一成绝对路径，避免相对/绝对各扫一遍导致下拉重复
        std::error_code canon_ec;
        const fs::path canon = fs::weakly_canonical(font_file, canon_ec);
        fe.path = canon_ec ? font_file.string() : canon.string();
        if (fs::is_regular_file(material)) {
            fe.label = parse_nickname(read_file(material));
        }
        if (fe.label.empty()) {
            fe.label = fe.id + " (" + font_file.filename().string() + ")";
        } else {
            fe.label = fe.label + " [" + fe.id + "]";
        }
        out.push_back(std::move(fe));
    }
}

}  // namespace

std::vector<FontEntry> scan_font_assets(const std::vector<std::string>& roots) {
    std::vector<FontEntry> out;
    for (const auto& root : roots) {
        // 既支持传 assets，也支持传 assets/fonts
        const fs::path p(root);
        if ((p.filename() == "fonts") || fs::is_directory(p / "package")) {
            scan_one_root(p, out);
        } else {
            scan_one_root(p / "fonts", out);
        }
    }

    // 去重：同一 id 或同一规范化 path 只留一份
    std::vector<FontEntry> unique;
    for (auto& fe : out) {
        bool seen = false;
        for (const auto& u : unique) {
            if (u.id == fe.id || u.path == fe.path) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            unique.push_back(std::move(fe));
        }
    }

    std::cout << "[font_catalog] 扫描到 " << unique.size() << " 套字体\n";
    for (const auto& fe : unique) {
        std::cout << "  - " << fe.label << " → " << fe.path << "\n";
    }
    return unique;
}

}  // namespace text3d
