# rvm — RingEcho 版本管理器

`rvm` 是 [RingEcho](https://github.com/RingAire/ringecho-lang-c) 编译器工具链的
版本管理器，形态类似 `nvm`：安装、切换已发布的编译器版本，不需要接触编译器源码。

本项目是**独立仓库**，与编译器主线分离；它只消费发布契约，不编译编译器。

## 命令

```text
rvm install <版本>     安装一个已发布的 RingEcho 版本
rvm use <版本>         切换到已安装版本
rvm list               列出已安装版本
rvm current            显示当前选择
rvm uninstall <版本>   卸载版本（若为当前选择则一并清除）
rvm remote             列出已发布版本
rvm version            打印 rvm 版本
```

## 发布契约

所有版本都在同一个 base 下解析：

```
<base>/index.txt                                  每行一个版本
<base>/v<版本>/ringecho-<版本>-<平台>-<架构>.zip       Windows
<base>/v<版本>/ringecho-<版本>-<平台>-<架构>.tar.gz    Linux / macOS
```

- `<base>` 默认 `https://ringecho-lang.org/dist`。
- 站点不可达或资产缺失时，`install` 回退到 GitHub Releases 的同名资产并给出提示；
  显式设置 `RVM_DIST_BASE` 时不回退。
- `RVM_DIST_BASE` 也可用于镜像或本地测试：
  `RVM_DIST_BASE=http://127.0.0.1:8080 rvm install 0.3.0`。
- 安装布局：`~/.rvm/versions/<版本>/<payload>/bin`，通过 `~/.rvm/current` 选择。

## 构建

```sh
make                 # target/Release/rvm
make CONFIG=Debug
make test            # 分发契约测试（本地 HTTP 服务器，不联网）
```

依赖 C11 编译器、`curl`、`tar`（Windows 上为 `bsdtar`）。

## 安全说明

- 下载使用 `curl -fsL`；安装失败会回滚版本目录，半成品目录会在重试时被替换。
- **尚未实现 `SHA256SUMS` 校验**；在 manifest 落地前，该通道不提供完整性保证。
- `rvm` 只管理发布树里的 `rev`/`rem`，不执行项目代码。

## 许可证

Apache-2.0，见 `LICENSE` 与 `NOTICE`。
