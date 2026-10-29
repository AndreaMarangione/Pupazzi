// Renderer GPU, OpenGL ES 2.
//
// Stesso contenuto del renderer software: la geometria fissa viene caricata una volta in un
// buffer statico, quella che si muove viene trasformata e illuminata sulla CPU e mandata alla
// GPU in un unico buffer per fotogramma (una sola chiamata di disegno: su queste GPU contano
// molto di piu' le chiamate che i triangoli).
//
// Il contesto GL e' quello del frontend, quindi alla fine di ogni fotogramma lo stato viene
// rimesso come RetroArch se lo aspetta.
#include "glr.h"
#include "plat.h"
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

// Funzioni per l'antialias, che non tutte le GPU hanno: se mancano si disegna senza.
//  - EXT_multisampled_render_to_texture (Mali e altre GPU a tessere): i campioni restano nella
//    memoria della tessera e si riducono gratis scrivendo la texture; poi una copia a schermo;
//  - OpenGL ES 3: renderbuffer multicampione e glBlitFramebuffer (Mesa, per le prove sul PC).
struct GLOpt {
  void (*FramebufferTexture2DMultisampleEXT)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
  void (*RenderbufferStorageMultisampleEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
  void (*RenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
  void (*BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
  void (*DiscardFramebufferEXT)(GLenum, GLsizei, const GLenum*);
};

enum { AA_NONE = 0, AA_EXT, AA_ES3 };
// max: campioni al massimo (da GL_MAX_SAMPLES, fino a 8); samples: quelli del bersaglio di adesso.
struct AAState { int mode, max, samples, w, h; GLuint fbo, tex, color_rb, depth_rb; GLenum depth_fmt; bool discard; };

struct WV { float x, y, z; uint8_t r, g, b, a; };          // vertice del mondo, 16 byte
struct HV { float x, y, u, v; uint8_t r, g, b, a; };       // vertice 2D (cielo, copia dell'antialias), 20 byte
// Vertice dell'interfaccia (0.5.2): posizione, coordinate nell'atlante, punto rispetto al centro della
// forma, tipo e morbidezza, mezze misure, taglio, rientro, inclinazione, spessore dell'anello, colore.
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
const int MAX_STATIC = 160;  // blocchi della mappa, del menu e qualche mappa precedente
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
int g_nattribs = 8;       // quanti array di attributi spegnere all'inizio di ogni fotogramma

const char* VS_WORLD =
  "attribute vec3 a_pos;\n"
  "attribute vec4 a_col;\n"
  "uniform mat4 u_vp;\n"
  "varying lowp vec4 v_col;\n"
  "void main() { v_col = a_col; gl_Position = u_vp * vec4(a_pos, 1.0); }\n";
// Nei fragment shader di GLSL ES i float non hanno una precisione predefinita: la si dichiara
// comunque, anche se ogni variabile ha gia' la sua, perche' alcuni compilatori la pretendono.
const char* FS_WORLD =
  "precision mediump float;\n"
  "varying lowp vec4 v_col;\n"
  "void main() { gl_FragColor = v_col; }\n";
// Coordinate di texture in alta precisione dove c'e': con la media (16 bit su Mali) in un atlante
// largo 1024 si sbaglierebbe fino a un quarto di pixel e i caratteri verrebbero sfocati.
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
// Copia a schermo dell'immagine disegnata con l'antialias.
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
// Interfaccia (0.5.2): caratteri e disegni dall'atlante (tipo 0) oppure forme calcolate per pixel,
// con la stessa matematica di hud_shape_sdf e hud_shape_cover (hud.cpp). Distanze in pixel
// d'uscita: servono in alta precisione (con 16 bit a 300 pixel dal centro si sbaglia di un quarto).
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

// Cerca una funzione GL: prima nella libreria GLES gia' caricata dal frontend (lo stesso modo
// in cui la trovano i core che si collegano direttamente a libGLESv2), poi tra i simboli
// globali, infine chiedendola al frontend.
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

// Trasforma e illumina sulla CPU un oggetto; scarta le facce rivolte altrove se cull = true.
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

// Geometria fissa: caricata una volta e riconosciuta dall'identificativo della mesh. Se la
// cache e' piena (per esempio dopo qualche cambio di mappa) si butta la meno usata di recente.
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
    if (g_static[slot].used == g_frame_no) return 0;   // tutte in uso in questo fotogramma
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

// Rettangolo in pixel (origine in alto a sinistra), con colore diverso sopra e sotto.
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

float SOLID_U, SOLID_V;   // un punto del blocco pieno dell'atlante (rettangoli a tinta unita)

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

// --- interfaccia ------------------------------------------------------------------------------

inline void uv_reserve(int extra) {
  if (g_uv_n + extra <= g_uv_cap) return;
  g_uv_cap = (g_uv_n + extra) * 2 + 1024;
  g_uv = (UV*)realloc(g_uv, sizeof(UV) * (size_t)g_uv_cap);
}

// Quadrilatero dell'interfaccia: i quattro angoli (in alto a sinistra, in basso a sinistra, in basso
// a destra, in alto a destra) con le loro coordinate e i loro colori; il resto uguale per tutti.
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

// Colore della sfumatura nel punto (px, py) d'uscita: la stessa retta di hud.cpp, prolungata
// fuori dalla forma (agli angoli del riquadro) e poi limitata canale per canale.
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

// Un pezzo rettangolare della forma (in pixel d'uscita): con la distanza dal bordo (sdf) o, dove
// la forma e' di certo piena, solo il colore (il blocco pieno dell'atlante, come i rettangoli).
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

// Forma dell'interfaccia. I rettangoli grandi (pannelli, velature, aloni) si dividono: la distanza
// dal bordo si calcola solo nelle strisce vicino al bordo e nei due angoli tagliati, l'interno pieno
// e' un rettangolo di colore (gli aloni "cavi" li' non si disegnano proprio). Per lo shader i
// pixel ai bordi sono gli stessi; cambia solo quanto lavora la GPU.
void ui_shape(const HudShape& sh, const HudCmd& c, float s) {
  const float bx0 = (float)c.x * s, by0 = (float)c.y * s, bx1 = (float)(c.x + c.w) * s, by1 = (float)(c.y + c.h) * s;
  if (sh.type == HS_BOX && sh.ring <= 0.0f) {
    // Piena di sicuro dove la distanza e' sotto -m: per i bordi netti mezzo pixel, per gli aloni
    // 2,5 volte la morbidezza (copertura 0,9998).
    const float m = sh.soft > 0.0f ? 2.5f * sh.soft * s : 0.5f;
    const float X = sh.x * s, Y = sh.y * s, W = sh.w * s, H = sh.h * s;
    const float e = sh.inset * s + m;                          // margine dai lati
    const float k = sh.cut > 0.0f ? sh.cut * s + 1.41421356f * e : e;   // margine dagli angoli tagliati
    if (W >= 2.0f * k + 2.0f && H >= 2.0f * k + 2.0f && W > 2.0f * e + 8.0f && H > 2.0f * e + 8.0f) {
      const float xe0 = X + e, xe1 = X + W - e, ye0 = Y + e, ye1 = Y + H - e;
      // Con un taglio piccolo rispetto al margine (k < 2e) i pezzi degli angoli non servono: i limiti
      // si fermano ai lati, cosi' i pezzi restano affiancati senza sovrapporsi.
      const float yk0 = Y + k, yk1 = Y + H - k, xk0 = fmaxf(X + k - e, xe0), xk1 = fminf(X + W - k + e, xe1);
      // Strisce con la distanza: sopra, sotto, a sinistra, a destra, e i due angoli tagliati.
      ui_shape_part(sh, s, bx0, by0, bx1, ye0, true);
      ui_shape_part(sh, s, bx0, ye1, bx1, by1, true);
      ui_shape_part(sh, s, bx0, ye0, xe0, ye1, true);
      ui_shape_part(sh, s, xe1, ye0, bx1, ye1, true);
      ui_shape_part(sh, s, xe0, ye0, xk0, yk0, true);
      ui_shape_part(sh, s, xk1, yk1, xe1, ye1, true);
      // L'interno pieno: la fascia in mezzo e le due accanto agli angoli tagliati.
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

// Interfaccia semplificata, con il programma dell'HUD di prima (solo se manca lo shader nuovo):
// testi, disegni e rettangoli come sempre; le forme diventano rettangoli pieni o contorni squadrati,
// con la sfumatura dall'alto in basso; gli aloni e le ombre morbide non ci sono.
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

// --- antialias ------------------------------------------------------------------------

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

// Prova del percorso EXT dove la GPU non lo ha (PUPAZZI_AA=prova): le stesse chiamate, ma con un
// solo campione. L'immagine deve venire identica a quella senza antialias. PUPAZZI_AA=prova8: come
// se la GPU arrivasse a 8; =prova8no: come se dicesse 8 ma poi lo rifiutasse (si passa a 4).
bool g_fake_reject8;
void fake_fbtex_ms(GLenum target, GLenum att, GLenum textarget, GLuint tex, GLint level, GLsizei samples) {
  if (g_fake_reject8 && samples > 4) return;                 // niente colore: il bersaglio non e' completo
  gl.FramebufferTexture2D(target, att, textarget, tex, level);
}
void fake_rb_ms(GLenum target, GLsizei samples, GLenum fmt, GLsizei w, GLsizei h) {
  if (g_fake_reject8 && samples > 4) return;                 // renderbuffer senza memoria: incompleto
  gl.RenderbufferStorage(target, fmt, w, h);
}

// Sceglie il modo dell'antialias. PUPAZZI_AA=0 lo spegne; =ext o =es3 forzano un modo, =prova
// prova il percorso EXT con un solo campione (prove sul PC).
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
  // Dalla 0.5.4 fino a 8 campioni (il gioco chiede 4 o 8, vedi game_aa_wanted).
  g_aa.max = max_samples >= 8 ? 8 : (max_samples >= 4 ? 4 : (int)max_samples);
  g_aa.depth_fmt = (major >= 3 || has_ext(ext, "GL_OES_depth24")) ? GL_DEPTH_COMPONENT24 : GL_DEPTH_COMPONENT16;
  g_aa.discard = g_aa.mode == AA_EXT && glo.DiscardFramebufferEXT && has_ext(ext, "GL_EXT_discard_framebuffer");
  plat_log("GL: antialias fino a %dx disponibile (%s, profondita' %d bit)", g_aa.max,
           g_aa.mode == AA_EXT ? "EXT_multisampled_render_to_texture" : "OpenGL ES 3, renderbuffer e blit",
           g_aa.depth_fmt == GL_DEPTH_COMPONENT24 ? 24 : 16);
}

// Bersaglio con want campioni per pixel (al massimo g_aa.max) della misura dell'immagine; se la GPU
// lo rifiuta con 8 si riprova con 4, con 4 l'antialias si spegne per tutta la sessione; lo si scrive
// nel registro.
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
    // Informazioni utili per le scelte future (formati, estensioni): solo nel log.
    const char* glsl = (const char*)gl.GetString(GL_SHADING_LANGUAGE_VERSION);
    GLint max_tex = 0;
    gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    plat_log("GL: glsl=%s, texture massima %d", glsl ? glsl : "?", (int)max_tex);
    GLint na = 0;
    gl.GetIntegerv(GL_MAX_VERTEX_ATTRIBS, &na);
    g_nattribs = na < 8 ? 8 : (na > 32 ? 32 : (int)na);
    const char* ext = (const char*)gl.GetString(GL_EXTENSIONS);
    if (ext) {
      // La lista e' lunga: la si spezza in righe da circa 400 caratteri, senza tagliare i nomi.
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
  // Senza lo shader dell'interfaccia (non dovrebbe succedere: se il driver lo rifiutasse) si gioca lo
  // stesso, con l'interfaccia semplificata. PUPAZZI_UI_SEMPLICE=1 la forza, per provarla.
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

  // Atlante dei caratteri e dei disegni dell'interfaccia (solo alfa, bordi sfumati, aloni) con il
  // blocco pieno per i rettangoli. Filtro lineare: a 640x480 i caratteri cadono pixel su pixel e
  // vengono uguali al renderer software; alle altre risoluzioni (solo prove) restano lisci.
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

// Libera la memoria di lavoro: da chiamare quando il core viene scaricato.
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
  // Con l'antialias il mondo si disegna nel bersaglio a piu' campioni, poi lo si porta nel
  // framebuffer del frontend; l'HUD va sempre direttamente in quello del frontend.
  if (g_depth_bits < 0) {
    // Profondita' del framebuffer del frontend (quella che conta senza antialias).
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    GLint bits = 0;
    gl.GetIntegerv(GL_DEPTH_BITS, &bits);
    g_depth_bits = bits;
    plat_log("GL: buffer di profondita' a %d bit", (int)bits);
  }
  const bool aa = f->aa > 0 && aa_ready(w, h, f->aa);
  gl.BindFramebuffer(GL_FRAMEBUFFER, aa ? g_aa.fbo : fbo);
  gl.Viewport(0, 0, w, h);
  // Lo stato ereditato dal frontend (o da un core precedente) non e' garantito: si rimette a
  // posto tutto cio' che influisce sul disegno, non solo quello che RetroArch tocca di solito.
  gl.Disable(GL_SCISSOR_TEST);
  gl.Disable(GL_STENCIL_TEST);
  gl.Disable(GL_BLEND);
  gl.Disable(GL_DEPTH_TEST);
  gl.Disable(GL_CULL_FACE);
  gl.Disable(GL_POLYGON_OFFSET_FILL);
  gl.Disable(GL_SAMPLE_ALPHA_TO_COVERAGE);
  gl.Disable(GL_SAMPLE_COVERAGE);
  // Array di attributi lasciati accesi dagli shader del frontend: qui servono solo 0, 1 e 2,
  // e un array acceso che punta a un buffer piu' corto del disegno e' un rischio inutile.
  for (int i = 0; i < g_nattribs; ++i) gl.DisableVertexAttribArray((GLuint)i);
  gl.ColorMask(1, 1, 1, 1);
  gl.DepthMask(1);
  gl.StencilMask(0xFFFFFFFFu);
  gl.DepthRangef(0.0f, 1.0f);
  gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  gl.ClearDepthf(1.0f);
  gl.ClearStencil(0);
  // Si puliscono tutti i piani: sulle GPU a tessere evita di ricaricare il contenuto precedente.
  gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  // Cielo.
  g_hv_n = 0;
  hud_quad(0, 0, (float)w, (float)h, SOLID_U, SOLID_V, SOLID_U, SOLID_V, f->sky_top, f->sky_bot, 255);
  hud_flush(w, h);

  // Mondo.
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
      const GLenum att = GL_DEPTH_ATTACHMENT;   // la profondita' non serve piu': non la si scrive in memoria
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
      hud_quad(0, 0, (float)w, (float)h, 0.0f, 1.0f, 1.0f, 0.0f, 0xFFFFFF, 0xFFFFFF, 255);   // la texture ha l'origine in basso
      quads_flush(w, h, g_prog_copy, g_u_xf_copy, g_u_tex_copy, g_aa.tex);
      st->draw_calls++;
    }
  }
  const double t1 = plat_now_ms();

  // HUD e menu (0.5.2: un solo disegno con lo shader dell'interfaccia, nell'ordine dei comandi).
  if (f->hud && !g_prog_ui) {
    hud_simple(f->hud, (float)f->hud_scale * 0.5f, w, h);
    st->draw_calls++;
  } else if (f->hud) {
    const float k = (float)f->hud_scale * 0.5f;   // i comandi sono in pixel a 640x480
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

  // Stato pulito per il frontend.
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
  if (finish) gl.Finish();     // aspetta la GPU: serve solo per misurarne il tempo
  const double t3 = plat_now_ms();
  st->ms_geo += t1 - t0;
  st->ms_hud += t2 - t1;
  st->ms_ras += t3 - t2;
}
