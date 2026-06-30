from PIL import Image
from pathlib import Path
import sys

# 作用：把一个文件夹里的 PNG 批量转换成纯黑白 1-bit 风格
# 用法：python3 convert.py <文件夹>  (默认当前目录)

input_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
output_dir = input_dir / "output_1bit"
output_dir.mkdir(exist_ok=True)

# 阈值：小于 128 的像素变黑，大于等于 128 的像素变白
threshold = 128

png_files = [p for p in input_dir.glob("*.png") if p.parent != output_dir]
if not png_files:
    print(f"No PNG files found in {input_dir.resolve()}")
    sys.exit(1)

for path in png_files:
    img = Image.open(path).convert("RGBA")

    pixels = img.load()

    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = pixels[x, y]

            # 如果是透明像素，保持透明
            if a == 0:
                continue

            # 计算亮度
            brightness = (r + g + b) // 3

            # 根据阈值转成纯黑或纯白
            if brightness < threshold:
                pixels[x, y] = (0, 0, 0, 255)
            else:
                pixels[x, y] = (255, 255, 255, 255)

    img.save(output_dir / path.name)
    print(f"converted {path.name}")

print("Done")