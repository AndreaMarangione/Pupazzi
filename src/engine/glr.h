// GPU renderer (OpenGL ES 2) for the hardware context provided by the libretro frontend.
// The GL functions are resolved at run time: the library does not depend on libGLESv2
// and loads even where there is no GPU.
#pragma once
#include "engine/render.h"

typedef void (*glr_proc_t)(void);
typedef glr_proc_t (*glr_get_proc_t)(const char* sym);

bool glr_init(glr_get_proc_t get_proc);   // to be called in context_reset
void glr_context_lost(void);              // to be called in context_destroy: the GL objects no longer exist
void glr_shutdown(void);                  // frees the working memory (when the core is unloaded)
void glr_render(const FrameDesc* f, unsigned fbo, RenderStats* st, bool finish);
const char* glr_info(void);               // "RENDERER | VERSION"
bool glr_aa_available(void);              // the GPU can do antialiasing (FrameDesc::aa requests it)
int glr_aa_max(void);                     // (0.5.4) maximum samples: 0 (none), 2, 4 or 8
int glr_depth_bits(void);
