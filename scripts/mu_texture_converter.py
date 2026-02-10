import os
import sys
from PIL import Image
import io

def convert_mu_texture(input_path, output_path):
    print(f"[*] Converting {input_path} -> {output_path}")
    
    with open(input_path, "rb") as f:
        ext = os.path.splitext(input_path)[1].lower()
        if ext in ['.ozt', '.ozj', '.ozb']:
            # MU-specific extensions have a 4-byte header to skip
            f.seek(4)
        data = f.read()
    
    ext = os.path.splitext(input_path)[1].lower()
    
    try:
        # We can try to open the remaining data with PIL
        img = Image.open(io.BytesIO(data))
        img.save(output_path, "PNG")
        return True
    except Exception as e:
        print(f"[!] PIL failed to open {input_path}: {e}")
        # Sometimes TGA needs manual handling if PIL's TGA support is picky
        # but usually it works.
        return False

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 mu_texture_converter.py <file1.ozt> [file2.ozj] ...")
        sys.exit(1)
        
    for path in sys.argv[1:]:
        if not os.path.exists(path):
            print(f"[!] Path not found: {path}")
            continue
            
        base = os.path.splitext(path)[0]
        out_path = base + ".png"
        convert_mu_texture(path, out_path)
