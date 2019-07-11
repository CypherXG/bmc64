//
// fb2.cpp
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fb2.h"

#include <stdio.h>

#ifndef ALIGN_UP
#define ALIGN_UP(x,y)  ((x + (y)-1) & ~((y)-1))
#endif

// Always use an indexed mode.
#define IMG_TYPE VC_IMAGE_8BPP
#define BYTES_PER_PIXEL 1

#define RGB565(r,g,b) (((r)>>3)<<11 | ((g)>>2)<<5 | (b)>>3)
#define ARGB(a,r,g,b) ((uint32_t)((uint8_t)(a)<<24 | (uint8_t)(r)<<16 | (uint8_t)(g)<<8 | (uint8_t)(b)))

// Palette used for canvases with no transparency
static uint16_t pal_565[256] = {
  RGB565(0x00, 0x00, 0x00),
  RGB565(0xFF, 0xFF, 0xFF),
  RGB565(0x68, 0x37, 0x2b),
  RGB565(0x70, 0xa4, 0xb2),
  RGB565(0x6f, 0x3d, 0x86),
  RGB565(0x58, 0x8d, 0x43),
  RGB565(0x35, 0x28, 0x79),
  RGB565(0xb8, 0xc7, 0x6f),
  RGB565(0x6f, 0x4f, 0x25),
  RGB565(0x43, 0x39, 0x00),
  RGB565(0x9a, 0x67, 0x59),
  RGB565(0x44, 0x44, 0x44),
  RGB565(0x6c, 0x6c, 0x6c),
  RGB565(0x9a, 0xd2, 0x84),
  RGB565(0x6c, 0x5e, 0xb5),
  RGB565(0x95, 0x95, 0x95),
};

// Palette used for canvases with transparency. This palette
// is identical to pal_565 except we include an additional
// entry for a fully transparent pixel (index 16).
static uint32_t pal_argb[256] = {
  // First 16 colors are opaque
  ARGB(0xFF, 0x00, 0x00, 0x00),
  ARGB(0xFF, 0xFF, 0xFF, 0xFF),
  ARGB(0xFF, 0x68, 0x37, 0x2b),
  ARGB(0xFF, 0x70, 0xa4, 0xb2),
  ARGB(0xFF, 0x6f, 0x3d, 0x86),
  ARGB(0xFF, 0x58, 0x8d, 0x43),
  ARGB(0xFF, 0x35, 0x28, 0x79),
  ARGB(0xFF, 0xb8, 0xc7, 0x6f),
  ARGB(0xFF, 0x6f, 0x4f, 0x25),
  ARGB(0xFF, 0x43, 0x39, 0x00),
  ARGB(0xFF, 0x9a, 0x67, 0x59),
  ARGB(0xFF, 0x44, 0x44, 0x44),
  ARGB(0xFF, 0x6c, 0x6c, 0x6c),
  ARGB(0xFF, 0x9a, 0xd2, 0x84),
  ARGB(0xFF, 0x6c, 0x5e, 0xb5),
  ARGB(0xFF, 0x95, 0x95, 0x95),

  // Use index 16 for fully transparent pixels
  ARGB(0x00, 0x00, 0x00, 0x00),
};

bool FrameBuffer2::initialized_ = false;
DISPMANX_DISPLAY_HANDLE_T FrameBuffer2::dispman_display_;

FrameBuffer2::FrameBuffer2() :
        width_(0), height_(0), pitch_(0), layer_(0), transparency_(false),
        showing_(false), allocated_(false) {
  alpha_.flags = DISPMANX_FLAGS_ALPHA_FROM_SOURCE;
  alpha_.opacity = 255;
  alpha_.mask = 0;
}

// static
void FrameBuffer2::Initialize() {
  if (initialized_)
     return;

  bcm_host_init();

  dispman_display_ = vc_dispmanx_display_open(0);

  initialized_ = true;
}

int FrameBuffer2::Allocate(uint8_t **pixels, int width, int height, int *pitch) {
  int ret;
  DISPMANX_MODEINFO_T dispman_info;
  uint32_t vc_image_ptr;

  if (allocated_) {
     Free();
  }

  allocated_ = true;

  *pitch = pitch_ = ALIGN_UP(width, 32);

  width_ = width;
  height_ = height;

  ret = vc_dispmanx_display_get_info(dispman_display_, &dispman_info);
  assert(ret == 0);

  display_width_ = dispman_info.width;
  display_height_ = dispman_info.height;

  *pixels = pixels_ = (uint8_t*) malloc(pitch_ * height * BYTES_PER_PIXEL);

  // Allocate the VC resources along with the frame buffer

  dispman_resource_ = vc_dispmanx_resource_create(IMG_TYPE,
                                                  width,
                                                  height,
                                                  &vc_image_ptr );
  assert(dispman_resource_);

  // Install the default palette
  UpdatePalette();

  vc_dispmanx_rect_set( &copy_dst_rect_, 0, 0, width, height);

  ret = vc_dispmanx_resource_write_data(dispman_resource_,
                                        IMG_TYPE,
                                        width,
                                        pixels_,
                                        &copy_dst_rect_ );
  assert(ret == 0);

  // Define source rect
  vc_dispmanx_rect_set(&src_rect_, 0, 0, width << 16, height << 16 );


  return 0;
}

void FrameBuffer2::Clear() {
  assert (allocated_);

  memset(pixels_, 0, height_ * pitch_);
}

void FrameBuffer2::Free() {
  int ret;

  if (!allocated_) return;

  width_ = 0;
  height_ = 0;
  pitch_ = 0;
  free(pixels_);

  ret = vc_dispmanx_resource_delete(dispman_resource_);
  assert(ret == 0);

  allocated_ = false;
}

FrameBuffer2::~FrameBuffer2() {
  if (showing_) {
    Hide();
  }
  if (allocated_) {
    Free();
  }
}

void FrameBuffer2::Show() {
  int ret;
  DISPMANX_UPDATE_HANDLE_T dispman_update;

  if (showing_) return;

  dispman_update = vc_dispmanx_update_start(0);
  assert( dispman_update );

  // Stretch to full width/height of our display
  vc_dispmanx_rect_set(&scale_dst_rect_, 0,
                                   0,
                                   display_width_,
                                   display_height_);

  dispman_element_ = vc_dispmanx_element_add(dispman_update,
                                            dispman_display_,
                                            layer_, // layer
                                            &scale_dst_rect_,
                                            dispman_resource_,
                                            &src_rect_,
                                            DISPMANX_PROTECTION_NONE,
                                            &alpha_,
                                            NULL,             // clamp
                                            DISPMANX_NO_ROTATE);

  ret = vc_dispmanx_update_submit_sync(dispman_update);
  assert( ret == 0 );
  showing_ = true;
}

void FrameBuffer2::Hide() {
  int ret;
  DISPMANX_UPDATE_HANDLE_T dispman_update;

  if (!showing_) return;
  dispman_update = vc_dispmanx_update_start(0);
  ret = vc_dispmanx_element_remove(dispman_update, dispman_element_);
  assert(ret == 0);
  ret = vc_dispmanx_update_submit_sync(dispman_update);
  assert(ret == 0);
  showing_ = false;
}

void* FrameBuffer2::GetPixels() {
  return pixels_;
}

void FrameBuffer2::FrameReady() {
  int ret;

  ret = vc_dispmanx_resource_write_data(dispman_resource_,
                                        IMG_TYPE,
                                        width_,
                                        pixels_,
                                        &copy_dst_rect_);
  assert(ret == 0);
}

void FrameBuffer2::SetPalette(uint8_t index, uint16_t rgb565) {
  assert(!transparency_);
  pal_565[index] = rgb565;
}

void FrameBuffer2::SetPalette(uint8_t index, uint32_t argb) {
  assert(transparency_);
  pal_argb[index] = argb;
}

void FrameBuffer2::UpdatePalette() {
  if (!allocated_) return;

  int ret;
  if (transparency_) {
     ret = vc_dispmanx_resource_set_palette(dispman_resource_,
                                            pal_argb, 0, sizeof pal_argb);
  } else {
     ret = vc_dispmanx_resource_set_palette(dispman_resource_,
                                            pal_565, 0, sizeof pal_565);
  }
  assert( ret == 0 );
}

void FrameBuffer2::SetLayer(int layer) {
  layer_ = layer;
}

void FrameBuffer2::SetTransparency(bool transparency) {
  transparency_ = transparency;
}
