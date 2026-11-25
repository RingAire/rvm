简体中文 | [English](README.en.md)

# rvm — 管理 RingEcho 编译器版本

rvm 用来安装、切换 RingEcho 编译器的不同版本。用法和 nvm 管理 Node 版本类似：
一条命令装某个版本，一条命令切到指定的版本，不需要由你修改环境。

## 安装

### Windows

在 PowerShell 里运行（只安装到用户，不需要更高权限）：

```powershell
irm https://github.com/RingAire/rvm/releases/latest/download/install.ps1 -OutFile install.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
```

安装完成后：

- 程序会被安装在 `%LOCALAPPDATA%\Programs\rvm`；
- 这个目录会添加至你的用户 PATH，新开一个终端加载新变量后就能使用 `rvm`；
- “设置 → 应用”里会出现 rvm，你可以从那里卸载，也可以运行这个目录里的脚本
  `uninstall.ps1`。

### macOS / Linux 安装

```sh
curl -fsSL https://github.com/RingAire/rvm/releases/latest/download/install.sh | sh
```

默认装到 `~/.local/bin`。想换目录或指定版本，先把脚本下载下来再运行：

```sh
curl -fsSL https://github.com/RingAire/rvm/releases/latest/download/install.sh -o install.sh
# 指定目录
sh install.sh --prefix /usr/local
# 卸载 rvm
sh install.sh --uninstall
```

如果 `~/.local/bin` 不在 PATH 里，照着脚本最后的提示，把那行加到
`~/.zshrc` 或 `~/.bashrc`。

## 常用命令

| 命令 | 作用 |
|------|------|
| `rvm install <版本>` | 装一个 RingEcho 版本 |
| `rvm use <版本>` | 切换到已装的版本 |
| `rvm list` | 看装了哪些版本 |
| `rvm current` | 看当前用的是哪个版本 |
| `rvm uninstall <版本>` | 删掉某个版本 |
| `rvm remote` | 看有哪些版本可以装 |
| `rvm version` | 看 rvm 自己的版本 |

## 版本从哪里下载

默认去 `https://ringecho-lang.org/dist` 找。这个网站打不开时，会自动改用
GitHub 的发布页，并告诉你它换了地址。

想用镜像，或者自己搭个测试服务器，可以设置 `RVM_DIST_BASE`：

```sh
RVM_DIST_BASE=http://127.0.0.1:8000 rvm install 0.3.0
```

设置了这个变量后，只认你给的地址，不会偷偷换回 GitHub。

## 自己编译

```sh
make                 # 生成 target/Release/rvm
make CONFIG=Debug
make test            # 跑安装、下载等测试，不联网
```

需要 C11 编译器、`curl` 和 `tar`（Windows 自带）。

## 安全说明

- 安装脚本会核对下载文件的 SHA256 指纹，对不上就停下。
- `rvm install` 安装编译器版本时，目前还没有核对指纹。
- rvm 只负责下载和切换编译器，不会运行你的项目代码。

## 许可证

Apache-2.0，见 `LICENSE` 和 `NOTICE`。
