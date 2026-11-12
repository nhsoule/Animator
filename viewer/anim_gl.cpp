#include "anim.h"

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

//----- Viewer state -----||

struct Slider {
    char name[48];
    float *value;
    float minv;
    float maxv;
};

struct Draw {
    const Vert *verts;
    int count;
    Vec3 offset;
    float scale;
    Transform xf;
    int rev;
};

struct Gpu {
    const Vert *key;
    GLuint vbo;
    int count;
    int rev;
};

enum {
    GESTURE_NONE = 0,
    GESTURE_ORBIT = 1,
    GESTURE_PAN = 2,
    GESTURE_PLACE = 3
};

static Slider g_slider[ANIM_SLIDER_MAX];
static int g_slider_n;

static Draw g_draw[ANIM_DRAW_MAX];
static int g_draw_n;

static Gpu g_gpu[96];
static int g_gpu_n;

static Vec3 g_target = v3(0.f, 0.f, 0.4f);
static float g_yaw = 0.7f;
static float g_pitch = 0.55f;
static float g_dist = 4.5f;
static const float g_fovy = 0.90f;

static int g_place = 0;
static int g_playing = 1;
static float g_time_scale = 1.f;
static float g_time = 0.f;
static double g_last_ms = 0.0;

static Anim_pointer g_pointer;
static int g_gesture = GESTURE_NONE;
static int g_fingers = 0;
static float g_last_x = 0.f;
static float g_last_y = 0.f;
static float g_pinch = 0.f;

static int g_view_w = 1;
static int g_view_h = 1;
static int g_started = 0;

static GLuint g_prog = 0;
static GLuint g_line_prog = 0;
static GLuint g_vao = 0;
static GLuint g_grid_vbo = 0;
static GLuint g_axis_vbo = 0;
static int g_grid_verts = 0;

static GLint g_u_vp = -1;
static GLint g_u_model = -1;
static GLint g_u_offset = -1;
static GLint g_u_scale = -1;
static GLint g_u_line_vp = -1;
static GLint g_u_line_color = -1;

//----- Camera -----||

static void clamp_cam() {
    if (g_pitch < 0.08f) g_pitch = 0.08f;
    if (g_pitch > 1.45f) g_pitch = 1.45f;
    if (g_dist < 0.35f) g_dist = 0.35f;
    if (g_dist > 80.f) g_dist = 80.f;
}

static Vec3 camera_eye() {
    float cp = cosf(g_pitch);
    float sp = sinf(g_pitch);
    return v3(g_target.x + cosf(g_yaw) * cp * g_dist,
              g_target.y + sinf(g_yaw) * cp * g_dist,
              g_target.z + sp * g_dist);
}

static void camera_basis(Vec3 *right, Vec3 *up, Vec3 *fwd) {
    Vec3 eye = camera_eye();
    Vec3 f = v3_norm(v3_sub(g_target, eye));
    Vec3 s = v3_norm(v3_cross(f, v3(0.f, 0.f, 1.f)));
    Vec3 u = v3_cross(s, f);
    if (right) *right = s;
    if (up) *up = u;
    if (fwd) *fwd = f;
}

static void orbit(float dx, float dy) {
    g_yaw -= dx * 0.007f;
    g_pitch -= dy * 0.007f;
    clamp_cam();
}

static void pan(float dx, float dy) {
    Vec3 s, u;
    camera_basis(&s, &u, 0);
    float k = g_dist * 0.0016f;
    g_target = v3_sub(g_target, v3_scale(s, dx * k));
    g_target = v3_sub(g_target, v3_scale(u, dy * k));
}

static void mat_mul(const float *a, const float *b, float *o) {
    float r[16];
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] + a[1 * 4 + row] * b[c * 4 + 1] +
                            a[2 * 4 + row] * b[c * 4 + 2] + a[3 * 4 + row] * b[c * 4 + 3];
        }
    }
    memcpy(o, r, sizeof(r));
}

static void mat_look(float *m, Vec3 eye, Vec3 at) {
    Vec3 f = v3_norm(v3_sub(at, eye));
    Vec3 s = v3_norm(v3_cross(f, v3(0.f, 0.f, 1.f)));
    Vec3 u = v3_cross(s, f);
    memset(m, 0, sizeof(float) * 16);
    m[0] = s.x;
    m[4] = s.y;
    m[8] = s.z;
    m[12] = -v3_dot(s, eye);
    m[1] = u.x;
    m[5] = u.y;
    m[9] = u.z;
    m[13] = -v3_dot(u, eye);
    m[2] = -f.x;
    m[6] = -f.y;
    m[10] = -f.z;
    m[14] = v3_dot(f, eye);
    m[15] = 1.f;
}

static void mat_persp(float *m, float fovy, float aspect, float n, float f) {
    memset(m, 0, sizeof(float) * 16);
    float t = 1.f / tanf(fovy * 0.5f);
    m[0] = t / aspect;
    m[5] = t;
    m[10] = (f + n) / (n - f);
    m[11] = -1.f;
    m[14] = (2.f * f * n) / (n - f);
}

// Ground plane is z = 0. px/py are css pixels, origin top-left of the canvas.
static int pick_ground(float px, float py, Vec3 *hit) {
    double cw = 0, ch = 0;
    emscripten_get_element_css_size("#canvas", &cw, &ch);
    if (cw < 1.0 || ch < 1.0) return 0;
    float ndc_x = (float)((px / cw) * 2.0 - 1.0);
    float ndc_y = (float)(1.0 - (py / ch) * 2.0);
    float aspect = (float)(cw / ch);
    float tan_h = tanf(g_fovy * 0.5f);
    float cam_x = ndc_x * aspect * tan_h;
    float cam_y = ndc_y * tan_h;
    Vec3 eye = camera_eye();
    Vec3 s, u, f;
    camera_basis(&s, &u, &f);
    Vec3 dir = v3_add(v3_add(v3_scale(s, cam_x), v3_scale(u, cam_y)), f);
    if (fabsf(dir.z) < 1e-6f) return 0;
    float t = -eye.z / dir.z;
    if (t < 0.f) return 0;
    *hit = v3_add(eye, v3_scale(dir, t));
    return 1;
}

static void update_hit() {
    g_pointer.x = g_last_x;
    g_pointer.y = g_last_y;
    g_pointer.hit = pick_ground(g_last_x, g_last_y, &g_pointer.world);
}

//----- Pointer -----||

static void begin_place() {
    g_gesture = GESTURE_PLACE;
    g_pointer.place = 1;
    g_pointer.down = 1;
    g_pointer.pressed = 1;
    update_hit();
}

static void end_place() {
    if (g_pointer.down || g_gesture == GESTURE_PLACE) {
        g_pointer.down = 0;
        g_pointer.released = 1;
    }
    g_pointer.place = g_place ? 1 : 0;
    if (g_gesture == GESTURE_PLACE) g_gesture = GESTURE_NONE;
}

EM_JS(void, canvas_local, (double client_x, double client_y, float *out_x, float *out_y), {
    var c = document.getElementById("canvas");
    if (!c) {
        setValue(out_x, client_x, "float");
        setValue(out_y, client_y, "float");
        return;
    }
    var r = c.getBoundingClientRect();
    setValue(out_x, client_x - r.left, "float");
    setValue(out_y, client_y - r.top, "float");
});

static void mouse_canvas(const EmscriptenMouseEvent *e, float *x, float *y) {
    canvas_local(e->clientX, e->clientY, x, y);
}

static EM_BOOL on_mouse_down(int, const EmscriptenMouseEvent *e, void *) {
    mouse_canvas(e, &g_last_x, &g_last_y);
    update_hit();
    int pan_btn = (e->button == 1 || e->button == 2 || (e->button == 0 && e->shiftKey));
    if (pan_btn) g_gesture = GESTURE_PAN;
    else if (e->button == 0 && g_place) begin_place();
    else if (e->button == 0) g_gesture = GESTURE_ORBIT;
    return EM_TRUE;
}

static EM_BOOL on_mouse_move(int, const EmscriptenMouseEvent *e, void *) {
    float x = 0.f, y = 0.f;
    mouse_canvas(e, &x, &y);
    float dx = x - g_last_x;
    float dy = y - g_last_y;
    g_last_x = x;
    g_last_y = y;
    if (g_gesture == GESTURE_ORBIT) orbit(dx, dy);
    else if (g_gesture == GESTURE_PAN) pan(dx, dy);
    update_hit();
    return EM_FALSE;
}

static EM_BOOL on_mouse_up(int, const EmscriptenMouseEvent *, void *) {
    if (g_gesture == GESTURE_PLACE) end_place();
    g_gesture = GESTURE_NONE;
    return EM_FALSE;
}

static EM_BOOL on_wheel(int, const EmscriptenWheelEvent *e, void *) {
    g_dist *= expf((float)e->deltaY * 0.0011f);
    clamp_cam();
    return EM_TRUE;
}

static int touch_points(const EmscriptenTouchEvent *e, float *xs, float *ys) {
    int n = 0;
    int total = e->numTouches;
    if (total > 8) total = 8;
    for (int i = 0; i < total; i++) {
        xs[n] = (float)e->touches[i].targetX;
        ys[n] = (float)e->touches[i].targetY;
        n++;
    }
    return n;
}

static float dist2(float ax, float ay, float bx, float by) {
    float dx = ax - bx;
    float dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

static EM_BOOL on_touch(int type, const EmscriptenTouchEvent *e, void *) {
    float xs[8], ys[8];
    int n = touch_points(e, xs, ys);
    if (type == EMSCRIPTEN_EVENT_TOUCHCANCEL) n = 0;

    if (n == 1) {
        float x = xs[0], y = ys[0];
        if (g_fingers == 0) {
            g_last_x = x;
            g_last_y = y;
            if (g_place) begin_place();
            else g_gesture = GESTURE_ORBIT;
        } else if (g_fingers == 1) {
            float dx = x - g_last_x;
            float dy = y - g_last_y;
            g_last_x = x;
            g_last_y = y;
            if (g_gesture == GESTURE_ORBIT) orbit(dx, dy);
        } else {
            // Dropped from a multi-touch. Don't start a new placement click.
            g_last_x = x;
            g_last_y = y;
            if (g_pointer.down) end_place();
            g_gesture = GESTURE_ORBIT;
        }
        update_hit();
    } else if (n >= 2) {
        if (g_pointer.down) end_place();
        float cx = 0.f, cy = 0.f;
        for (int i = 0; i < n; i++) {
            cx += xs[i];
            cy += ys[i];
        }
        cx /= (float)n;
        cy /= (float)n;
        float pinch = dist2(xs[0], ys[0], xs[1], ys[1]);
        if (g_fingers >= 2) {
            float dx = cx - g_last_x;
            float dy = cy - g_last_y;
            float dp = pinch - g_pinch;
            if (fabsf(dp) > 1.2f) {
                g_dist *= expf(-dp * 0.008f);
                clamp_cam();
            }
            if (n >= 3 || !g_place) pan(dx, dy);
            else orbit(dx, dy);
        }
        g_last_x = cx;
        g_last_y = cy;
        g_pinch = pinch;
        g_gesture = GESTURE_NONE;
    } else if (g_pointer.down) {
        end_place();
        g_gesture = GESTURE_NONE;
    } else {
        g_gesture = GESTURE_NONE;
    }
    g_fingers = n;
    return EM_TRUE;
}

static void init_input() {
    emscripten_set_mousedown_callback("#canvas", 0, 1, on_mouse_down);
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, 0, 1, on_mouse_move);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, 0, 1, on_mouse_up);
    emscripten_set_wheel_callback("#canvas", 0, 1, on_wheel);
    emscripten_set_touchstart_callback("#canvas", 0, 1, on_touch);
    emscripten_set_touchmove_callback("#canvas", 0, 1, on_touch);
    emscripten_set_touchend_callback("#canvas", 0, 1, on_touch);
    emscripten_set_touchcancel_callback("#canvas", 0, 1, on_touch);
}

//----- GL -----||

static const char *MESH_VS = R"glsl(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aNrm;
layout(location = 2) in vec4 aCol;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec3 uOffset;
uniform float uScale;
out vec3 vN;
out vec3 vC;
vec3 octDecode(vec2 e) {
    vec3 n = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        vec2 s = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (vec2(1.0) - abs(n.yx)) * s;
    }
    return normalize(n);
}
void main() {
    vec3 local = uOffset + aPos * uScale;
    vec4 world = uModel * vec4(local, 1.0);
    gl_Position = uViewProj * world;
    vN = mat3(transpose(inverse(uModel))) * octDecode(aNrm);
    vC = aCol.rgb;
}
)glsl";

static const char *MESH_FS = R"glsl(#version 300 es
precision mediump float;
in vec3 vN;
in vec3 vC;
out vec4 frag;
void main() {
    vec3 n = normalize(vN);
    vec3 sun = normalize(vec3(0.35, 0.45, 0.82));
    float d = max(dot(n, sun), 0.0);
    vec3 c = vC * (vec3(0.46, 0.46, 0.50) + vec3(0.54, 0.52, 0.46) * d);
    frag = vec4(c, 1.0);
}
)glsl";

static const char *LINE_VS = R"glsl(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)glsl";

static const char *LINE_FS = R"glsl(#version 300 es
precision mediump float;
uniform vec3 uColor;
out vec4 frag;
void main() { frag = vec4(uColor, 1.0); }
)glsl";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        glGetShaderInfoLog(s, (GLsizei)sizeof(log), &n, log);
        printf("shader: %s\n", log);
    }
    return s;
}

static GLuint link_program(const char *vs, const char *fs) {
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        glGetProgramInfoLog(p, (GLsizei)sizeof(log), &n, log);
        printf("link: %s\n", log);
        return 0;
    }
    return p;
}

static void push_line(float *buf, int *n, int cap, float x0, float y0, float z0, float x1, float y1, float z1) {
    if (*n + 6 > cap) return;
    float *d = buf + *n;
    d[0] = x0;
    d[1] = y0;
    d[2] = z0;
    d[3] = x1;
    d[4] = y1;
    d[5] = z1;
    *n += 6;
}

static void init_gl() {
    g_prog = link_program(MESH_VS, MESH_FS);
    g_line_prog = link_program(LINE_VS, LINE_FS);
    if (g_prog) {
        g_u_vp = glGetUniformLocation(g_prog, "uViewProj");
        g_u_model = glGetUniformLocation(g_prog, "uModel");
        g_u_offset = glGetUniformLocation(g_prog, "uOffset");
        g_u_scale = glGetUniformLocation(g_prog, "uScale");
    }
    if (g_line_prog) {
        g_u_line_vp = glGetUniformLocation(g_line_prog, "uViewProj");
        g_u_line_color = glGetUniformLocation(g_line_prog, "uColor");
    }

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);

    float grid[1024];
    int floats = 0;
    const int N = 10;
    for (int i = -N; i <= N; i++) {
        float a = (float)i;
        float e = (float)N;
        push_line(grid, &floats, 1024, -e, a, 0.f, e, a, 0.f);
        push_line(grid, &floats, 1024, a, -e, 0.f, a, e, 0.f);
    }
    g_grid_verts = floats / 3;
    glGenBuffers(1, &g_grid_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_grid_vbo);
    glBufferData(GL_ARRAY_BUFFER, floats * (int)sizeof(float), grid, GL_STATIC_DRAW);

    float axes[18] = {
        0.f, 0.f, 0.002f, 1.f, 0.f, 0.002f,
        0.f, 0.f, 0.002f, 0.f, 1.f, 0.002f,
        0.f, 0.f, 0.002f, 0.f, 0.f, 1.f
    };
    glGenBuffers(1, &g_axis_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_axis_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axes), axes, GL_STATIC_DRAW);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.62f, 0.70f, 0.76f, 1.f);
}

static void resize() {
    double cw = 0, ch = 0;
    emscripten_get_element_css_size("#canvas", &cw, &ch);
    double dpr = emscripten_get_device_pixel_ratio();
    if (dpr < 1.0) dpr = 1.0;
    if (dpr > 2.0) dpr = 2.0;
    int w = (int)(cw * dpr);
    int h = (int)(ch * dpr);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == g_view_w && h == g_view_h) return;
    g_view_w = w;
    g_view_h = h;
    emscripten_set_canvas_element_size("#canvas", w, h);
    glViewport(0, 0, w, h);
}

static GLuint mesh_vbo(const Draw &d) {
    for (int i = 0; i < g_gpu_n; i++) {
        if (g_gpu[i].key != d.verts) continue;
        if (g_gpu[i].rev != d.rev || g_gpu[i].count != d.count) {
            glBindBuffer(GL_ARRAY_BUFFER, g_gpu[i].vbo);
            glBufferData(GL_ARRAY_BUFFER, d.count * (int)sizeof(Vert), d.verts, GL_DYNAMIC_DRAW);
            g_gpu[i].rev = d.rev;
            g_gpu[i].count = d.count;
        }
        return g_gpu[i].vbo;
    }
    if (g_gpu_n >= 96) return 0;
    Gpu &g = g_gpu[g_gpu_n++];
    glGenBuffers(1, &g.vbo);
    g.key = d.verts;
    g.rev = d.rev;
    g.count = d.count;
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, d.count * (int)sizeof(Vert), d.verts, GL_DYNAMIC_DRAW);
    return g.vbo;
}

static void draw_lines(GLuint vbo, int first, int count, float r, float g, float b, const float *vp) {
    if (!g_line_prog || count <= 0) return;
    glUseProgram(g_line_prog);
    glUniformMatrix4fv(g_u_line_vp, 1, GL_FALSE, vp);
    glUniform3f(g_u_line_color, r, g, b);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void *)(size_t)(first * 12));
    glDrawArrays(GL_LINES, 0, count);
}

static void render() {
    resize();
    glViewport(0, 0, g_view_w, g_view_h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = g_view_h > 0 ? (float)g_view_w / (float)g_view_h : 1.f;
    float view[16], proj[16], vp[16];
    mat_look(view, camera_eye(), g_target);
    mat_persp(proj, g_fovy, aspect, 0.05f, 200.f);
    mat_mul(proj, view, vp);

    glBindVertexArray(g_vao);
    draw_lines(g_grid_vbo, 0, g_grid_verts, 0.42f, 0.46f, 0.50f, vp);
    draw_lines(g_axis_vbo, 0, 2, 0.75f, 0.28f, 0.25f, vp);
    draw_lines(g_axis_vbo, 2, 2, 0.28f, 0.62f, 0.32f, vp);
    draw_lines(g_axis_vbo, 4, 2, 0.25f, 0.40f, 0.75f, vp);

    if (!g_prog) return;
    glUseProgram(g_prog);
    glUniformMatrix4fv(g_u_vp, 1, GL_FALSE, vp);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    for (int i = 0; i < g_draw_n; i++) {
        Draw &d = g_draw[i];
        GLuint vbo = mesh_vbo(d);
        if (!vbo) continue;
        float model[16];
        xf_to_mat(d.xf, model);
        glUniformMatrix4fv(g_u_model, 1, GL_FALSE, model);
        glUniform3f(g_u_offset, d.offset.x, d.offset.y, d.offset.z);
        glUniform1f(g_u_scale, d.scale);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 3, GL_SHORT, GL_TRUE, 12, (void *)0);
        glVertexAttribPointer(1, 2, GL_BYTE, GL_TRUE, 12, (void *)6);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 12, (void *)8);
        glDrawArrays(GL_TRIANGLES, 0, d.count);
    }
}

//----- Frame -----||

static void frame() {
    double now = emscripten_get_now();
    float real_dt = 0.016f;
    if (g_last_ms > 0.0) real_dt = (float)((now - g_last_ms) * 0.001);
    g_last_ms = now;
    if (real_dt < 0.f) real_dt = 0.f;
    if (real_dt > 0.05f) real_dt = 0.05f;

    float dt = g_playing ? real_dt * g_time_scale : 0.f;
    if (dt < 0.f) dt = 0.f;
    g_time += dt;

    Anim_frame in;
    in.dt = dt;
    in.time = g_time;
    in.pointer = g_pointer;
    g_draw_n = 0;
    scene_tick(in);
    render();
    g_pointer.pressed = 0;
    g_pointer.released = 0;
}

//----- Scene hooks -----||

void anim_slider(const char *name, float *value, float minv, float maxv) {
    if (!value || g_slider_n >= ANIM_SLIDER_MAX) return;
    Slider &s = g_slider[g_slider_n++];
    if (!name) name = "";
    int i = 0;
    for (; i < 47 && name[i]; i++) s.name[i] = name[i];
    s.name[i] = 0;
    s.value = value;
    s.minv = minv;
    s.maxv = maxv;
    if (*value < minv) *value = minv;
    if (*value > maxv) *value = maxv;
}

void anim_draw(const Anim_mesh &mesh, const Transform &xf) {
    if (!mesh.verts || mesh.count <= 0) return;
    if (g_draw_n >= ANIM_DRAW_MAX) return;
    Draw &d = g_draw[g_draw_n++];
    d.verts = mesh.verts;
    d.count = mesh.count;
    d.offset = mesh.offset;
    d.scale = mesh.scale > 0.f ? mesh.scale : 1.f;
    d.xf = xf;
    d.rev = mesh.rev;
}

//----- JS exports -----||

extern "C" {

EMSCRIPTEN_KEEPALIVE
void anim_start() {
    if (g_started) return;
    g_started = 1;
    init_input();
    init_gl();
    scene_init();
    g_last_ms = emscripten_get_now();
    emscripten_set_main_loop(frame, 0, 0);
}

EMSCRIPTEN_KEEPALIVE int anim_slider_count() { return g_slider_n; }

EMSCRIPTEN_KEEPALIVE const char *anim_slider_name(int i) {
    if (i < 0 || i >= g_slider_n) return "";
    return g_slider[i].name;
}

EMSCRIPTEN_KEEPALIVE float anim_slider_value(int i) {
    if (i < 0 || i >= g_slider_n || !g_slider[i].value) return 0.f;
    return *g_slider[i].value;
}

EMSCRIPTEN_KEEPALIVE float anim_slider_min(int i) {
    if (i < 0 || i >= g_slider_n) return 0.f;
    return g_slider[i].minv;
}

EMSCRIPTEN_KEEPALIVE float anim_slider_max(int i) {
    if (i < 0 || i >= g_slider_n) return 1.f;
    return g_slider[i].maxv;
}

EMSCRIPTEN_KEEPALIVE
void anim_slider_set(int i, float v) {
    if (i < 0 || i >= g_slider_n || !g_slider[i].value) return;
    if (v < g_slider[i].minv) v = g_slider[i].minv;
    if (v > g_slider[i].maxv) v = g_slider[i].maxv;
    *g_slider[i].value = v;
}

EMSCRIPTEN_KEEPALIVE void anim_set_playing(int on) { g_playing = on ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int anim_get_playing() { return g_playing; }

EMSCRIPTEN_KEEPALIVE
void anim_set_time_scale(float s) {
    if (s < 0.f) s = 0.f;
    if (s > 8.f) s = 8.f;
    g_time_scale = s;
}

EMSCRIPTEN_KEEPALIVE float anim_get_time_scale() { return g_time_scale; }

EMSCRIPTEN_KEEPALIVE
void anim_set_place(int on) {
    g_place = on ? 1 : 0;
    g_pointer.place = g_place;
    if (!g_place && g_gesture == GESTURE_PLACE) end_place();
}

EMSCRIPTEN_KEEPALIVE int anim_get_place() { return g_place; }

}
