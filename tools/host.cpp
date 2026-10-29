// Frontend libretro minimo, senza finestra, per provare il core senza console.
//
//   host CORE.so [--frames N] [--dump DIR] [--at 10,60,...] [--in SCRIPT] [--var k=v]
//                [--gl] [--depth16] [--no-proc] [--dirty N] [--hashes] [--quiet] [--wav FILE] [--tempo HZ]
//
// SCRIPT: eventi separati da virgola, "porta:NOME@inizio-fine" per un tasto oppure
// "porta:asse=valore@inizio-fine" per una levetta (assi lx ly rx ry, valore tra -1 e 1).
// Esempio: --in "0:RIGHT@10-60,1:A@30-32,0:rx=0.5@80-120"
//
// Con --gl (solo se compilato con -DHOST_GL) offre al core un contesto OpenGL ES 2 fuori
// schermo e controlla che il core lasci pulito lo stato GL, come si aspetta RetroArch.
// --dirty N sporca lo stato GL prima di ogni fotogramma (vedi gl_dirty): l'immagine non deve cambiare.
// --tempo HZ: HZ fotogrammi al secondo come un frontend (le prove del gioco in rete con piu' copie,
// che devono andare allo stesso passo); senza, il piu' veloce possibile.
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/libretro.h"

#define MAXW 1024
#define MAXH 1024

struct Core {
  void (*init)(void);
  void (*deinit)(void);
  unsigned (*api_version)(void);
  void (*get_system_info)(struct retro_system_info*);
  void (*get_system_av_info)(struct retro_system_av_info*);
  void (*set_environment)(retro_environment_t);
  void (*set_video_refresh)(retro_video_refresh_t);
  void (*set_audio_sample)(retro_audio_sample_t);
  void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
  void (*set_input_poll)(retro_input_poll_t);
  void (*set_input_state)(retro_input_state_t);
  bool (*load_game)(const struct retro_game_info*);
  void (*unload_game)(void);
  void (*run)(void);
};

struct Ev { int port, axis, id; float val; int f0, f1; };

static Core core;
static Ev g_ev[256];
static int g_nev;
static int g_frame;
static bool g_quiet, g_shutdown, g_no_game, g_want_gl, g_depth16, g_no_proc;
static char g_vars[8][2][96];
static int g_nvars;
static uint32_t* g_last;
static unsigned g_last_w, g_last_h;
static long g_audio_frames, g_video_calls, g_dupes;
static struct retro_hw_render_callback g_hw;
static bool g_hw_set;
static unsigned g_geom_w, g_geom_h;

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static void log_printf(enum retro_log_level level, const char* fmt, ...) {
  if (g_quiet && level < RETRO_LOG_WARN) return;
  va_list ap;
  va_start(ap, fmt);
  fputs("  core> ", stdout);
  vfprintf(stdout, fmt, ap);
  va_end(ap);
}

// --- OpenGL ES fuori schermo ---------------------------------------------
#ifdef HOST_GL
#define HGL(X) \
  X(void, glGenFramebuffers, (int, unsigned*)) \
  X(void, glBindFramebuffer, (unsigned, unsigned)) \
  X(void, glGenTextures, (int, unsigned*)) \
  X(void, glBindTexture, (unsigned, unsigned)) \
  X(void, glTexImage2D, (unsigned, int, int, int, int, int, unsigned, unsigned, const void*)) \
  X(void, glTexParameteri, (unsigned, unsigned, int)) \
  X(void, glFramebufferTexture2D, (unsigned, unsigned, unsigned, unsigned, int)) \
  X(void, glGenRenderbuffers, (int, unsigned*)) \
  X(void, glBindRenderbuffer, (unsigned, unsigned)) \
  X(void, glRenderbufferStorage, (unsigned, unsigned, int, int)) \
  X(void, glFramebufferRenderbuffer, (unsigned, unsigned, unsigned, unsigned)) \
  X(unsigned, glCheckFramebufferStatus, (unsigned)) \
  X(void, glReadPixels, (int, int, int, int, unsigned, unsigned, void*)) \
  X(const unsigned char*, glGetString, (unsigned)) \
  X(unsigned, glGetError, (void)) \
  X(void, glGetIntegerv, (unsigned, int*)) \
  X(unsigned char, glIsEnabled, (unsigned)) \
  X(void, glGetVertexAttribiv, (unsigned, unsigned, int*)) \
  X(void, glGetBooleanv, (unsigned, unsigned char*)) \
  X(void, glViewport, (int, int, int, int)) \
  X(void, glUseProgram, (unsigned)) \
  X(void, glEnable, (unsigned)) \
  X(void, glDisable, (unsigned)) \
  X(void, glBlendFunc, (unsigned, unsigned)) \
  X(void, glBlendEquation, (unsigned)) \
  X(void, glScissor, (int, int, int, int)) \
  X(void, glDepthFunc, (unsigned)) \
  X(void, glDepthMask, (unsigned char)) \
  X(void, glColorMask, (unsigned char, unsigned char, unsigned char, unsigned char)) \
  X(void, glCullFace, (unsigned)) \
  X(void, glFrontFace, (unsigned)) \
  X(void, glStencilFunc, (unsigned, int, unsigned)) \
  X(void, glStencilMask, (unsigned)) \
  X(void, glActiveTexture, (unsigned)) \
  X(void, glPolygonOffset, (float, float)) \
  X(void, glDepthRangef, (float, float)) \
  X(void, glClearColor, (float, float, float, float)) \
  X(void, glClearDepthf, (float)) \
  X(void, glClearStencil, (int)) \
  X(void, glPixelStorei, (unsigned, int)) \
  X(void, glGenBuffers, (int, unsigned*)) \
  X(void, glBindBuffer, (unsigned, unsigned)) \
  X(void, glBufferData, (unsigned, long, const void*, unsigned)) \
  X(void, glEnableVertexAttribArray, (unsigned)) \
  X(void, glVertexAttribPointer, (unsigned, int, unsigned, unsigned char, int, const void*)) \
  X(void, glSampleCoverage, (float, unsigned char))
#define X(ret, name, args) static ret (*h_##name) args;
HGL(X)
#undef X
static void* (*h_eglGetProcAddress)(const char*);
static unsigned g_fbo, g_fbo_tex, g_fbo_rb;
static uint8_t* g_rgba;
static bool g_leak_reported;

static bool gl_setup(unsigned side) {
  void* egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
  if (!egl) { printf("host: libEGL non trovata: %s\n", dlerror()); return false; }
  dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
  typedef void* (*get_platform_display_t)(unsigned, void*, const intptr_t*);
  get_platform_display_t get_display = (get_platform_display_t)dlsym(egl, "eglGetPlatformDisplay");
  unsigned (*initialize)(void*, int*, int*) = (unsigned (*)(void*, int*, int*))dlsym(egl, "eglInitialize");
  unsigned (*bind_api)(unsigned) = (unsigned (*)(unsigned))dlsym(egl, "eglBindAPI");
  unsigned (*choose)(void*, const int*, void**, int, int*) = (unsigned (*)(void*, const int*, void**, int, int*))dlsym(egl, "eglChooseConfig");
  void* (*create_pbuffer)(void*, void*, const int*) = (void* (*)(void*, void*, const int*))dlsym(egl, "eglCreatePbufferSurface");
  void* (*create_context)(void*, void*, void*, const int*) = (void* (*)(void*, void*, void*, const int*))dlsym(egl, "eglCreateContext");
  unsigned (*make_current)(void*, void*, void*, void*) = (unsigned (*)(void*, void*, void*, void*))dlsym(egl, "eglMakeCurrent");
  int (*get_error)(void) = (int (*)(void))dlsym(egl, "eglGetError");
  h_eglGetProcAddress = (void* (*)(const char*))dlsym(egl, "eglGetProcAddress");
  if (!get_display || !initialize || !bind_api || !choose || !create_pbuffer || !create_context || !make_current || !h_eglGetProcAddress) {
    printf("host: funzioni EGL mancanti\n");
    return false;
  }
  void* dpy = get_display(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, NULL, NULL);
  int major = 0, minor = 0;
  if (!dpy || !initialize(dpy, &major, &minor)) { printf("host: eglInitialize fallita (0x%x)\n", get_error ? get_error() : 0); return false; }
  bind_api(0x30A0 /* EGL_OPENGL_ES_API */);
  const int cattr[] = { 0x3033, 0x0001, 0x3040, 0x0004, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3038 };
  void* cfg = NULL;
  int n = 0;
  if (!choose(dpy, cattr, &cfg, 1, &n) || n < 1) { printf("host: nessuna configurazione EGL\n"); return false; }
  const int pattr[] = { 0x3057, 16, 0x3056, 16, 0x3038 };
  void* surf = create_pbuffer(dpy, cfg, pattr);
  const int xattr[] = { 0x3098, 2, 0x3038 };
  void* ctx = create_context(dpy, cfg, NULL, xattr);
  if (!ctx || !make_current(dpy, surf, surf, ctx)) { printf("host: contesto GLES2 non creato (0x%x)\n", get_error ? get_error() : 0); return false; }
#define X(ret, name, args) \
  h_##name = (ret (*) args)h_eglGetProcAddress(#name); \
  if (!h_##name) { printf("host: manca %s\n", #name); return false; }
  HGL(X)
#undef X
  printf("host: EGL %d.%d, %s, %s\n", major, minor, (const char*)h_glGetString(0x1F01), (const char*)h_glGetString(0x1F02));
  // Stessa struttura dell'FBO di RetroArch: quadrato, lato potenza di due, RGBA8, profondita' e stencil.
  h_glGenTextures(1, &g_fbo_tex);
  h_glBindTexture(0x0DE1, g_fbo_tex);
  h_glTexImage2D(0x0DE1, 0, 0x1908, (int)side, (int)side, 0, 0x1908, 0x1401, NULL);
  h_glTexParameteri(0x0DE1, 0x2801, 0x2600);
  h_glTexParameteri(0x0DE1, 0x2800, 0x2600);
  h_glTexParameteri(0x0DE1, 0x2802, 0x812F);
  h_glTexParameteri(0x0DE1, 0x2803, 0x812F);
  h_glBindTexture(0x0DE1, 0);
  h_glGenRenderbuffers(1, &g_fbo_rb);
  h_glBindRenderbuffer(0x8D41, g_fbo_rb);
  h_glRenderbufferStorage(0x8D41, g_depth16 ? 0x81A5 : 0x88F0, (int)side, (int)side);
  h_glGenFramebuffers(1, &g_fbo);
  h_glBindFramebuffer(0x8D40, g_fbo);
  h_glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, g_fbo_tex, 0);
  h_glFramebufferRenderbuffer(0x8D40, 0x8D00, 0x8D41, g_fbo_rb);
  if (!g_depth16) h_glFramebufferRenderbuffer(0x8D40, 0x8D20, 0x8D41, g_fbo_rb);
  const unsigned status = h_glCheckFramebufferStatus(0x8D40);
  if (status != 0x8CD5) { printf("host: FBO incompleto 0x%x\n", status); return false; }
  h_glBindFramebuffer(0x8D40, 0);
  g_rgba = (uint8_t*)malloc((size_t)side * side * 4);
  return true;
}

// --dirty N: prima di ogni fotogramma lascia lo stato GL "sporco", come potrebbe lasciarlo il
// frontend o un core precedente. Il core deve disegnare la stessa immagine comunque.
// N e' una maschera di bit: 1 lo stato che RetroArch tocca davvero, 2 array di attributi accesi
// che il core non usa, 4 equazione di fusione, 8 scostamento dei poligoni, 16 copertura dei campioni.
static int g_dirty;
static unsigned g_dvbo, g_dtex;
static void gl_dirty(void) {
  if (!g_dirty) return;
  if (!g_dvbo) {
    float z[16] = { 0 };
    h_glGenBuffers(1, &g_dvbo);
    h_glBindBuffer(0x8892, g_dvbo);
    h_glBufferData(0x8892, sizeof(z), z, 0x88E4);
    h_glBindBuffer(0x8892, 0);
    h_glGenTextures(1, &g_dtex);
  }
  if (g_dirty & 1) {
    h_glEnable(0x0BE2); h_glBlendFunc(1, 1);                           // fusione ONE/ONE
    h_glEnable(0x0C11); h_glScissor(5, 5, 20, 20);                     // forbice
    h_glEnable(0x0B71); h_glDepthFunc(0x0200); h_glDepthMask(0);       // profondita' NEVER, senza scrittura
    h_glEnable(0x0B44); h_glCullFace(0x0404); h_glFrontFace(0x0900);   // scarto delle facce anteriori, senso orario
    h_glEnable(0x0B90); h_glStencilFunc(0x0200, 0, 0xFF); h_glStencilMask(0);
    h_glColorMask(1, 0, 1, 0);
    h_glDisable(0x0BD0);                                               // dithering spento
    h_glActiveTexture(0x84C3); h_glBindTexture(0x0DE1, g_dtex);        // unita' 3 attiva
    h_glClearColor(1, 0, 1, 0); h_glClearDepthf(0.0f); h_glClearStencil(7);
    h_glDepthRangef(0.3f, 0.6f);
    h_glPixelStorei(0x0CF5, 1);
    h_glViewport(3, 3, 9, 9);
    h_glBindBuffer(0x8892, g_dvbo);
    for (unsigned i = 0; i < 3; ++i) { h_glEnableVertexAttribArray(i); h_glVertexAttribPointer(i, 2, 0x1406, 0, 0, 0); }
    h_glBindBuffer(0x8892, 0);
  }
  if (g_dirty & 2) {
    h_glBindBuffer(0x8892, g_dvbo);
    for (unsigned i = 3; i < 8; ++i) { h_glEnableVertexAttribArray(i); h_glVertexAttribPointer(i, 2, 0x1406, 0, 0, 0); }
    h_glBindBuffer(0x8892, 0);
  }
  if (g_dirty & 4) h_glBlendEquation(0x800A);                          // sottrazione
  if (g_dirty & 8) { h_glEnable(0x8037); h_glPolygonOffset(50.0f, 5000.0f); }
  if (g_dirty & 16) { h_glEnable(0x80A0); h_glSampleCoverage(0.0f, 0); h_glEnable(0x809E); }
}

static uintptr_t hw_get_fbo(void) { return g_fbo; }
static retro_proc_address_t hw_get_proc(const char* sym) {
  if (g_no_proc) return NULL;
  return (retro_proc_address_t)h_eglGetProcAddress(sym);
}

// Dopo retro_run lo stato deve essere quello che RetroArch non ripristina da solo.
static void gl_leak_check(void) {
  if (g_leak_reported) return;
  int v = 0;
  char msg[512];
  msg[0] = 0;
  h_glGetIntegerv(0x8894, &v); if (v) strcat(msg, " ARRAY_BUFFER legato;");
  h_glGetIntegerv(0x84E0, &v); if (v != 0x84C0) strcat(msg, " texture attiva diversa da 0;");
  if (h_glIsEnabled(0x0C11)) strcat(msg, " SCISSOR_TEST acceso;");
  unsigned char cm[4] = { 0, 0, 0, 0 };
  h_glGetBooleanv(0x0C23, cm);
  if (!(cm[0] && cm[1] && cm[2] && cm[3])) strcat(msg, " maschera colore non piena;");
  for (unsigned i = 0; i < 8; ++i) {
    h_glGetVertexAttribiv(i, 0x8622, &v);
    if (v) { strcat(msg, " attributi di vertice ancora accesi;"); break; }
  }
  const unsigned err = h_glGetError();
  if (err) { char e[48]; snprintf(e, sizeof(e), " errore GL 0x%04x;", err); strcat(msg, e); }
  if (msg[0]) { printf("host: STATO GL SPORCO al fotogramma %d:%s\n", g_frame, msg); g_leak_reported = true; }
}
#endif

// --- ambiente -------------------------------------------------------------

static bool environment(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback*)data)->log = log_printf;
      return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
      g_no_game = *(bool*)data;
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return *(enum retro_pixel_format*)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable* v = (struct retro_variable*)data;
      for (int i = 0; i < g_nvars; ++i)
        if (!strcmp(v->key, g_vars[i][0])) { v->value = g_vars[i][1]; return true; }
      return false;
    }
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
      const struct retro_game_geometry* g = (const struct retro_game_geometry*)data;
      g_geom_w = g->base_width;
      g_geom_h = g->base_height;
      return true;
    }
    case RETRO_ENVIRONMENT_SHUTDOWN:
      g_shutdown = true;
      return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
#ifdef HOST_GL
      if (!g_want_gl) return false;
      struct retro_hw_render_callback* cb = (struct retro_hw_render_callback*)data;
      if (cb->context_type != RETRO_HW_CONTEXT_OPENGLES2 && cb->context_type != RETRO_HW_CONTEXT_OPENGLES3) return false;
      cb->get_current_framebuffer = hw_get_fbo;
      cb->get_proc_address = hw_get_proc;
      g_hw = *cb;
      g_hw_set = true;
      return true;
#else
      return false;
#endif
    }
    default:
      return false;
  }
}

static void store_frame(const uint32_t* src, unsigned w, unsigned h, size_t pitch) {
  for (unsigned y = 0; y < h; ++y)
    memcpy(g_last + (size_t)y * w, (const uint8_t*)src + (size_t)y * pitch, (size_t)w * 4);
  g_last_w = w;
  g_last_h = h;
}

static void video_refresh(const void* data, unsigned w, unsigned h, size_t pitch) {
  g_video_calls++;
  if (!data) { g_dupes++; return; }
  if (w > MAXW || h > MAXH) { printf("host: fotogramma troppo grande %ux%u\n", w, h); return; }
#ifdef HOST_GL
  if (data == RETRO_HW_FRAME_BUFFER_VALID) {
    h_glBindFramebuffer(0x8D40, g_fbo);
    h_glReadPixels(0, 0, (int)w, (int)h, 0x1908, 0x1401, g_rgba);
    h_glBindFramebuffer(0x8D40, 0);
    for (unsigned y = 0; y < h; ++y) {
      const uint8_t* s = g_rgba + (size_t)(h - 1 - y) * w * 4;      // origine in basso a sinistra
      uint32_t* d = g_last + (size_t)y * w;
      for (unsigned x = 0; x < w; ++x) d[x] = 0xFF000000u | ((uint32_t)s[x * 4] << 16) | ((uint32_t)s[x * 4 + 1] << 8) | s[x * 4 + 2];
    }
    g_last_w = w;
    g_last_h = h;
    return;
  }
#endif
  store_frame((const uint32_t*)data, w, h, pitch);
}

// --wav FILE: il sonoro del core in un file WAV (48 kHz, stereo, 16 bit) per misurarlo.
static FILE* g_wav;
static long g_wav_frames;
static void wav_header(FILE* f, long frames) {
  const unsigned data = (unsigned)frames * 4u, rate = 48000u;
  unsigned char h[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                          0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0 };
  const unsigned riff = 36u + data, byterate = rate * 4u;
  for (int k = 0; k < 4; ++k) {
    h[4 + k] = (unsigned char)(riff >> (8 * k));
    h[24 + k] = (unsigned char)(rate >> (8 * k));
    h[28 + k] = (unsigned char)(byterate >> (8 * k));
    h[40 + k] = (unsigned char)(data >> (8 * k));
  }
  fseek(f, 0, SEEK_SET);
  fwrite(h, 1, sizeof(h), f);
  fseek(f, 0, SEEK_END);
}
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; g_audio_frames++; }
static size_t audio_batch(const int16_t* data, size_t frames) {
  g_audio_frames += (long)frames;
  if (g_wav) {
    fwrite(data, 4, frames, g_wav);
    g_wav_frames += (long)frames;
  }
  return frames;
}
static void input_poll(void) {}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (device == RETRO_DEVICE_JOYPAD) {
    unsigned mask = 0;
    for (int i = 0; i < g_nev; ++i) {
      const Ev& e = g_ev[i];
      if (e.axis < 0 && e.port == (int)port && g_frame >= e.f0 && g_frame <= e.f1) mask |= 1u << e.id;
    }
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)mask;
    return (int16_t)((mask >> id) & 1u);
  }
  if (device == RETRO_DEVICE_ANALOG) {
    const int a = (int)(index * 2 + id);
    float v = 0.0f;
    for (int i = 0; i < g_nev; ++i) {
      const Ev& e = g_ev[i];
      if (e.axis == a && e.port == (int)port && g_frame >= e.f0 && g_frame <= e.f1) v = e.val;
    }
    return (int16_t)(v * 32767.0f);
  }
  return 0;
}

static bool parse_script(const char* s) {
  static const char* const BTN[16] = { "B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R", "L2", "R2", "L3", "R3" };
  static const char* const AX[4] = { "lx", "ly", "rx", "ry" };
  char buf[4096];
  snprintf(buf, sizeof(buf), "%s", s);
  for (char* tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
    Ev e;
    char name[32];
    e.val = 0.0f;
    e.axis = -1;
    e.id = -1;
    int n = 0;
    if (sscanf(tok, "%d:%31[A-Za-z0-9]%n", &e.port, name, &n) < 2) { printf("host: evento non valido '%s'\n", tok); return false; }
    const char* rest = tok + n;
    if (*rest == '=') {
      for (int i = 0; i < 4; ++i)
        if (!strcmp(name, AX[i])) e.axis = i;
      int m = 0;
      if (e.axis < 0 || sscanf(rest, "=%f%n", &e.val, &m) < 1) { printf("host: asse non valido '%s'\n", tok); return false; }
      rest += m;
    } else {
      for (int i = 0; i < 16; ++i)
        if (!strcmp(name, BTN[i])) e.id = i;
      if (e.id < 0) { printf("host: tasto non valido '%s'\n", tok); return false; }
    }
    if (sscanf(rest, "@%d-%d", &e.f0, &e.f1) < 2) { printf("host: intervallo non valido '%s'\n", tok); return false; }
    if (g_nev < 256) g_ev[g_nev++] = e;
  }
  return true;
}

static uint64_t fnv(const uint32_t* px, unsigned w, unsigned h) {
  uint64_t x = 1469598103934665603ull;
  x = (x ^ w) * 1099511628211ull;
  x = (x ^ h) * 1099511628211ull;
  for (size_t i = 0; i < (size_t)w * h; ++i) x = (x ^ (px[i] & 0xFFFFFFu)) * 1099511628211ull;
  return x;
}

static void dump_ppm(const char* dir, int frame) {
  char path[512];
  snprintf(path, sizeof(path), "%s/f%05d.ppm", dir, frame);
  FILE* f = fopen(path, "wb");
  if (!f) { printf("host: non riesco a scrivere %s\n", path); return; }
  fprintf(f, "P6\n%u %u\n255\n", g_last_w, g_last_h);
  for (size_t i = 0; i < (size_t)g_last_w * g_last_h; ++i) {
    const uint32_t p = g_last[i];
    const uint8_t rgb[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

template <class T> static bool sym(void* lib, const char* name, T* out) {
  *out = (T)dlsym(lib, name);
  if (!*out) printf("host: simbolo mancante %s\n", name);
  return *out != 0;
}

int main(int argc, char** argv) {
  if (argc < 2) { printf("uso: host CORE.so [opzioni]\n"); return 2; }
  const char* path = argv[1];
  int frames = 60;
  const char* dump = NULL;
  int at[64], nat = 0;
  bool hashes = false;
  double tempo = 0.0;
  for (int i = 2; i < argc; ++i) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
    else if (!strcmp(argv[i], "--at") && i + 1 < argc) {
      char buf[512];
      snprintf(buf, sizeof(buf), "%s", argv[++i]);
      for (char* t = strtok(buf, ","); t && nat < 64; t = strtok(NULL, ",")) at[nat++] = atoi(t);
    }
    else if (!strcmp(argv[i], "--in") && i + 1 < argc) { if (!parse_script(argv[++i])) return 2; }
    else if (!strcmp(argv[i], "--var") && i + 1 < argc) {
      const char* kv = argv[++i];
      const char* eq = strchr(kv, '=');
      if (eq && g_nvars < 8) {
        snprintf(g_vars[g_nvars][0], 96, "%.*s", (int)(eq - kv), kv);
        snprintf(g_vars[g_nvars][1], 96, "%s", eq + 1);
        g_nvars++;
      }
    }
    else if (!strcmp(argv[i], "--gl")) g_want_gl = true;
    else if (!strcmp(argv[i], "--depth16")) g_depth16 = true;
    else if (!strcmp(argv[i], "--no-proc")) g_no_proc = true;
#ifdef HOST_GL
    else if (!strcmp(argv[i], "--dirty") && i + 1 < argc) g_dirty = atoi(argv[++i]);
#endif
    else if (!strcmp(argv[i], "--hashes")) hashes = true;
    else if (!strcmp(argv[i], "--tempo") && i + 1 < argc) tempo = atof(argv[++i]);
    else if (!strcmp(argv[i], "--quiet")) g_quiet = true;
    else if (!strcmp(argv[i], "--wav") && i + 1 < argc) {
      g_wav = fopen(argv[++i], "wb");
      if (g_wav) wav_header(g_wav, 0);
    }
    else { printf("host: opzione sconosciuta %s\n", argv[i]); return 2; }
  }

  void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib) { printf("host: dlopen fallita: %s\n", dlerror()); return 1; }
  // RetroArch pretende tutti e 25 i simboli dell'interfaccia.
  static const char* const ALL[25] = {
    "retro_init", "retro_deinit", "retro_api_version", "retro_get_system_info", "retro_get_system_av_info",
    "retro_set_environment", "retro_set_video_refresh", "retro_set_audio_sample", "retro_set_audio_sample_batch",
    "retro_set_input_poll", "retro_set_input_state", "retro_set_controller_port_device", "retro_reset", "retro_run",
    "retro_serialize_size", "retro_serialize", "retro_unserialize", "retro_cheat_reset", "retro_cheat_set",
    "retro_load_game", "retro_load_game_special", "retro_unload_game", "retro_get_region", "retro_get_memory_data",
    "retro_get_memory_size" };
  for (int i = 0; i < 25; ++i)
    if (!dlsym(lib, ALL[i])) { printf("host: simbolo mancante %s\n", ALL[i]); return 1; }
  bool ok = sym(lib, "retro_init", &core.init) & sym(lib, "retro_deinit", &core.deinit) &
            sym(lib, "retro_api_version", &core.api_version) & sym(lib, "retro_get_system_info", &core.get_system_info) &
            sym(lib, "retro_get_system_av_info", &core.get_system_av_info) & sym(lib, "retro_set_environment", &core.set_environment) &
            sym(lib, "retro_set_video_refresh", &core.set_video_refresh) & sym(lib, "retro_set_audio_sample", &core.set_audio_sample) &
            sym(lib, "retro_set_audio_sample_batch", &core.set_audio_sample_batch) & sym(lib, "retro_set_input_poll", &core.set_input_poll) &
            sym(lib, "retro_set_input_state", &core.set_input_state) & sym(lib, "retro_load_game", &core.load_game) &
            sym(lib, "retro_unload_game", &core.unload_game) & sym(lib, "retro_run", &core.run);
  if (!ok) return 1;
  if (core.api_version() != RETRO_API_VERSION) { printf("host: versione API %u inattesa\n", core.api_version()); return 1; }

  g_last = (uint32_t*)calloc((size_t)MAXW * MAXH, 4);
  struct retro_system_info si;
  memset(&si, 0, sizeof(si));
  core.get_system_info(&si);
  // Stesso ordine di RetroArch: ambiente, init, load_game, callback, av_info, contesto video.
  core.set_environment(environment);
  core.init();
  if (!g_no_game) { printf("host: il core non dichiara di partire senza contenuto\n"); return 1; }
  if (!core.load_game(NULL)) { printf("host: retro_load_game ha fallito\n"); return 1; }
  core.set_video_refresh(video_refresh);
  core.set_audio_sample(audio_sample);
  core.set_audio_sample_batch(audio_batch);
  core.set_input_poll(input_poll);
  core.set_input_state(input_state);
  struct retro_system_av_info av;
  memset(&av, 0, sizeof(av));
  core.get_system_av_info(&av);
  printf("host: %s %s | %ux%u (max %ux%u) %.2f fps %.0f Hz | renderer hardware: %s\n", si.library_name, si.library_version,
         av.geometry.base_width, av.geometry.base_height, av.geometry.max_width, av.geometry.max_height, av.timing.fps,
         av.timing.sample_rate, g_hw_set ? "si" : "no");
#ifdef HOST_GL
  if (g_hw_set) {
    unsigned side = 256;
    const unsigned need = av.geometry.max_width > av.geometry.max_height ? av.geometry.max_width : av.geometry.max_height;
    while (side < need) side *= 2;
    if (!gl_setup(side)) return 1;
    if (g_hw.context_reset) g_hw.context_reset();
  }
#endif

  double sum = 0.0, worst = 0.0;
  uint64_t all = 1469598103934665603ull;
  int ran = 0;
  const double t_start = now_ms();
  for (g_frame = 0; g_frame < frames && !g_shutdown; ++g_frame) {
    if (tempo > 0.0) {
      // Al passo di un frontend: il fotogramma n non prima di n / HZ secondi dall'inizio.
      const double due = t_start + (double)g_frame * 1000.0 / tempo;
      for (double t = now_ms(); t < due; t = now_ms()) {
        struct timespec ts;
        const double wait = due - t;
        ts.tv_sec = (time_t)(wait / 1000.0);
        ts.tv_nsec = (long)((wait - (double)ts.tv_sec * 1000.0) * 1e6);
        nanosleep(&ts, NULL);
      }
    }
#ifdef HOST_GL
    if (g_hw_set) {
      h_glBindFramebuffer(0x8D40, 0);          // RetroArch entra in retro_run con il framebuffer 0 legato
      h_glViewport(0, 0, 7, 7);
      h_glUseProgram(0);
      gl_dirty();
    }
#endif
    const long calls_before = g_video_calls;
    const double t0 = now_ms();
    core.run();
    const double t = now_ms() - t0;
    if (g_video_calls != calls_before + 1) printf("host: fotogramma %d: %ld chiamate alla callback video invece di una\n", g_frame, g_video_calls - calls_before);
#ifdef HOST_GL
    if (g_hw_set) gl_leak_check();
#endif
    sum += t;
    if (t > worst) worst = t;
    ran++;
    const uint64_t hh = fnv(g_last, g_last_w, g_last_h);
    all = (all ^ hh) * 1099511628211ull;
    if (hashes) printf("frame %d %ux%u %016llx\n", g_frame, g_last_w, g_last_h, (unsigned long long)hh);
    bool want = false;
    for (int i = 0; i < nat; ++i) want |= at[i] == g_frame;
    if (dump && (want || (nat == 0 && g_frame == frames - 1))) dump_ppm(dump, g_frame);
  }
#ifdef HOST_GL
  if (g_hw_set && g_hw.context_destroy) g_hw.context_destroy();
#endif
  core.unload_game();
  core.deinit();
  dlclose(lib);
  printf("host: fotogrammi=%d ultimo=%ux%u hash=%016llx media=%.3fms massimo=%.3fms audio=%ld campioni/fotogramma duplicati=%ld%s\n", ran,
         g_last_w, g_last_h, (unsigned long long)all, ran ? sum / ran : 0.0, worst, ran ? g_audio_frames / ran : 0, g_dupes,
         g_shutdown ? " (chiusura chiesta dal core)" : "");
  if (g_wav) {
    wav_header(g_wav, g_wav_frames);
    fclose(g_wav);
  }
  return 0;
}
