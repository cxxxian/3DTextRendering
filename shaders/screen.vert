#version 330 core
// screen.vert —— 全屏三角（无 VBO，靠 gl_VertexID）

out vec2 vUV;

void main() {
    // 覆盖 NDC 的大三角：(-1,-1), (3,-1), (-1,3)
    float x = (gl_VertexID == 1) ? 3.0 : -1.0;
    float y = (gl_VertexID == 2) ? 3.0 : -1.0;
    vUV = vec2((x + 1.0) * 0.5, (y + 1.0) * 0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
