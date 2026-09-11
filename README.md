<p align="center">
  <img src="assets/icon.png" width="120" alt="QQ APNG 藏图工具图标">
</p>

<h1 align="center">QQ APNG 藏图工具</h1>

<p align="center">一张封面，一张隐藏图。拖入图片，生成适用于 QQ 聊天环境的 APNG。</p>

<p align="center">
  <a href="https://github.com/ryujou/qq-apng-disguise/releases/latest"><img src="https://img.shields.io/github/v/release/ryujou/qq-apng-disguise?label=download" alt="下载"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20x64-0078D4" alt="Windows x64">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-green" alt="MIT"></a>
</p>

<p align="center"><a href="https://github.com/ryujou/qq-apng-disguise/releases/latest">下载 Windows EXE</a> · <a href="#使用方法">使用方法</a> · <a href="#运行源码">运行源码</a></p>

![工具界面](assets/screenshot.png)

## 效果与适用环境

在 QQ 聊天中，缩略预览显示封面，查看原图时显示隐藏图片。

**作者已在 QQ 聊天环境实测通过。** 具体 QQ 客户端版本与操作系统组合尚未记录；其他版本及其他平台的表现取决于图片解码和转码方式。

## 功能

- 拖入封面图、隐藏图，也可通过文件选择器导入。
- 保留隐藏图原始分辨率；封面等比例缩放并居中留白。
- 支持中文与带空格路径，自动填写输出文件名。
- 本地处理，不联网、不上传图片；已有文件不会被覆盖。
- Windows 单文件 EXE，无需安装 Python；同时提供命令行入口。

## 使用方法

1. 从 [Releases](https://github.com/ryujou/qq-apng-disguise/releases/latest) 下载 `QQ-APNG-Disguise.exe`，双击运行。
2. 将图片分别拖到「封面图」和「隐藏图」输入框，每次拖入一个文件。
3. 选择保存位置，点击「生成藏图」。
4. 在 QQ 中发送生成的 PNG，并查看原图确认效果；发送时尽量保留原文件。

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

输入动画图片时读取其默认画面。输出路径必须使用 `.png`，且文件尚不存在。

## 打包 EXE

在 Windows PowerShell 7 中运行：

```powershell
python -m pip install -r requirements-build.txt
./build.ps1
```

产物位于 `dist/QQ-APNG-Disguise.exe`，包含多尺寸图标和拖拽组件。

## 工作原理

输出是一个带独立静态封面的 APNG 文件：

| 内容 | 用途 |
| --- | --- |
| 默认静态图（IDAT） | 显示封面，不参与动画 |
| 动画第 1 帧 | 完整隐藏图，持续 100 ms |
| 动画第 2 帧 | 透明 1×1 占位帧，持续 100 ms，叠加后保持隐藏图 |

动画无限循环。静态解码器和动画解码器读取不同内容，从而产生预览与原图不同的效果。它不加密图片，隐藏图可被支持 APNG 的工具读取。

## 许可

项目采用 [MIT License](LICENSE)。第三方运行时与依赖保留各自许可证，随发行版提供。项目为独立工具，与 QQ 官方无关联。
