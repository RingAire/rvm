# 从 ringecho-lang-c 迁出说明

`rvm` 最初以 `src/exec/rvm_main.c` 的形式存在于编译器仓库
[ringecho-lang-c](https://github.com/RingAire/ringecho-lang-c)，由同一条
`make` 构建管线产出 `rvm` 二进制并随安装包分发。本次迁移把它拆成独立项目。

## 迁移了什么

- 源码：`src/exec/rvm_main.c` → 本仓库 `src/main.c`。
- 构建：新增独立 `Makefile`（只编译 `src/main.c`，不再依赖编译器的
  `include/` 与 `src/`）。
- 自包含：平台判定改为本文件内的 `_WIN32` / `__APPLE__` / `__linux__` 分支，
  不再包含 `platform.h`、`base/safe.h`。
- 测试：新增 `tests/test_distribution.py`，用本地 HTTP 服务器覆盖
  “index.txt / 资产命名 / 安装 / 回滚 / 显式 base 不回退”。

## 迁移时顺手修掉的缺陷

1. **Windows 盘符路径**：`make_path` 从 `tmp+1` 开始创建目录，遇到
   `C:\Users\name` 时先尝试 `mkdir("C:")` 并失败，导致默认 Windows 账户下
   `rvm install` 不可用。现在跳过盘符（以及 UNC 的 `\\server\share`）后再逐级创建。
2. **失败残留**：下载/解压失败会留下空的版本目录，重试被误报“已安装”。现在
   统一回滚，并在检测到半成品目录时提示后重装。
3. **卸载残留选择**：`rvm uninstall` 不清 `current`。现在若卸载的是当前版本，
   会移除链接与标记并提示重新选择。
4. **`list` 混入非目录**：现在只列出目录。
5. **Unix 资产后缀**：Unix 发布物是 `.tar.gz`，原先固定拼 `.zip`；现在按平台选择。

## 编译器仓库的相应变化

- `make build` 只产出 `rev`、`rem`（不再构建 `rvm`）。
- `scripts/ci/package.py` 的 `TOOLS` 只含 `rev`、`rem`；安装包与便携包不再包含
  `rvm`，需要 rvm 的用户单独安装。
- 文档（README 三语、BUILD、INSTALL）移除三件套描述，指向本仓库。

## 兼容性

- 命令、`~/.rvm` 目录布局、`index.txt` 契约保持不变；旧版 `rvm` 可直接替换为本
  仓库构建的二进制。
- `RVM_DIST_BASE` 为新增能力，旧版本忽略它。

## 版本策略

- `rvm` 使用独立版本号（本仓库 tag `v*`），与编译器版本解耦。
- 兼容性由“发布契约”保证：`index.txt` 行格式与资产路径。契约变更时在本仓库
  README 中标注，并保持至少一个 major 的向后兼容。

## 回滚

如需回到主线内置 rvm：在编译器仓库恢复 `rvm` 构建目标与 `TOOLS` 条目，并把
`src/main.c` 复制回 `src/exec/rvm_main.c`（恢复 `platform.h` 包含即可）。
