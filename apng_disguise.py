"""生成静态预览与动画画面不同的 PNG。依赖：pip install Pillow tkinterdnd2"""

import argparse
import io
import struct
import sys
import zlib
from pathlib import Path

from PIL import Image, ImageOps


def png_image_data(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    data = buffer.getvalue()
    offset = 8
    compressed = []
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        if data[offset + 4:offset + 8] == b"IDAT":
            compressed.append(data[offset + 8:offset + 8 + length])
        offset += length + 12
    return b"".join(compressed)


def write_chunk(output, kind, data):
    # CRC 是 PNG 数据块格式的必需字段。
    output.write(struct.pack(">I", len(data)) + kind + data)
    output.write(struct.pack(">I", zlib.crc32(kind + data)))


def make_disguise(cover_path, hidden_path, output_path):
    with Image.open(cover_path) as source:
        cover = ImageOps.exif_transpose(source).convert("RGBA")
    with Image.open(hidden_path) as source:
        hidden = ImageOps.exif_transpose(source).convert("RGBA")

    # APNG 共用画布，以隐藏图原始尺寸为准，避免小封面压低隐藏图分辨率。
    if hidden.size != cover.size:
        cover = ImageOps.pad(
            cover, hidden.size, method=Image.Resampling.LANCZOS,
            color=(255, 255, 255, 255),
        )

    # 独立封面 + 完整隐藏帧 + 透明占位帧，避免单帧动画被视为静态图。
    with Path(output_path).open("xb") as output:
        output.write(b"\x89PNG\r\n\x1a\n")
        write_chunk(output, b"IHDR", struct.pack(">IIBBBBB", *hidden.size, 8, 6, 0, 0, 0))
        write_chunk(output, b"acTL", struct.pack(">II", 2, 0))
        write_chunk(output, b"IDAT", png_image_data(cover))
        write_chunk(output, b"fcTL", struct.pack(">IIIIIHHBB", 0, *hidden.size, 0, 0, 10, 100, 0, 0))
        data = png_image_data(hidden)
        sequence = 1
        for offset in range(0, len(data), 65536):
            write_chunk(output, b"fdAT", struct.pack(">I", sequence) + data[offset:offset + 65536])
            sequence += 1
        write_chunk(output, b"fcTL", struct.pack(">IIIIIHHBB", sequence, 1, 1, 0, 0, 10, 100, 0, 1))
        write_chunk(output, b"fdAT", struct.pack(">I", sequence + 1) + png_image_data(Image.new("RGBA", (1, 1))))
        write_chunk(output, b"IEND", b"")


def create_window():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from tkinterdnd2 import TkinterDnD, DND_FILES

    root = TkinterDnD.Tk()
    root.iconbitmap(str(Path(__file__).resolve().parent / "assets" / "icon.ico"))
    root.title("APNG 藏图工具")
    root.geometry("760x440")
    root.minsize(760, 440)
    panel = ttk.Frame(root, padding=24)
    panel.pack(fill="both", expand=True)
    panel.columnconfigure(1, weight=1)
    ttk.Label(panel, text="APNG 藏图工具", font=("Microsoft YaHei UI", 18, "bold")).grid(
        row=0, column=0, columnspan=3, sticky="w", pady=(0, 8))
    ttk.Label(panel, text="将图片拖入对应输入框，或点击浏览选择图片。").grid(
        row=1, column=0, columnspan=3, sticky="w", pady=(0, 16))
    paths = [tk.StringVar(root) for _ in range(3)]

    def set_path(index, path):
        paths[index].set(str(Path(path)))
        if index == 0 and not paths[2].get():
            cover = Path(path)
            paths[2].set(str(cover.with_name(cover.stem + "_藏图.png")))

    def drop(event, index):
        files = root.tk.splitlist(event.data)
        if len(files) != 1 or not Path(files[0]).is_file():
            messagebox.showerror("拖入图片", "请一次拖入一个图片文件。", parent=root)
            return "refuse_drop"
        set_path(index, files[0])
        return "copy"

    def browse(index):
        if index == 2:
            path = filedialog.asksaveasfilename(
                parent=root, title="保存生成的图片", defaultextension=".png",
                filetypes=[("PNG 图片", "*.png")], initialfile="藏图.png")
        else:
            path = filedialog.askopenfilename(
                parent=root, title="选择封面图" if index == 0 else "选择隐藏图",
                filetypes=[("图片", "*.png *.jpg *.jpeg *.webp *.bmp *.gif"), ("所有文件", "*.*")])
        if path:
            set_path(index, path)

    for index, label in enumerate(("封面图", "隐藏图", "保存到")):
        ttk.Label(panel, text=label).grid(row=index + 2, column=0, sticky="w", padx=(0, 12), pady=6)
        entry = ttk.Entry(panel, textvariable=paths[index])
        entry.grid(row=index + 2, column=1, sticky="ew", pady=6)
        if index < 2:
            entry.drop_target_register(DND_FILES)
            entry.dnd_bind("<<Drop>>", lambda event, i=index: drop(event, i))
        ttk.Button(panel, text="浏览…", command=lambda i=index: browse(i)).grid(
            row=index + 2, column=2, padx=(10, 0), pady=6)

    status = tk.StringVar(root, value="保留隐藏图原始分辨率；封面等比例适配，居中留白。")

    def generate():
        values = [value.get().strip() for value in paths]
        if not all(values):
            messagebox.showerror("信息不完整", "请选择封面图、隐藏图和保存位置。", parent=root)
            return
        values = [str(Path(value)) for value in values]
        for variable, value in zip(paths, values):
            variable.set(value)
        if Path(values[2]).suffix.lower() != ".png":
            messagebox.showerror("保存格式", "请使用 .png 扩展名。", parent=root)
            return
        status.set("正在生成，请稍候…")
        root.update_idletasks()
        try:
            make_disguise(*values)
        except FileExistsError:
            status.set("文件已存在，请更换保存文件名。")
            messagebox.showerror("文件已存在", "为避免覆盖原图，请更换保存文件名。", parent=root)
            return
        except (OSError, ValueError, Image.DecompressionBombError) as error:
            status.set("生成失败，请检查图片和保存位置。")
            messagebox.showerror("生成失败", str(error), parent=root)
            return
        status.set("生成完成。")
        messagebox.showinfo("生成完成", f"已保存到：\n{values[2]}", parent=root)

    ttk.Button(panel, text="生成藏图", command=generate).grid(row=5, column=0, columnspan=3, pady=(16, 10))
    ttk.Label(panel, textvariable=status).grid(row=6, column=0, columnspan=3, sticky="w")
    ttk.Label(panel, text="适用于 QQ 聊天环境；压缩或转码可能使隐藏图丢失。", foreground="#666666").grid(
        row=7, column=0, columnspan=3, sticky="w", pady=(8, 0))
    return root


def main():
    if len(sys.argv) == 1:
        create_window().mainloop()
        return
    parser = argparse.ArgumentParser(
        description="生成 APNG 藏图：静态预览显示封面，APNG 查看器显示隐藏图。",
        epilog="效果取决于查看器；平台压缩或转码可能使隐藏图丢失。输出文件不能已存在。",
    )
    parser.add_argument("cover", type=Path, help="封面图片路径")
    parser.add_argument("hidden", type=Path, help="隐藏图片路径；保留原始分辨率，封面等比例适配")
    parser.add_argument("output", type=Path, help="输出 .png 路径")
    args = parser.parse_args()
    if args.output.suffix.lower() != ".png":
        parser.error("输出文件扩展名必须是 .png")
    if args.output.exists():
        parser.error("输出文件已存在，请换一个文件名")
    make_disguise(args.cover, args.hidden, args.output)
    print(f"已生成：{args.output.resolve()}")


if __name__ == "__main__":
    main()


