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

    # Graphics counterpart: a real color pixel must equal the changing
    # storage-buffer payload when the template belongs to GRAPHICS.
    fragment = source.replace("layout(local_size_x=1) in;", "layout(location=0) out uint color;")
    fragment = fragment.replace(" buffer B", " readonly buffer B")
    fragment = fragment.replace(f"b{count-1}.data[pc.index] = ", f"color = b{count-1}.data[pc.index] + ")
    path = destination / f"buffers-{count}.frag"
    path.write_text(fragment)
    subprocess.run(["glslangValidator", "-V", str(path), "-o", str(path.with_suffix(".frag.spv"))], check=True)

vertex = destination / "triangle.vert"
vertex.write_text("""#version 450
void main() {
    vec2 p[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
    gl_Position = vec4(p[gl_VertexIndex],0,1);
}
""")
subprocess.run(["glslangValidator", "-V", str(vertex), "-o", str(vertex.with_suffix(".vert.spv"))], check=True)
