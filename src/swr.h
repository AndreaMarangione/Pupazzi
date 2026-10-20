// Renderer software: trasformazione, ritaglio e rasterizzazione con z-buffer, su piu' thread.
#pragma once
#include "render.h"

bool swr_init(int max_w, int max_h);
void swr_shutdown(void);
void swr_set_threads(int n);
int  swr_get_threads(void);
int  swr_max_threads(void);

// Disegna il fotogramma e restituisce il buffer colore XRGB8888 (w * h pixel, passo w * 4 byte).
const uint32_t* swr_render(const FrameDesc* f, RenderStats* st);

// Solo per i test: in modalita' conteggio ogni pixel accumula quante volte viene coperto.
void swr_debug_count_mode(bool on);
const float* swr_debug_depth(void);
int swr_debug_clipped(void);   // triangoli passati dal ritaglio nell'ultimo fotogramma
