<p align="center">
  <img src="assets/icon.png" width="120" alt="QQ APNG 藏图工具图标">
</p>

<h1 align="center">QQ APNG 藏图工具</h1>

<p align="center">一张封面，藏起整段动画。导入图片、GIF 或 APNG，生成适用于 QQ 聊天环境的藏图。</p>

<p align="center">
  <a href="https://github.com/ryujou/qq-apng-disguise/releases/latest"><img src="https://img.shields.io/github/v/release/ryujou/qq-apng-disguise?label=download" alt="下载"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20x64-0078D4" alt="Windows x64">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-green" alt="MIT"></a>
</p>

<p align="center"><a href="https://ryujou.github.io/qq-apng-disguise/">在线使用</a> · <a href="https://github.com/ryujou/qq-apng-disguise/releases/latest">下载 Windows EXE</a> · <a href="#使用方法">使用方法</a> · <a href="#运行源码">运行源码</a></p>

![Web 版界面](assets/screenshot-web.png)

## 效果与适用环境

在 QQ 聊天中，缩略预览显示封面，查看原图时显示隐藏图片或播放多图动画。

**发送步骤很重要：先将生成的 PNG 发送到手机，再在手机 QQ 中选择该图片，勾选「原图」后发送，才有藏图效果。直接从电脑本地发送不能替代这个步骤。**

**Python 版生成的图片已由作者在手机 QQ 实测通过。** 电脑 QQ 从本地发送时会直接显示隐藏图，原参考样图也有相同行为；原消息或转发消息保留封面不代表本地发送也可用。C++ 版使用相同 APNG 结构，客户端效果需要单独实测。具体 QQ 版本组合尚未记录。

## 版本

| 版本 | 下载文件 | 实现 |
| --- | --- | --- |
| Web 版 | [打开网页](https://ryujou.github.io/qq-apng-disguise/) | 在浏览器本地处理，支持电脑和手机，无需安装 |
| C++ 原生版 | `QQ-APNG-Disguise-CPP.exe` | C++17 / Win32 / Windows Imaging Component，无 Python、Qt 或额外运行库安装要求 |
| Python 版 | `QQ-APNG-Disguise.exe` | Python / Pillow / Tkinter，发行包已包含运行环境 |

C++ 和 Web 版支持独立封面、静态图片与 GIF／APNG 混合导入，按列表顺序循环播放，可批量选择或拖入文件并移除条目。GIF 的局部更新、透明叠加和恢复画面，以及 APNG 的混合与清除规则均会先合成为完整画面；APNG 的独立默认封面不参与播放。

默认保留动图各帧的原始时长。静态图片使用「图片时长」，范围 1–65535 ms，默认 100 ms；取消「保留动图原始时长」后，导入动画的每帧也统一使用这个时长。文件中的零时长按 100 ms 处理。画布采用第一项播放内容的尺寸，其余图片等比例适配、居中留白。

C++ 版通过 WIC 读取 PNG、JPEG、BMP、GIF、TIFF，WebP 取决于系统编解码器；Web 版支持 PNG、GIF、APNG，以及浏览器可读取的 JPEG、WebP 等静态图片。网页不上传图片，解码后的播放画面总量限制为 512 MB，过大时需减少帧数或缩小分辨率。

![C++ 多图播放界面](assets/screenshot-cpp.png)

### 编译 C++ 原生版

使用 Windows x64 的 MinGW-w64 工具链（例如 [w64devkit](https://github.com/skeeto/w64devkit)）：

```powershell
./cpp/build.ps1 -Toolchain "C:\tools\w64devkit\bin"
```

源码为 `cpp/main.cpp` 和 `cpp/animation.h`，图标沿用 `assets/icon.ico`。输出为 `dist/QQ-APNG-Disguise-CPP.exe`。编译及运行均不需要 Python。

也可以从命令行生成图片：

```powershell
./dist/QQ-APNG-Disguise-CPP.exe --delay-ms 1000 "封面.png" "播放图1.png" "动画.gif" "动画.apng" "结果.png"
```

命令行的 `--delay-ms` 设置静态图片时长，GIF／APNG 保留原始帧时长。

### 构建 Web 版

```powershell
cd web
npm ci
npm run build
python -m http.server 8080 --directory dist
```

浏览器打开 `http://localhost:8080`。`web/dist` 是完整静态站点，当前 GitHub Pages 使用 `gh-pages` 分支根目录中的构建产物。

## 功能

- 拖入封面图、隐藏图，也可通过文件选择器导入。
- 保留隐藏图原始分辨率；封面等比例缩放并居中留白。
- 支持中文与带空格路径，自动填写输出文件名。
- 本地处理，不联网、不上传图片；已有文件不会被覆盖。
- Windows 单文件 EXE，无需安装 Python；同时提供命令行入口。

## 使用方法

1. 打开 [Web 版](https://ryujou.github.io/qq-apng-disguise/)，或从 [Releases](https://github.com/ryujou/qq-apng-disguise/releases/latest) 下载 `QQ-APNG-Disguise-CPP.exe`。
2. 选择一张封面，再添加图片、GIF 或 APNG 作为播放内容，可批量选择或拖入。Python 版仅支持一张隐藏图。
3. 设置图片时长，例如 1000 表示 1 秒。动图默认使用原始帧时长，需要统一调整时取消保留选项。C++ 版选择保存位置后生成；Web 版点击「生成并下载 PNG」，也可再次点击下载链接保存。
4. 先将生成的 PNG 发送到手机，保留完整的原始文件。
5. 在手机 QQ 中选择该图片，**勾选「原图」后发送**，才有藏图效果。
6. 在聊天中检查封面预览，再打开原图确认隐藏图片或多图动画。

不要用截图或转存 JPG 替代生成的 PNG，这会丢失动画数据。拖入图片是从本地选择文件，不会上传至服务器。

## 运行源码

Windows 上使用 Python 3.11（已验证环境）：

```powershell
git clone https://github.com/ryujou/qq-apng-disguise.git
cd qq-apng-disguise
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python apng_disguise.py
```

命令行生成：

```powershell
python apng_disguise.py "封面.png" "隐藏图.png" "结果.png"
```

Python 版输入动画图片时只读取其默认画面。输出路径必须使用 `.png`，且文件尚不存在。

## 打包 EXE

在 Windows PowerShell 7 中运行：

```powershell
python -m pip install -r requirements-build.txt
./build.ps1
```

产物位于 `dist/QQ-APNG-Disguise.exe`，包含多尺寸图标和拖拽组件。

## 工作原理

输出是一个带独立静态封面的 APNG 文件。C++ 和 Web 版按每张图的时长拆帧：先显示完整画面，后续每次只重写左上角的同色 1×1 像素，保留其余画面。每帧最长 100 ms，最后一帧使用剩余时长。

例如，500 ms = 完整帧 100 ms + 四个同色小帧各 100 ms；250 ms = 100 + 100 + 50 ms。单图且时长不超过 100 ms 时，将总时长均分给完整帧和小帧，保持至少两帧。全部动画帧使用保留画布、替换局部像素的方式（dispose=0、blend=0）。封面不参与循环。

文件同时写入 `ChatBarApngDisguise` 制作标记；该标记不代表 QQ 的兼容性保证。

Python 版仍采用以下单图结构：

| 内容 | 用途 |
| --- | --- |
| 默认静态图（IDAT） | 显示封面，不参与动画 |
| 动画第 1 帧 | 完整隐藏图，持续 100 ms |
| 动画第 2 帧 | 透明 1×1 占位帧，持续 100 ms，叠加后保持隐藏图 |

动画无限循环。静态解码器和动画解码器读取不同内容，从而产生预览与原图不同的效果。它不加密图片，隐藏图可被支持 APNG 的工具读取。

## 许可

项目采用 [MIT License](LICENSE)。第三方运行时与依赖保留各自许可证，随发行版提供。项目为独立工具，与 QQ 官方无关联。
