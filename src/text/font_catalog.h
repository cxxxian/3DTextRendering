#pragma once
/*
 * 扫描 assets/fonts 下的素材字体包，供 UI 下拉选用。
 */

#include <string>
#include <vector>

namespace text3d {

struct FontEntry {
    std::string id;     // 目录名，如 "184109"
    std::string label;  // material.json 的 nickname，用于下拉显示
    std::string path;   // package/*.ttf|otf 路径
};

/* roots 可传多个候选目录（源码 assets / 可执行旁 assets） */
std::vector<FontEntry> scan_font_assets(const std::vector<std::string>& roots);

}  // namespace text3d
