#!/usr/bin/env python3
"""按 deps.json 拉取第三方依赖到 third_party/（不提交实体，仅本地使用）。"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEPS_JSON = ROOT / "deps.json"


def log(msg: str) -> None:
    print(f"[fetch_deps] {msg}", flush=True)


def download(url: str, out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    log(f"download {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "3d-text-demo-fetch_deps/1.0"})
    with urllib.request.urlopen(req, timeout=120) as resp, open(out, "wb") as f:
        shutil.copyfileobj(resp, f)


def extract_archive(archive: Path, dest: Path, strip_components: int) -> None:
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="deps_extract_") as tmp:
        tmp_path = Path(tmp)
        try:
            with tarfile.open(archive, "r:*") as tf:
                tf.extractall(tmp_path)
        except tarfile.ReadError:
            with zipfile.ZipFile(archive, "r") as zf:
                zf.extractall(tmp_path)

        src = tmp_path
        # 常见：压缩包内单顶层目录 name-version/
        entries = [p for p in src.iterdir()]
        if len(entries) == 1 and entries[0].is_dir():
            src = entries[0]
            strip_components = max(strip_components - 1, 0)

        for _ in range(strip_components):
            kids = [p for p in src.iterdir()]
            if len(kids) == 1 and kids[0].is_dir():
                src = kids[0]
            else:
                break

        for item in src.iterdir():
            target = dest / item.name
            if item.is_dir():
                shutil.copytree(item, target)
            else:
                shutil.copy2(item, target)


def already_ok(dep: dict) -> bool:
    dest = ROOT / dep["dest"]
    marker = dep.get("marker")
    if marker:
        return (dest / marker).is_file() if dep["type"] != "file" else dest.is_file()
    if dep["type"] == "file":
        return dest.is_file()
    return dest.is_dir() and any(dest.iterdir())


def fetch_archive(dep: dict, cache: Path, force: bool) -> None:
    dest = ROOT / dep["dest"]
    if not force and already_ok(dep):
        log(f"skip {dep['name']} (already present)")
        return

    filename = dep["url"].rstrip("/").split("/")[-1]
    if not filename.endswith((".gz", ".tgz", ".zip", ".tar")):
        filename = f"{dep['name']}-{dep['version']}.tar.gz"
    cached = cache / f"{dep['name']}-{dep['version']}-{filename}"
    if force or not cached.is_file():
        download(dep["url"], cached)
    extract_archive(cached, dest, int(dep.get("strip_components", 1)))
    log(f"ok {dep['name']} -> {dest}")


def fetch_file(dep: dict, cache: Path, force: bool) -> None:
    dest = ROOT / dep["dest"]
    if not force and dest.is_file():
        log(f"skip {dep['name']} (already present)")
        return
    cached = cache / f"{dep['name']}-{dep['version']}-{dest.name}"
    if force or not cached.is_file():
        download(dep["url"], cached)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(cached, dest)
    log(f"ok {dep['name']} -> {dest}")


def fetch_glad(dep: dict, force: bool) -> None:
    dest = ROOT / dep["dest"]
    marker = dest / dep.get("marker", "src/gl.c")
    if not force and marker.is_file():
        log("skip glad (already present)")
        return

    log("generate glad (pip install glad2 if needed)")
    subprocess.check_call(
        [sys.executable, "-m", "pip", "install", "--user", "-q", "glad2"],
        cwd=ROOT,
    )
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(
        [
            sys.executable,
            "-m",
            "glad",
            f"--api={dep.get('api', 'gl:core=3.3')}",
            f"--out-path={dest}",
            "--reproducible",
        ],
        cwd=ROOT,
    )
    # glad2 可能生成 include/KHR；确保 gl.c 路径符合 CMake
    if not (dest / "src" / "gl.c").is_file():
        raise RuntimeError(f"glad 生成结果不符合预期: 缺少 {dest / 'src' / 'gl.c'}")
    log(f"ok glad -> {dest}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch third_party deps from deps.json")
    parser.add_argument("--force", action="store_true", help="重新下载并覆盖已有目录")
    parser.add_argument("--only", nargs="*", help="只拉取指定 name")
    args = parser.parse_args()

    if not DEPS_JSON.is_file():
        log(f"missing {DEPS_JSON}")
        return 1

    data = json.loads(DEPS_JSON.read_text(encoding="utf-8"))
    cache = ROOT / data.get("cache_dir", ".deps_cache")
    cache.mkdir(parents=True, exist_ok=True)

    only = set(args.only) if args.only else None
    for dep in data["deps"]:
        if only and dep["name"] not in only:
            continue
        t = dep["type"]
        try:
            if t == "archive":
                fetch_archive(dep, cache, args.force)
            elif t == "file":
                fetch_file(dep, cache, args.force)
            elif t == "glad_generate":
                fetch_glad(dep, args.force)
            else:
                log(f"unknown type {t} for {dep['name']}")
                return 1
        except Exception as e:
            log(f"FAILED {dep['name']}: {e}")
            return 1

    # 写入简易锁文件，方便排查版本
    lock = {
        "deps": [{"name": d["name"], "version": d["version"], "type": d["type"]} for d in data["deps"]]
    }
    (ROOT / "third_party" / ".deps_lock.json").write_text(
        json.dumps(lock, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
