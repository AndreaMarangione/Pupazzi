// Renderer GPU (OpenGL ES 2) per il contesto hardware fornito dal frontend libretro.
// Le funzioni GL vengono risolte a tempo di esecuzione: la libreria non dipende da libGLESv2
// e si carica anche dove la GPU non c'e'.
#pragma once
#include "render.h"

typedef void (*glr_proc_t)(void);
typedef glr_proc_t (*glr_get_proc_t)(const char* sym);

bool glr_init(glr_get_proc_t get_proc);   // da chiamare in context_reset
void glr_context_lost(void);              // da chiamare in context_destroy: gli oggetti GL non esistono piu'
void glr_shutdown(void);                  // libera la memoria di lavoro (allo scaricamento del core)
void glr_render(const FrameDesc* f, unsigned fbo, RenderStats* st, bool finish);
const char* glr_info(void);               // "RENDERER | VERSION"
bool glr_aa_available(void);              // la GPU sa fare l'antialias (FrameDesc::aa lo chiede)
int glr_aa_max(void);                     // (0.5.4) campioni al massimo: 0 (niente), 2, 4 o 8
int glr_depth_bits(void);
