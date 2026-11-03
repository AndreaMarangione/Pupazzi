// Software renderer: transformation, clipping and z-buffer rasterization, on multiple threads.
#pragma once
#include "engine/render.h"

bool swr_init(int max_w, int max_h);
void swr_shutdown(void);
void swr_set_threads(int n);
int  swr_get_threads(void);
int  swr_max_threads(void);

// Draws the frame and returns the XRGB8888 color buffer (w * h pixels, stride w * 4 bytes).
const uint32_t* swr_render(const FrameDesc* f, RenderStats* st);

// Tests only: in count mode each pixel accumulates how many times it is covered.
void swr_debug_count_mode(bool on);
const float* swr_debug_depth(void);
int swr_debug_clipped(void);   // triangles that went through clipping in the last frame
