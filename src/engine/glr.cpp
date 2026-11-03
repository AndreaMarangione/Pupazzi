// GPU renderer, OpenGL ES 2.
//
// Same content as the software renderer: the fixed geometry is uploaded once into a static
// buffer, the moving geometry is transformed and lit on the CPU and sent to the GPU in a
// single buffer per frame (a single draw call: on these GPUs the calls matter much more
// than the triangles).
//
// The GL context is the frontend's, so at the end of every frame the state is put back
// the way RetroArch expects it.
#include "engine/glr.h"
#include "platform/plat.h"
#include <dlfcn.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

#define GL_TRIANGLES 0x0004
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_STENCIL_BUFFER_BIT 0x0400
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_LESS 0x0201
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_BACK 0x0405
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_SAMPLE_COVERAGE 0x80A0
#define GL_FUNC_ADD 0x8006
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#define GL_DEPTH_BITS 0x0D56
#define GL_STENCIL_BITS 0x0D57
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_ALPHA 0x1906
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_MAX_SAMPLES 0x8D57

#define GLF(X) \
  X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
  X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(void, ClearDepthf, (GLfloat)) \
  X(void, ClearStencil, (GLint)) \
  X(void, Clear, (GLbitfield)) \
  X(void, Enable, (GLenum)) \
  X(void, Disable, (GLenum)) \
  X(void, DepthFunc, (GLenum)) \
  X(void, DepthMask, (GLboolean)) \
  X(void, DepthRangef, (GLfloat, GLfloat)) \
  X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
  X(void, StencilMask, (GLuint)) \
  X(void, CullFace, (GLenum)) \
  X(void, FrontFace, (GLenum)) \
  X(void, BlendFunc, (GLenum, GLenum)) \
  X(void, BlendEquation, (GLenum)) \
  X(GLuint, CreateShader, (GLenum)) \
  X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
  X(void, CompileShader, (GLuint)) \
  X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
  X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
  X(void, DeleteShader, (GLuint)) \
  X(GLuint, CreateProgram, (void)) \
  X(void, AttachShader, (GLuint, GLuint)) \
  X(void, BindAttribLocation, (GLuint, GLuint, const GLchar*)) \
  X(void, LinkProgram, (GLuint)) \
  X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
  X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
  X(void, UseProgram, (GLuint)) \
  X(GLint, GetUniformLocation, (GLuint, const GLchar*)) \
  X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
  X(void, Uniform1i, (GLint, GLint)) \
  X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(void, GenBuffers, (GLsizei, GLuint*)) \
  X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
  X(void, BindBuffer, (GLenum, GLuint)) \
  X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
  X(void, EnableVertexAttribArray, (GLuint)) \
  X(void, DisableVertexAttribArray, (GLuint)) \
  X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
  X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
  X(void, GenTextures, (GLsizei, GLuint*)) \
  X(void, BindTexture, (GLenum, GLuint)) \
  X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
  X(void, TexParameteri, (GLenum, GLenum, GLint)) \
  X(void, ActiveTexture, (GLenum)) \
  X(void, BindFramebuffer, (GLenum, GLuint)) \
  X(void, GenFramebuffers, (GLsizei, GLuint*)) \
  X(void, DeleteFramebuffers, (GLsizei, const GLuint*)) \
  X(void, GenRenderbuffers, (GLsizei, GLuint*)) \
  X(void, DeleteRenderbuffers, (GLsizei, const GLuint*)) \
  X(void, BindRenderbuffer, (GLenum, GLuint)) \
  X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
  X(GLenum, CheckFramebufferStatus, (GLenum)) \
  X(void, DeleteTextures, (GLsizei, const GLuint*)) \
  X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
  X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
  X(const GLubyte*, GetString, (GLenum)) \
  X(void, GetIntegerv, (GLenum, GLint*)) \
  X(GLenum, GetError, (void)) \
  X(void, Finish, (void))

namespace {

struct GLApi {
#define X(ret, name, args) ret (*name) args;
  GLF(X)
#undef X
};

// Antialiasing functions, which not all GPUs have: if they are missing, drawing goes on without it.
//  - EXT_multisampled_render_to_texture (Mali and other tile-based GPUs): the samples stay in the
//    tile memory and are resolved for free when writing the texture; then a copy to the screen;
//  - OpenGL ES 3: multisample renderbuffer and glBlitFramebuffer (Mesa, for the tests on the PC).
struct GLOpt {
  void (*FramebufferTexture2DMultisampleEXT)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
  void (*RenderbufferStorageMultisampleEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
  void (*RenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
  void (*BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
  void (*DiscardFramebufferEXT)(GLenum, GLsizei, const GLenum*);
};

enum { AA_NONE = 0, AA_EXT, AA_ES3 };
// max: maximum samples (from GL_MAX_SAMPLES, up to 8); samples: those of the current target.
struct AAState { int mode, max, samples, w, h; GLuint fbo, tex, color_rb, depth_rb; GLenum depth_fmt; bool discard; };

struct WV { float x, y, z; uint8_t r, g, b, a; };          // world vertex, 16 bytes
struct HV { float x, y, u, v; uint8_t r, g, b, a; };       // 2D vertex (sky, antialiasing copy), 20 bytes
// Interface vertex (0.5.2): position, coordinates in the atlas, point relative to the center of the
// shape, type and softness, half sizes, cut, inset, skew, ring thickness, color.
struct UV { float x, y, u, v, qx, qy, type, soft, hx, hy, cut, inset, skew, ring; uint8_t r, g, b, a; };
struct StaticEntry { uint32_t id; GLuint vbo; int count; unsigned used; };

GLApi gl;
GLOpt glo;
AAState g_aa;
bool g_ok;
GLuint g_prog_world, g_prog_hud, g_prog_copy, g_prog_ui, g_vbo_dyn, g_vbo_hud, g_vbo_ui, g_tex_font;
GLint g_u_xf_ui, g_u_tex_ui;
UV* g_uv;
int g_uv_n, g_uv_cap;
GLint g_u_vp, g_u_xf, g_u_tex, g_u_xf_copy, g_u_tex_copy;
const int MAX_STATIC = 160;  // blocks of the map, of the menu and a few previous maps
StaticEntry g_static[MAX_STATIC];
int g_nstatic;
unsigned g_frame_no;
WV* g_wv;
int g_wv_n, g_wv_cap;
HV* g_hv;
int g_hv_n, g_hv_cap;
Vec3* g_tmp;
int g_tmp_cap;
char g_info[200];
int g_depth_bits = -1;
int g_nattribs = 8;       // how many attribute arrays to disable at the start of every frame

const char* VS_WORLD =
  "attribute vec3 a_pos;\n"
  "attribute vec4 a_col;\n"
  "uniform mat4 u_vp;\n"
  "varying lowp vec4 v_col;\n"
  "void main() { v_col = a_col; gl_Position = u_vp * vec4(a_pos, 1.0); }\n";
// In GLSL ES fragment shaders floats have no default precision: it is declared anyway, even
// though every variable already has its own, because some compilers demand it.
const char* FS_WORLD =
  "precision mediump float;\n"
  "varying lowp vec4 v_col;\n"
  "void main() { gl_FragColor = v_col; }\n";
// Texture coordinates in high precision where available: with medium (16 bits on Mali) in an atlas
// 1024 wide the error would be up to a quarter of a pixel and the glyphs would come out blurred.
const char* VS_HUD =
  "attribute vec2 a_pos;\n"
  "attribute vec2 a_uv;\n"
  "attribute vec4 a_col;\n"
  "uniform vec4 u_xf;\n"
  "varying highp vec2 v_uv;\n"
  "varying lowp vec4 v_col;\n"
  "void main() { v_uv = a_uv; v_col = a_col; gl_Position = vec4(a_pos * u_xf.xy + u_xf.zw, 0.0, 1.0); }\n";
const char* FS_HUD =
  "precision mediump float;\n"
  "uniform sampler2D u_tex;\n"
  "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
  "varying highp vec2 v_uv;\n"
  "#else\n"
  "varying mediump vec2 v_uv;\n"
  "#endif\n"
  "varying lowp vec4 v_col;\n"
  "void main() { gl_FragColor = vec4(v_col.rgb, v_col.a * texture2D(u_tex, v_uv).a); }\n";
// Copy to the screen of the image drawn with antialiasing.
const char* FS_COPY =
  "precision mediump float;\n"
  "uniform sampler2D u_tex;\n"
  "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
  "varying highp vec2 v_uv;\n"
  "#else\n"
  "varying mediump vec2 v_uv;\n"
  "#endif\n"
  "varying lowp vec4 v_col;\n"
  "void main() { gl_FragColor = vec4(texture2D(u_tex, v_uv).rgb, 1.0); }\n";
// Interface (0.5.2): glyphs and icons from the atlas (type 0) or shapes computed per pixel,
// with the same math as hud_shape_sdf and hud_shape_cover (hud.cpp). Distances in output pixels:
// they need high precision (with 16 bits, at 300 pixels from the center the error is a quarter).
const char* VS_UI =
  "attribute vec2 a_pos;\n"
  "attribute vec2 a_uv;\n"
  "attribute vec4 a_col;\n"
  "attribute vec4 a_q;\n"
  "attribute vec4 a_p;\n"
  "attribute vec2 a_e;\n"
  "uniform vec4 u_xf;\n"
  "varying highp vec2 v_uv;\n"
  "varying lowp vec4 v_col;\n"
  "varying highp vec4 v_q;\n"
  "varying highp vec4 v_p;\n"
  "varying highp vec2 v_e;\n"
  "void main() { v_uv = a_uv; v_col = a_col; v_q = a_q; v_p = a_p; v_e = a_e;\n"
  "  gl_Position = vec4(a_pos * u_xf.xy + u_xf.zw, 0.0, 1.0); }\n";
const char* FS_UI =
  "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
  "precision highp float;\n"
  "#else\n"
  "precision mediump float;\n"
  "#endif\n"
  "uniform sampler2D u_tex;\n"
  "varying vec2 v_uv;\n"
  "varying lowp vec4 v_col;\n"
  "varying vec4 v_q;\n"
  "varying vec4 v_p;\n"
  "varying vec2 v_e;\n"
  "float sd_ellipse(vec2 q, vec2 r) {\n"
  "  if (r.x <= 0.0 || r.y <= 0.0) return 1e9;\n"
  "  vec2 k = q / r;\n"
  "  float k0 = length(k);\n"
  "  float k1 = length(k / r);\n"
  "  return k1 > 1e-5 ? k0 * (k0 - 1.0) / k1 : -min(r.x, r.y);\n"
  "}\n"
  "void main() {\n"
  "  float t = v_q.z;\n"
  "  float a;\n"
  "  if (t < 0.5) {\n"
  "    a = texture2D(u_tex, v_uv).a;\n"
  "  } else {\n"
  "    vec2 q = v_q.xy;\n"
  "    vec2 hs = v_p.xy;\n"
  "    float d;\n"
  "    if (t > 1.5 && t < 2.5) {\n"
  "      d = sd_ellipse(q, hs);\n"
  "    } else if (t > 2.5 && t < 3.5) {\n"
  "      d = abs(sd_ellipse(q, hs - 0.5 * v_e.y)) - 0.5 * v_e.y;\n"
  "    } else {\n"
  "      if (t > 3.5) q.x += v_e.x * q.y;\n"
  "      d = max(abs(q.x) - hs.x, abs(q.y) - hs.y);\n"
  "      float c = v_p.z;\n"
  "      if (c > 0.0) {\n"
  "        float tl = (c - (q.x + hs.x) - (q.y + hs.y)) * 0.70710678;\n"
  "        float br = (c - (hs.x - q.x) - (hs.y - q.y)) * 0.70710678;\n"
  "        d = max(d, max(tl, br));\n"
  "      }\n"
  "      if (v_e.y > 0.0) d = abs(d + 0.5 * v_e.y) - 0.5 * v_e.y;\n"
  "    }\n"
  "    d += v_p.w;\n"
  "    float soft = v_q.w;\n"
  "    a = soft > 0.0 ? 1.0 / (1.0 + exp(3.404 * d / soft)) : clamp(0.5 - d, 0.0, 1.0);\n"
  "  }\n"
  "  gl_FragColor = vec4(v_col.rgb, v_col.a * a);\n"
  "}\n";

// Looks up a GL function: first in the GLES library already loaded by the frontend (the same way
// the cores that link directly against libGLESv2 find it), then among the global symbols,
// finally by asking the frontend for it.
void* resolve(glr_get_proc_t get_proc, const char* name, int how[3]) {
  static void* lib;
  static bool tried;
  if (!tried) {
    tried = true;
    lib = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) lib = dlopen("libGLESv2.so", RTLD_NOW | RTLD_GLOBAL);
  }
  void* p = lib ? dlsym(lib, name) : 0;
  if (p) { how[0]++; return p; }
  p = dlsym(RTLD_DEFAULT, name);
  if (p) { how[1]++; return p; }
  if (get_proc) {
    glr_proc_t f = get_proc(name);
    if (f) { how[2]++; return reinterpret_cast<void*>(f); }
  }
  return 0;
}

GLuint compile(GLenum type, const char* src) {
  const GLuint s = gl.CreateShader(type);
  gl.ShaderSource(s, 1, &src, 0);
  gl.CompileShader(s);
  GLint ok = 0;
  gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    GLsizei n = 0;
    gl.GetShaderInfoLog(s, (GLsizei)sizeof(log) - 1, &n, log);
    log[n < 0 ? 0 : n] = 0;
    plat_log("GL: shader non compilato: %s", log);
    gl.DeleteShader(s);
    return 0;
  }
  return s;
}

GLuint link(const char* vs_src, const char* fs_src, const char* const* attribs, int nattribs) {
  const GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
  const GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
  if (!vs || !fs) return 0;
  const GLuint p = gl.CreateProgram();
  gl.AttachShader(p, vs);
  gl.AttachShader(p, fs);
  for (int i = 0; i < nattribs; ++i) gl.BindAttribLocation(p, (GLuint)i, attribs[i]);
  gl.LinkProgram(p);
  gl.DeleteShader(vs);
  gl.DeleteShader(fs);
  GLint ok = 0;
  gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512];
    GLsizei n = 0;
    gl.GetProgramInfoLog(p, (GLsizei)sizeof(log) - 1, &n, log);
    log[n < 0 ? 0 : n] = 0;
    plat_log("GL: programma non collegato: %s", log);
    return 0;
  }
  return p;
}

inline void wv_reserve(int extra) {
  if (g_wv_n + extra <= g_wv_cap) return;
  g_wv_cap = (g_wv_n + extra) * 2 + 1024;
  g_wv = (WV*)realloc(g_wv, sizeof(WV) * (size_t)g_wv_cap);
}

inline void wv_push(Vec3 p, uint32_t c) {
  WV& v = g_wv[g_wv_n++];
  v.x = p.x; v.y = p.y; v.z = p.z;
  v.r = (uint8_t)(c >> 16); v.g = (uint8_t)(c >> 8); v.b = (uint8_t)c; v.a = 255;
}

// Transforms and lights an object on the CPU; discards the faces turned away if cull = true.
void emit_item(const DrawItem& it, const FrameDesc& f, bool cull, RenderStats* st) {
  const Mesh& m = *it.mesh;
  if (m.nv > g_tmp_cap) {
    g_tmp_cap = m.nv + 256;
    g_tmp = (Vec3*)realloc(g_tmp, sizeof(Vec3) * (size_t)g_tmp_cap);
  }
  for (int i = 0; i < m.nv; ++i) g_tmp[i] = xf_point(it.xf, m.v[i]);
  ItemSpace sp;
  item_space(it.xf, f.cam.eye, &sp);
  wv_reserve(m.nt * 3);
  st->tris_in += m.nt;
  for (int i = 0; i < m.nt; ++i) {
    const MeshTri& t = m.t[i];
    if (cull && !tri_facing(t, m.v, sp)) continue;
    const uint32_t c = shade_tri(t, sp, f.light, it.flash, it.dim);
    wv_push(g_tmp[t.a], c);
    wv_push(g_tmp[t.b], c);
    wv_push(g_tmp[t.c], c);
  }
}

// Fixed geometry: uploaded once and recognized by the mesh id. If the cache is full (for
// example after a few map changes) the least recently used entry is thrown away.
const StaticEntry* get_static(const DrawItem& it, const FrameDesc& f) {
  for (int i = 0; i < g_nstatic; ++i)
    if (g_static[i].id == it.mesh->id) {
      g_static[i].used = g_frame_no;
      return &g_static[i];
    }
  int slot = g_nstatic;
  if (g_nstatic >= MAX_STATIC) {
    slot = 0;
    for (int i = 1; i < MAX_STATIC; ++i)
      if (g_static[i].used < g_static[slot].used) slot = i;
    if (g_static[slot].used == g_frame_no) return 0;   // all in use in this frame
    gl.DeleteBuffers(1, &g_static[slot].vbo);
  } else {
    g_nstatic++;
  }
  RenderStats dummy;
  memset(&dummy, 0, sizeof(dummy));
  g_wv_n = 0;
  emit_item(it, f, false, &dummy);
  StaticEntry& e = g_static[slot];
  e.id = it.mesh->id;
  e.used = g_frame_no;
  e.count = g_wv_n;
  gl.GenBuffers(1, &e.vbo);
  gl.BindBuffer(GL_ARRAY_BUFFER, e.vbo);
  gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(WV) * (size_t)g_wv_n), g_wv, GL_STATIC_DRAW);
  g_wv_n = 0;
  return &e;
}

void world_pointers(void) {
  gl.VertexAttribPointer(0, 3, GL_FLOAT, 0, (GLsizei)sizeof(WV), (const void*)0);
  gl.VertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, 1, (GLsizei)sizeof(WV), (const void*)12);
}

inline void hv_reserve(int extra) {
  if (g_hv_n + extra <= g_hv_cap) return;
  g_hv_cap = (g_hv_n + extra) * 2 + 1024;
  g_hv = (HV*)realloc(g_hv, sizeof(HV) * (size_t)g_hv_cap);
}

inline void hv_push(float x, float y, float u, float v, uint32_t rgb, uint8_t a) {
  HV& o = g_hv[g_hv_n++];
  o.x = x; o.y = y; o.u = u; o.v = v;
  o.r = (uint8_t)(rgb >> 16); o.g = (uint8_t)(rgb >> 8); o.b = (uint8_t)rgb; o.a = a;
}

// Rectangle in pixels (origin at the top left), with different colors at the top and bottom.
void hud_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              uint32_t top, uint32_t bot, uint8_t a) {
  hv_reserve(6);
  hv_push(x0, y0, u0, v0, top, a);
  hv_push(x0, y1, u0, v1, bot, a);
  hv_push(x1, y1, u1, v1, bot, a);
  hv_push(x0, y0, u0, v0, top, a);
  hv_push(x1, y1, u1, v1, bot, a);
  hv_push(x1, y0, u1, v0, top, a);
}

float SOLID_U, SOLID_V;   // a point of the atlas's solid block (solid-color rectangles)

void quads_flush(int w, int h, GLuint prog, GLint u_xf, GLint u_tex, GLuint tex) {
  if (g_hv_n == 0) return;
  gl.UseProgram(prog);
  gl.Uniform4f(u_xf, 2.0f / (float)w, -2.0f / (float)h, -1.0f, 1.0f);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindTexture(GL_TEXTURE_2D, tex);
  gl.Uniform1i(u_tex, 0);
  gl.BindBuffer(GL_ARRAY_BUFFER, g_vbo_hud);
  gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(HV) * (size_t)g_hv_n), g_hv, GL_STREAM_DRAW);
  gl.EnableVertexAttribArray(0);
  gl.EnableVertexAttribArray(1);
  gl.EnableVertexAttribArray(2);
  gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, (GLsizei)sizeof(HV), (const void*)0);
  gl.VertexAttribPointer(1, 2, GL_FLOAT, 0, (GLsizei)sizeof(HV), (const void*)8);
  gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, (GLsizei)sizeof(HV), (const void*)16);
  gl.DrawArrays(GL_TRIANGLES, 0, g_hv_n);
  gl.DisableVertexAttribArray(2);
  g_hv_n = 0;
}

void hud_flush(int w, int h) { quads_flush(w, h, g_prog_hud, g_u_xf, g_u_tex, g_tex_font); }

// --- interface --------------------------------------------------------------------------------

inline void uv_reserve(int extra) {
  if (g_uv_n + extra <= g_uv_cap) return;
  g_uv_cap = (g_uv_n + extra) * 2 + 1024;
  g_uv = (UV*)realloc(g_uv, sizeof(UV) * (size_t)g_uv_cap);
}

// Interface quadrilateral: the four corners (top left, bottom left, bottom right, top right) with
// their coordinates and their colors; the rest is the same for all.
struct UIQuad { float x[4], y[4], u[4], v[4], qx[4], qy[4]; uint32_t col[4]; float type, soft, hx, hy, cut, inset, skew, ring; };

void ui_push(const UIQuad& q) {
  static const int ORDER[6] = { 0, 1, 2, 0, 2, 3 };
  uv_reserve(6);
  for (int i = 0; i < 6; ++i) {
    const int k = ORDER[i];
    UV& o = g_uv[g_uv_n++];
    o.x = q.x[k]; o.y = q.y[k]; o.u = q.u[k]; o.v = q.v[k]; o.qx = q.qx[k]; o.qy = q.qy[k];
    o.type = q.type; o.soft = q.soft; o.hx = q.hx; o.hy = q.hy; o.cut = q.cut; o.inset = q.inset; o.skew = q.skew; o.ring = q.ring;
    const uint32_t c = q.col[k];
    o.r = (uint8_t)(c >> 16); o.g = (uint8_t)(c >> 8); o.b = (uint8_t)c; o.a = (uint8_t)(c >> 24);
  }
}

void ui_rect(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t argb) {
  UIQuad q;
  memset(&q, 0, sizeof(q));
  q.x[0] = q.x[1] = x0; q.x[2] = q.x[3] = x1;
  q.y[0] = q.y[3] = y0; q.y[1] = q.y[2] = y1;
  q.u[0] = q.u[1] = u0; q.u[2] = q.u[3] = u1;
  q.v[0] = q.v[3] = v0; q.v[1] = q.v[2] = v1;
  for (int i = 0; i < 4; ++i) q.col[i] = argb;
  ui_push(q);
}

// Gradient color at the output point (px, py): the same line as in hud.cpp, extended outside
// the shape (to the corners of the box) and then clamped channel by channel.
uint32_t grad_at(const HudShape& sh, float s, float px, float py) {
  if (sh.c0 == sh.c1) return sh.c0;
  const float an = sh.ang * 0.017453292f, dx = sinf(an), dy = -cosf(an);
  const float L = fabsf(sh.w * s * dx) + fabsf(sh.h * s * dy);
  const float cx = (sh.x + sh.w * 0.5f) * s, cy = (sh.y + sh.h * 0.5f) * s;
  const float t = L > 1e-3f ? ((px - cx) * dx + (py - cy) * dy) / L + 0.5f : 0.5f;
  uint32_t out = 0;
  for (int i = 0; i < 4; ++i) {
    const float a = (float)((sh.c0 >> (8 * i)) & 255u), b = (float)((sh.c1 >> (8 * i)) & 255u);
    float v = a + (b - a) * t + 0.5f;
    v = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
    out |= (uint32_t)v << (8 * i);
  }
  return out;
}

// A rectangular piece of the shape (in output pixels): with the distance from the edge (sdf) or,
// where the shape is certainly solid, only the color (the atlas's solid block, like rectangles).
void ui_shape_part(const HudShape& sh, float s, float x0, float y0, float x1, float y1, bool sdf) {
  if (x1 <= x0 || y1 <= y0) return;
  UIQuad q;
  memset(&q, 0, sizeof(q));
  const float cx = (sh.x + sh.w * 0.5f) * s, cy = (sh.y + sh.h * 0.5f) * s;
  q.x[0] = q.x[1] = x0; q.x[2] = q.x[3] = x1;
  q.y[0] = q.y[3] = y0; q.y[1] = q.y[2] = y1;
  float su, sv;
  hud_solid_uv(&su, &sv);
  for (int i = 0; i < 4; ++i) {
    q.u[i] = su; q.v[i] = sv;
    q.qx[i] = q.x[i] - cx; q.qy[i] = q.y[i] - cy;
    q.col[i] = grad_at(sh, s, q.x[i], q.y[i]);
  }
  if (sdf) {
    q.type = (float)sh.type;
    q.soft = sh.soft * s;
    q.hx = sh.w * 0.5f * s; q.hy = sh.h * 0.5f * s;
    q.cut = sh.cut * s; q.inset = sh.inset * s; q.skew = sh.skew; q.ring = sh.ring * s;
  }
  ui_push(q);
}

// Interface shape. Large rectangles (panels, scrims, glows) are split: the distance from the edge
// is computed only in the strips near the edge and in the two cut corners, the solid inside is a
// color rectangle ("hollow" glows are not drawn there at all). For the shader the pixels at the
// edges are the same; only how much the GPU works changes.
void ui_shape(const HudShape& sh, const HudCmd& c, float s) {
  const float bx0 = (float)c.x * s, by0 = (float)c.y * s, bx1 = (float)(c.x + c.w) * s, by1 = (float)(c.y + c.h) * s;
  if (sh.type == HS_BOX && sh.ring <= 0.0f) {
    // Certainly solid where the distance is below -m: for sharp edges half a pixel, for glows
    // 2.5 times the softness (coverage 0.9998).
    const float m = sh.soft > 0.0f ? 2.5f * sh.soft * s : 0.5f;
    const float X = sh.x * s, Y = sh.y * s, W = sh.w * s, H = sh.h * s;
    const float e = sh.inset * s + m;                          // margin from the sides
    const float k = sh.cut > 0.0f ? sh.cut * s + 1.41421356f * e : e;   // margin from the cut corners
    if (W >= 2.0f * k + 2.0f && H >= 2.0f * k + 2.0f && W > 2.0f * e + 8.0f && H > 2.0f * e + 8.0f) {
      const float xe0 = X + e, xe1 = X + W - e, ye0 = Y + e, ye1 = Y + H - e;
      // With a cut small compared to the margin (k < 2e) the corner pieces are not needed: the
      // limits stop at the sides, so the pieces stay side by side without overlapping.
      const float yk0 = Y + k, yk1 = Y + H - k, xk0 = fmaxf(X + k - e, xe0), xk1 = fminf(X + W - k + e, xe1);
      // Strips with the distance: top, bottom, left, right, and the two cut corners.
      ui_shape_part(sh, s, bx0, by0, bx1, ye0, true);
      ui_shape_part(sh, s, bx0, ye1, bx1, by1, true);
      ui_shape_part(sh, s, bx0, ye0, xe0, ye1, true);
      ui_shape_part(sh, s, xe1, ye0, bx1, ye1, true);
      ui_shape_part(sh, s, xe0, ye0, xk0, yk0, true);
      ui_shape_part(sh, s, xk1, yk1, xe1, ye1, true);
      // The solid inside: the middle band and the two next to the cut corners.
      if (!sh.hollow) {
        ui_shape_part(sh, s, xe0, yk0, xe1, yk1, false);
        ui_shape_part(sh, s, xk0, ye0, xe1, yk0, false);
        ui_shape_part(sh, s, xe0, yk1, xk1, ye1, false);
      }
      return;
    }
  }
  ui_shape_part(sh, s, bx0, by0, bx1, by1, true);
}

void ui_flush(int w, int h) {
  if (g_uv_n == 0) return;
  gl.UseProgram(g_prog_ui);
  gl.Uniform4f(g_u_xf_ui, 2.0f / (float)w, -2.0f / (float)h, -1.0f, 1.0f);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindTexture(GL_TEXTURE_2D, g_tex_font);
  gl.Uniform1i(g_u_tex_ui, 0);
  gl.BindBuffer(GL_ARRAY_BUFFER, g_vbo_ui);
  gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(UV) * (size_t)g_uv_n), g_uv, GL_STREAM_DRAW);
  const GLsizei st = (GLsizei)sizeof(UV);
  for (int i = 0; i < 6; ++i) gl.EnableVertexAttribArray((GLuint)i);
  gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, st, (const void*)0);
  gl.VertexAttribPointer(1, 2, GL_FLOAT, 0, st, (const void*)8);
  gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, st, (const void*)56);
  gl.VertexAttribPointer(3, 4, GL_FLOAT, 0, st, (const void*)16);
  gl.VertexAttribPointer(4, 4, GL_FLOAT, 0, st, (const void*)32);
  gl.VertexAttribPointer(5, 2, GL_FLOAT, 0, st, (const void*)48);
  gl.DrawArrays(GL_TRIANGLES, 0, g_uv_n);
  for (int i = 2; i < 6; ++i) gl.DisableVertexAttribArray((GLuint)i);
  g_uv_n = 0;
}

// Simplified interface, with the old HUD program (only if the new shader is missing): texts,
// icons and rectangles as always; shapes become solid rectangles or square outlines, with the
// gradient from top to bottom; there are no glows or soft shadows.
void hud_simple(const Hud* hd, float k, int w, int h) {
  g_hv_n = 0;
  for (int i = 0; i < hd->n; ++i) {
    const HudCmd& c = hd->cmd[i];
    const float x0 = (float)c.x * k, y0 = (float)c.y * k;
    const float x1 = x0 + (float)c.w * k, y1 = y0 + (float)c.h * k;
    if (c.kind == HUD_SHAPE) {
      if (c.idx >= hd->ns) continue;
      const HudShape& sh = hd->shape[c.idx];
      if (sh.soft > 0.0f) continue;
      const float in = sh.inset * k;
      const float sx0 = sh.x * k + in, sy0 = sh.y * k + in, sx1 = (sh.x + sh.w) * k - in, sy1 = (sh.y + sh.h) * k - in;
      if (sx1 <= sx0 || sy1 <= sy0) continue;
      const uint32_t top = grad_at(sh, k, (sx0 + sx1) * 0.5f, sy0), bot = grad_at(sh, k, (sx0 + sx1) * 0.5f, sy1);
      const uint8_t a = (uint8_t)(top >> 24);
      const float r = sh.ring * k;
      if (r > 0.0f && 2.0f * r < sx1 - sx0 && 2.0f * r < sy1 - sy0) {
        hud_quad(sx0, sy0, sx1, sy0 + r, SOLID_U, SOLID_V, SOLID_U, SOLID_V, top, top, a);
        hud_quad(sx0, sy1 - r, sx1, sy1, SOLID_U, SOLID_V, SOLID_U, SOLID_V, bot, bot, a);
        hud_quad(sx0, sy0 + r, sx0 + r, sy1 - r, SOLID_U, SOLID_V, SOLID_U, SOLID_V, top, bot, a);
        hud_quad(sx1 - r, sy0 + r, sx1, sy1 - r, SOLID_U, SOLID_V, SOLID_U, SOLID_V, top, bot, a);
      } else {
        hud_quad(sx0, sy0, sx1, sy1, SOLID_U, SOLID_V, SOLID_U, SOLID_V, top, bot, a);
      }
    } else if (c.kind == HUD_GLYPH) {
      float u0, v0, u1, v1;
      hud_glyph_uv(c.idx, &u0, &v0, &u1, &v1);
      hud_quad(x0, y0, x1, y1, u0, v0, u1, v1, c.argb, c.argb, (uint8_t)(c.argb >> 24));
    } else if (c.kind == HUD_DIM) {
      hud_quad(x0, y0, x1, y1, SOLID_U, SOLID_V, SOLID_U, SOLID_V, 0, 0, 0xA0);
    } else {
      hud_quad(x0, y0, x1, y1, SOLID_U, SOLID_V, SOLID_U, SOLID_V, c.argb, c.argb, 255);
    }
  }
  gl.Enable(GL_BLEND);
  gl.BlendEquation(GL_FUNC_ADD);
  gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  hud_flush(w, h);
}

// --- antialiasing ---------------------------------------------------------------------

bool has_ext(const char* list, const char* name) {
  if (!list) return false;
  const size_t n = strlen(name);
  for (const char* p = strstr(list, name); p; p = strstr(p + 1, name))
    if ((p == list || p[-1] == ' ') && (p[n] == 0 || p[n] == ' ')) return true;
  return false;
}

void aa_free(bool gl_alive) {
  if (gl_alive) {
    if (g_aa.fbo) gl.DeleteFramebuffers(1, &g_aa.fbo);
    if (g_aa.tex) gl.DeleteTextures(1, &g_aa.tex);
    if (g_aa.color_rb) gl.DeleteRenderbuffers(1, &g_aa.color_rb);
    if (g_aa.depth_rb) gl.DeleteRenderbuffers(1, &g_aa.depth_rb);
  }
  g_aa.fbo = g_aa.tex = g_aa.color_rb = g_aa.depth_rb = 0;
  g_aa.w = g_aa.h = 0;
}

// Test of the EXT path where the GPU does not have it (PUPAZZI_AA=prova): the same calls, but with
// a single sample. The image must come out identical to the one without antialiasing.
// PUPAZZI_AA=prova8: as if the GPU went up to 8; =prova8no: as if it said 8 but then refused it
// (it falls back to 4).
bool g_fake_reject8;
void fake_fbtex_ms(GLenum target, GLenum att, GLenum textarget, GLuint tex, GLint level, GLsizei samples) {
  if (g_fake_reject8 && samples > 4) return;                 // no color: the target is not complete
  gl.FramebufferTexture2D(target, att, textarget, tex, level);
}
void fake_rb_ms(GLenum target, GLsizei samples, GLenum fmt, GLsizei w, GLsizei h) {
  if (g_fake_reject8 && samples > 4) return;                 // renderbuffer without storage: incomplete
  gl.RenderbufferStorage(target, fmt, w, h);
}

// Chooses the antialiasing mode. PUPAZZI_AA=0 turns it off; =ext or =es3 force a mode, =prova
// tests the EXT path with a single sample (tests on the PC).
void aa_init(glr_get_proc_t get_proc, const char* ext, const char* version) {
  memset(&glo, 0, sizeof(glo));
  memset(&g_aa, 0, sizeof(g_aa));
  const char* want = getenv("PUPAZZI_AA");
  if (want && !strcmp(want, "0")) {
    plat_log("GL: antialias spento (PUPAZZI_AA=0)");
    return;
  }
  int how[3] = { 0, 0, 0 };
  glo.FramebufferTexture2DMultisampleEXT = reinterpret_cast<void (*)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei)>(
      resolve(get_proc, "glFramebufferTexture2DMultisampleEXT", how));
  glo.RenderbufferStorageMultisampleEXT = reinterpret_cast<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei)>(
      resolve(get_proc, "glRenderbufferStorageMultisampleEXT", how));
  glo.RenderbufferStorageMultisample = reinterpret_cast<void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei)>(
      resolve(get_proc, "glRenderbufferStorageMultisample", how));
  glo.BlitFramebuffer = reinterpret_cast<void (*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)>(
      resolve(get_proc, "glBlitFramebuffer", how));
  glo.DiscardFramebufferEXT = reinterpret_cast<void (*)(GLenum, GLsizei, const GLenum*)>(
      resolve(get_proc, "glDiscardFramebufferEXT", how));
  int major = 0, minor = 0;
  if (version) sscanf(version, "OpenGL ES %d.%d", &major, &minor);
  bool ext_ok = has_ext(ext, "GL_EXT_multisampled_render_to_texture") && glo.FramebufferTexture2DMultisampleEXT &&
                glo.RenderbufferStorageMultisampleEXT;
  bool es3_ok = major >= 3 && glo.RenderbufferStorageMultisample && glo.BlitFramebuffer;
  if (want && !strcmp(want, "es3")) ext_ok = false;
  if (want && !strcmp(want, "ext")) es3_ok = false;
  GLint max_samples = 0;
  if (ext_ok || es3_ok) gl.GetIntegerv(GL_MAX_SAMPLES, &max_samples);
  while (gl.GetError() != 0) {}
  g_fake_reject8 = false;
  if (want && !strncmp(want, "prova", 5)) {
    glo.FramebufferTexture2DMultisampleEXT = fake_fbtex_ms;
    glo.RenderbufferStorageMultisampleEXT = fake_rb_ms;
    ext_ok = true;
    max_samples = strncmp(want, "prova8", 6) ? 4 : 8;
    g_fake_reject8 = !strcmp(want, "prova8no");
    plat_log("GL: prova del percorso dell'antialias EXT con un solo campione (PUPAZZI_AA=%s)", want);
  }
  if ((!ext_ok && !es3_ok) || max_samples < 2) {
    plat_log("GL: antialias non disponibile (EXT_multisampled_render_to_texture %s, OpenGL ES %d.%d, campioni %d)",
             has_ext(ext, "GL_EXT_multisampled_render_to_texture") ? "si'" : "no", major, minor, (int)max_samples);
    return;
  }
  g_aa.mode = ext_ok ? AA_EXT : AA_ES3;
  // Since 0.5.4 up to 8 samples (the game asks for 4 or 8, see game_aa_wanted).
  g_aa.max = max_samples >= 8 ? 8 : (max_samples >= 4 ? 4 : (int)max_samples);
  g_aa.depth_fmt = (major >= 3 || has_ext(ext, "GL_OES_depth24")) ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT16;
  g_aa.discard = g_aa.mode == AA_EXT && glo.DiscardFramebufferEXT && has_ext(ext, "GL_EXT_discard_framebuffer");
  plat_log("GL: antialias fino a %dx disponibile (%s, profondita' %d bit)", g_aa.max,
           g_aa.mode == AA_EXT ? "EXT_multisampled_render_to_texture" : "OpenGL ES 3, renderbuffer e blit",
           g_aa.depth_fmt == GL_DEPTH_COMPONENT24 ? 24 : 16);
}

// Target with want samples per pixel (at most g_aa.max) of the size of the image; if the GPU
// refuses it with 8 it retries with 4, with 4 antialiasing is turned off for the whole session;
// this is written to the log.
bool aa_ready(int w, int h, int want) {
  if (g_aa.mode == AA_NONE) return false;
  const int s = want > g_aa.max ? g_aa.max : want;
  if (s < 2) return false;
  if (g_aa.fbo && g_aa.w == w && g_aa.h == h && g_aa.samples == s) return true;
  aa_free(true);
  g_aa.samples = s;
  while (gl.GetError() != 0) {}
  gl.GenFramebuffers(1, &g_aa.fbo);
  gl.BindFramebuffer(GL_FRAMEBUFFER, g_aa.fbo);
  if (g_aa.mode == AA_EXT) {
    gl.GenTextures(1, &g_aa.tex);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, g_aa.tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    glo.FramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_aa.tex, 0, g_aa.samples);
    gl.GenRenderbuffers(1, &g_aa.depth_rb);
    gl.BindRenderbuffer(GL_RENDERBUFFER, g_aa.depth_rb);
    glo.RenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, g_aa.samples, g_aa.depth_fmt, w, h);
  } else {
    gl.GenRenderbuffers(1, &g_aa.color_rb);
    gl.BindRenderbuffer(GL_RENDERBUFFER, g_aa.color_rb);
    glo.RenderbufferStorageMultisample(GL_RENDERBUFFER, g_aa.samples, GL_RGBA8, w, h);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_aa.color_rb);
    gl.GenRenderbuffers(1, &g_aa.depth_rb);
    gl.BindRenderbuffer(GL_RENDERBUFFER, g_aa.depth_rb);
    glo.RenderbufferStorageMultisample(GL_RENDERBUFFER, g_aa.samples, g_aa.depth_fmt, w, h);
  }
  gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_aa.depth_rb);
  gl.BindRenderbuffer(GL_RENDERBUFFER, 0);
  const GLenum status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
  const GLenum err = gl.GetError();
  if (status != GL_FRAMEBUFFER_COMPLETE || err) {
    aa_free(true);
    while (gl.GetError() != 0) {}
    if (s > 4) {
      plat_log("GL: antialias %dx rifiutato dalla GPU (stato 0x%04x, errore 0x%04x): si prova con 4", s, (unsigned)status, (unsigned)err);
      g_aa.max = 4;
      return aa_ready(w, h, 4);
    }
    plat_log("GL: antialias rifiutato dalla GPU (stato 0x%04x, errore 0x%04x): si disegna senza", (unsigned)status, (unsigned)err);
    g_aa.mode = AA_NONE;
    return false;
  }
  g_aa.w = w;
  g_aa.h = h;
  plat_log("GL: antialias %dx pronto a %dx%d", g_aa.samples, w, h);
  return true;
}

}  // namespace

bool glr_aa_available(void) { return g_ok && g_aa.mode != AA_NONE; }
int glr_aa_max(void) { return glr_aa_available() ? g_aa.max : 0; }

bool glr_init(glr_get_proc_t get_proc) {
  memset(&gl, 0, sizeof(gl));
  g_ok = false;
  g_nstatic = 0;
  g_depth_bits = -1;
  int how[3] = { 0, 0, 0 };
  const char* missing = 0;
#define X(ret, name, args) \
  gl.name = reinterpret_cast<ret (*) args>(resolve(get_proc, "gl" #name, how)); \
  if (!gl.name && !missing) missing = "gl" #name;
  GLF(X)
#undef X
  if (missing) {
    plat_log("GL: funzione non trovata: %s", missing);
    return false;
  }
  plat_log("GL: funzioni risolte: da libGLESv2 %d, globali %d, dal frontend %d", how[0], how[1], how[2]);
  const char* vendor = (const char*)gl.GetString(GL_VENDOR);
  const char* renderer = (const char*)gl.GetString(GL_RENDERER);
  const char* version = (const char*)gl.GetString(GL_VERSION);
  snprintf(g_info, sizeof(g_info), "%s | %s", renderer ? renderer : "?", version ? version : "?");
  plat_log("GL: vendor=%s renderer=%s version=%s", vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?");
  {
    // Information useful for future choices (formats, extensions): only in the log.
    const char* glsl = (const char*)gl.GetString(GL_SHADING_LANGUAGE_VERSION);
    GLint max_tex = 0;
    gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    plat_log("GL: glsl=%s, texture massima %d", glsl ? glsl : "?", (int)max_tex);
    GLint na = 0;
    gl.GetIntegerv(GL_MAX_VERTEX_ATTRIBS, &na);
    g_nattribs = na < 8 ? 8 : (na > 32 ? 32 : (int)na);
    const char* ext = (const char*)gl.GetString(GL_EXTENSIONS);
    if (ext) {
      // The list is long: it is split into lines of about 400 characters, without cutting names.
      size_t len = strlen(ext), pos = 0;
      while (pos < len) {
        size_t n = len - pos;
        if (n > 400) {
          n = 400;
          while (n > 0 && ext[pos + n] != ' ') --n;
          if (n == 0) n = 400;
        }
        plat_log("GL: estensioni: %.*s", (int)n, ext + pos);
        pos += n;
        while (pos < len && ext[pos] == ' ') ++pos;
      }
    }
  }

  static const char* const world_attr[2] = { "a_pos", "a_col" };
  static const char* const hud_attr[3] = { "a_pos", "a_uv", "a_col" };
  g_prog_world = link(VS_WORLD, FS_WORLD, world_attr, 2);
  g_prog_hud = link(VS_HUD, FS_HUD, hud_attr, 3);
  g_prog_copy = link(VS_HUD, FS_COPY, hud_attr, 3);
  static const char* const ui_attr[6] = { "a_pos", "a_uv", "a_col", "a_q", "a_p", "a_e" };
  // Without the interface shader (it should not happen: if the driver refused it) the game is still
  // playable, with the simplified interface. PUPAZZI_UI_SEMPLICE=1 forces it, to test it.
  const char* simple = getenv("PUPAZZI_UI_SEMPLICE");
  g_prog_ui = (simple && *simple == '1') ? 0 : link(VS_UI, FS_UI, ui_attr, 6);
  if (!g_prog_world || !g_prog_hud || !g_prog_copy) return false;
  if (!g_prog_ui) plat_log("GL: ERRORE: shader dell'interfaccia non disponibile: interfaccia semplificata (forme squadrate, senza aloni)");
  else {
    g_u_xf_ui = gl.GetUniformLocation(g_prog_ui, "u_xf");
    g_u_tex_ui = gl.GetUniformLocation(g_prog_ui, "u_tex");
  }
  g_u_vp = gl.GetUniformLocation(g_prog_world, "u_vp");
  g_u_xf = gl.GetUniformLocation(g_prog_hud, "u_xf");
  g_u_tex = gl.GetUniformLocation(g_prog_hud, "u_tex");
  g_u_xf_copy = gl.GetUniformLocation(g_prog_copy, "u_xf");
  g_u_tex_copy = gl.GetUniformLocation(g_prog_copy, "u_tex");
  aa_init(get_proc, (const char*)gl.GetString(GL_EXTENSIONS), version);
  gl.GenBuffers(1, &g_vbo_dyn);
  gl.GenBuffers(1, &g_vbo_hud);
  gl.GenBuffers(1, &g_vbo_ui);

  // Atlas of the interface glyphs and icons (alpha only, feathered edges, glows) with the solid
  // block for rectangles. Linear filter: at 640x480 the glyphs fall pixel for pixel and come out
  // identical to the software renderer; at other resolutions (tests only) they stay smooth.
  int aw = 0, ah = 0;
  const uint8_t* atlas = hud_atlas(&aw, &ah);
  hud_solid_uv(&SOLID_U, &SOLID_V);
  gl.GenTextures(1, &g_tex_font);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindTexture(GL_TEXTURE_2D, g_tex_font);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, aw, ah, 0, GL_ALPHA, GL_UNSIGNED_BYTE, atlas);
  gl.BindTexture(GL_TEXTURE_2D, 0);
  gl.BindBuffer(GL_ARRAY_BUFFER, 0);
  const GLenum err = gl.GetError();
  if (err) plat_log("GL: errore 0x%04x durante la preparazione", err);
  g_ok = true;
  return true;
}

void glr_context_lost(void) {
  g_ok = false;
  g_nstatic = 0;
  g_prog_world = g_prog_hud = g_prog_copy = g_prog_ui = g_vbo_dyn = g_vbo_hud = g_vbo_ui = g_tex_font = 0;
  aa_free(false);
  g_aa.mode = AA_NONE;
}

// Frees the working memory: to be called when the core is unloaded.
void glr_shutdown(void) {
  g_ok = false;
  free(g_wv); g_wv = 0; g_wv_n = g_wv_cap = 0;
  free(g_hv); g_hv = 0; g_hv_n = g_hv_cap = 0;
  free(g_uv); g_uv = 0; g_uv_n = g_uv_cap = 0;
  free(g_tmp); g_tmp = 0; g_tmp_cap = 0;
}

const char* glr_info(void) { return g_info; }
int glr_depth_bits(void) { return g_depth_bits; }

void glr_render(const FrameDesc* f, unsigned fbo, RenderStats* st, bool finish) {
  if (!g_ok) return;
  g_frame_no++;
  const double t0 = plat_now_ms();
  const int w = f->w, h = f->h;
  // With antialiasing the world is drawn into the multisample target, then brought into the
  // frontend's framebuffer; the HUD always goes directly into the frontend's one.
  if (g_depth_bits < 0) {
    // Depth of the frontend's framebuffer (the one that matters without antialiasing).
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLint bits = 0;
    gl.GetIntegerv(GL_DEPTH_BITS, &bits);
    g_depth_bits = bits;
    plat_log("GL: buffer di profondita' a %d bit", (int)bits);
  }
  const bool aa = f->aa > 0 && aa_ready(w, h, f->aa);
  gl.BindFramebuffer(GL_FRAMEBUFFER, aa ? g_aa.fbo : fbo);
  gl.Viewport(0, 0, w, h);
  // The state inherited from the frontend (or from a previous core) is not guaranteed: everything
  // that affects drawing is reset, not only what RetroArch usually touches.
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_STENCIL_TEST);
  gl.Disable(GL_BLEND);
  gl.Disable(GL_DEPTH_TEST);
  gl.Disable(GL_CULL_FACE);
  gl.Disable(GL_POLYGON_OFFSET_FILL);
  gl.Disable(GL_SAMPLE_ALPHA_TO_COVERAGE);
  gl.Disable(GL_SAMPLE_COVERAGE);
  // Attribute arrays left enabled by the frontend's shaders: only 0, 1 and 2 are needed here,
  // and an enabled array pointing to a buffer shorter than the draw is a needless risk.
  for (int i = 0; i < g_nattribs; ++i) gl.DisableVertexAttribArray((GLuint)i);
  gl.ColorMask(1, 1, 1, 1);
  gl.DepthMask(1);
  gl.StencilMask(0xFFFFFFFFu);
  gl.DepthRangef(0.0f, 1.0f);
  gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  gl.ClearDepthf(1.0f);
  gl.ClearStencil(0);
  // All planes are cleared: on tile-based GPUs this avoids reloading the previous content.
  gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  // Sky.
  g_hv_n = 0;
  hud_quad(0, 0, (float)w, (float)h, SOLID_U, SOLID_V, SOLID_U, SOLID_V, f->sky_top, f->sky_bot, 255);
  hud_flush(w, h);

  // World.
  Plane fr[5];
  frustum_planes(f->cam.vp, f->cam.znear, fr);
  gl.UseProgram(g_prog_world);
  gl.UniformMatrix4fv(g_u_vp, 1, 0, f->cam.vp.m);
  gl.Enable(GL_DEPTH_TEST);
  gl.DepthFunc(GL_LESS);
  gl.Enable(GL_CULL_FACE);
  gl.CullFace(GL_BACK);
  gl.FrontFace(GL_CCW);
  gl.EnableVertexAttribArray(0);
  gl.EnableVertexAttribArray(1);
  st->items_in += f->nitems;
  for (int i = 0; i < f->nitems; ++i) {
    const DrawItem& it = f->items[i];
    if (!(it.flags & ITEM_STATIC)) continue;
    if (sphere_outside(fr, xf_point(it.xf, it.mesh->center), it.mesh->radius * xf_max_scale(it.xf))) continue;
    const StaticEntry* e = get_static(it, *f);
    if (!e) continue;
    gl.BindBuffer(GL_ARRAY_BUFFER, e->vbo);
    world_pointers();
    gl.DrawArrays(GL_TRIANGLES, 0, e->count);
    st->items_drawn++;
    st->draw_calls++;
    st->tris_in += it.mesh->nt;
    st->tris_drawn += e->count / 3;
  }
  g_wv_n = 0;
  for (int i = 0; i < f->nitems; ++i) {
    const DrawItem& it = f->items[i];
    if (it.flags & ITEM_STATIC) continue;
    if (sphere_outside(fr, xf_point(it.xf, it.mesh->center), it.mesh->radius * xf_max_scale(it.xf))) continue;
    st->items_drawn++;
    emit_item(it, *f, true, st);
  }
  if (g_wv_n > 0) {
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vbo_dyn);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(WV) * (size_t)g_wv_n), g_wv, GL_STREAM_DRAW);
    world_pointers();
    gl.DrawArrays(GL_TRIANGLES, 0, g_wv_n);
    st->draw_calls++;
    st->tris_drawn += g_wv_n / 3;
  }
  gl.Disable(GL_DEPTH_TEST);
  gl.Disable(GL_CULL_FACE);
  if (aa) {
    if (g_aa.discard) {
      const GLenum att = GL_DEPTH_ATTACHMENT;   // depth is no longer needed: it is not written to memory
      glo.DiscardFramebufferEXT(GL_FRAMEBUFFER, 1, &att);
    }
    if (g_aa.mode == AA_ES3) {
      gl.BindFramebuffer(GL_READ_FRAMEBUFFER, g_aa.fbo);
      gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
      glo.BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    } else {
      gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
      gl.Viewport(0, 0, w, h);
      gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
      g_hv_n = 0;
      hud_quad(0, 0, (float)w, (float)h, 0.0f, 1.0f, 1.0f, 0.0f, 0xFFFFFF, 0xFFFFFF, 255);   // the texture has its origin at the bottom
      quads_flush(w, h, g_prog_copy, g_u_xf_copy, g_u_tex_copy, g_aa.tex);
      st->draw_calls++;
    }
  }
  const double t1 = plat_now_ms();

  // HUD and menus (0.5.2: a single draw with the interface shader, in the order of the commands).
  if (f->hud && !g_prog_ui) {
    hud_simple(f->hud, (float)f->hud_scale * 0.5f, w, h);
    st->draw_calls++;
  } else if (f->hud) {
    const float k = (float)f->hud_scale * 0.5f;   // the commands are in pixels at 640x480
    const Hud* hd = f->hud;
    g_uv_n = 0;
    for (int i = 0; i < hd->n; ++i) {
      const HudCmd& c = hd->cmd[i];
      const float x0 = (float)c.x * k, y0 = (float)c.y * k;
      const float x1 = x0 + (float)c.w * k, y1 = y0 + (float)c.h * k;
      if (c.kind == HUD_SHAPE) {
        if (c.idx < hd->ns) ui_shape(hd->shape[c.idx], c, k);
      } else if (c.kind == HUD_GLYPH) {
        float u0, v0, u1, v1;
        hud_glyph_uv(c.idx, &u0, &v0, &u1, &v1);
        ui_rect(x0, y0, x1, y1, u0, v0, u1, v1, c.argb);
      } else if (c.kind == HUD_DIM) {
        ui_rect(x0, y0, x1, y1, SOLID_U, SOLID_V, SOLID_U, SOLID_V, 0xA0000000u);
      } else {
        ui_rect(x0, y0, x1, y1, SOLID_U, SOLID_V, SOLID_U, SOLID_V, HUD_OPAQUE(c.argb));
      }
    }
    gl.Enable(GL_BLEND);
    gl.BlendEquation(GL_FUNC_ADD);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ui_flush(w, h);
    st->draw_calls++;
  }

  // Clean state for the frontend.
  gl.Disable(GL_BLEND);
  gl.DisableVertexAttribArray(0);
  gl.DisableVertexAttribArray(1);
  gl.DisableVertexAttribArray(2);
  for (int i = 3; i < 6 && i < g_nattribs; ++i) gl.DisableVertexAttribArray((GLuint)i);
  gl.BindBuffer(GL_ARRAY_BUFFER, 0);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindTexture(GL_TEXTURE_2D, 0);
  gl.UseProgram(0);
  const double t2 = plat_now_ms();
  if (finish) gl.Finish();     // waits for the GPU: needed only to measure its time
  const double t3 = plat_now_ms();
  st->ms_geo += t1 - t0;
  st->ms_hud += t2 - t1;
  st->ms_ras += t3 - t2;
}
