/*
Copyright (c) 2018 Raspberry Pi (Trading) Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Authors: John Cox
*/

#ifndef LIBAVCODEC_RPI_ZC_H
#define LIBAVCODEC_RPI_ZC_H

// Zero-Copy frame code for RPi
// RPi needs Y/U/V planes to be contiguous for display.  By default
// ffmpeg will allocate separated planes so a memcpy is needed before
// display.  This code provides a method a making ffmpeg allocate a single
// bit of memory for the frame when can then be reference counted until
// display has finished with it.

// Frame buffer number in which to stuff an 8-bit copy of a 16-bit frame
// 0 disables
// *** This option still in development
//     Only works if SAO active
//     Allocates buffers that are twice the required size
#define RPI_ZC_SAND_8_IN_10_BUF  0

struct AVBufferRef;
struct AVFrame;
struct AVCodecContext;
enum AVPixelFormat;

// "Opaque" pointer to whatever we are using as a buffer reference
typedef struct AVBufferRef * AVRpiZcRefPtr;

struct AVZcEnv;
typedef struct AVZcEnv * AVZcEnvPtr;

typedef struct AVRpiZcFrameGeometry
{
    unsigned int stride_y;  // Luma stride (bytes)
    unsigned int height_y;  // Luma height (lines)
    unsigned int stride_c;  // Chroma stride (bytes)
    unsigned int height_c;  // Chroma stride (lines)
    unsigned int planes_c;  // Chroma plane count (U, V = 2, interleaved = 1)
    unsigned int stripes;   // Number of stripes (sand)
    unsigned int bytes_per_pel;
    int stripe_is_yc;       // A single stripe is Y then C (false for tall sand)
} AVRpiZcFrameGeometry;


AVRpiZcFrameGeometry av_rpi_zc_frame_geometry(
    const int format,
    const unsigned int video_width, const unsigned int video_height);

// Replacement fn for avctx->get_buffer2
// Should be set before calling avcodec_decode_open2
//
// N.B. in addition to to setting avctx->get_buffer2, avctx->refcounted_frames
// must be set to 1 as otherwise the buffer info is killed before being returned
// by avcodec_decode_video2.  Note also that this means that the AVFrame that is
// returned must be manually derefed with av_frame_unref.  This should be done
// after av_rpi_zc_ref has been called.
int av_rpi_zc_get_buffer2(struct AVCodecContext *s, AVFrame *frame, int flags);

// Generate a ZC reference to the buffer(s) in this frame
// If the buffer doesn't appear to be one allocated by _get_buffer_2
// then the behaviour depends on maycopy:
//   If maycopy=0 then return NULL
//   If maycopy=1 && the src frame is in a form where we can easily copy
//     the data, then allocate a new buffer and copy the data into it
//   Otherwise return NULL
AVRpiZcRefPtr av_rpi_zc_ref(struct AVCodecContext * const s,
    const struct AVFrame * const frame, const enum AVPixelFormat expected_format, const int maycopy);

// Get the vc_handle from the frame ref
// Returns -1 if ref doesn't look valid
int av_rpi_zc_vc_handle(const AVRpiZcRefPtr fr_ref);
// Get offset from the start of the memory referenced
// by the vc_handle to valid data
int av_rpi_zc_offset(const AVRpiZcRefPtr fr_ref);
// Length of buffer data
int av_rpi_zc_length(const AVRpiZcRefPtr fr_ref);
// Get the number of bytes allocated from the frame ref
// Returns 0 if ref doesn't look valid
int av_rpi_zc_numbytes(const AVRpiZcRefPtr fr_ref);

// Unreference the buffer refed/allocated by _zc_ref
// If fr_ref is NULL then this will NOP
void av_rpi_zc_unref(AVRpiZcRefPtr fr_ref);

#if 0
// Allocate an environment for the buffer pool used by the ZC code
// This should be put in avctx->get_buffer_context so it can be found by
// av_rpi_zc_get_buffer2 when it is called from ffmpeg
AVZcEnvPtr av_rpi_zc_env_alloc(const int use_cma);

// Allocate the environment used by the ZC code
void av_rpi_zc_env_free(AVZcEnvPtr);
#endif

// Test to see if the context is using zc (checks get_buffer2)
int av_rpi_zc_in_use(const struct AVCodecContext * const s);

// Init ZC into a context
// There is nothing magic in this fn - it just packages setting
// get_buffer2 & get_buffer_context

typedef AVBufferRef * av_rpi_zc_alloc_buf_fn_t(void * pool_env, size_t size);
typedef void av_rpi_zc_free_pool_fn_t(void * pool_env);

int av_rpi_zc_init2(struct AVCodecContext * const s,
                    void * pool_env, av_rpi_zc_alloc_buf_fn_t * alloc_buf_fn,
                    av_rpi_zc_free_pool_fn_t * free_pool_fn);

// Free ZC from a context
// There is nothing magic in this fn - it just packages unsetting
// get_buffer2 & get_buffer_context
void av_rpi_zc_uninit2(struct AVCodecContext * const s);

int av_rpi_zc_init_local(struct AVCodecContext * const s);
void av_rpi_zc_uninit_local(struct AVCodecContext * const s);


typedef struct av_rpi_zc_buf_fn_tab_s {
    void (* free)(void * v);

    unsigned int (* vcsm_handle)(void * v);
    unsigned int (* vc_handle)(void * v);
    void * (* map_arm)(void * v);
    unsigned int (* map_vc)(void * v);
} av_rpi_zc_buf_fn_tab_t;

AVBufferRef * av_rpi_zc_buf(size_t numbytes, int addr_offset, void * v, const av_rpi_zc_buf_fn_tab_t * fn_tab);
void * av_rpi_zc_buf_v(AVBufferRef * const buf);

#endif

