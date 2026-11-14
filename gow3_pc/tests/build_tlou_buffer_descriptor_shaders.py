"""Generate small compute programs for exact Vulkan descriptor payload checks."""
from pathlib import Path
import subprocess
import sys

destination = Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
for count in (2, 4, 8, 16):
    source = "#version 450\nlayout(local_size_x=1) in;\n"
    source += "layout(push_constant) uniform PC { uint index; } pc;\n"
    for binding in range(count):
        source += (f"layout(set=0,binding={binding},std430) buffer B{binding} "
                   f"{{ uint data[]; }} b{binding};\n")
    source += f"void main() {{ b{count-1}.data[pc.index] = "
    source += " + ".join(f"b{i}.data[pc.index]" for i in range(count-1)) + "; }\n"
    path = destination / f"buffers-{count}.comp"
    path.write_text(source)
    subprocess.run(["glslangValidator", "-V", str(path), "-o", str(path.with_suffix(".spv"))], check=True)
