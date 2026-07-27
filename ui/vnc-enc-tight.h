/*
 * QEMU VNC display driver: tight encoding
 *
 * From libvncserver/rfb/rfbproto.h
 * Copyright (C) 2005 Rohit Kumar, Johannes E. Schindelin
 * Copyright (C) 2000-2002 Constantin Kaplinsky.  All Rights Reserved.
 * Copyright (C) 2000 Tridia Corporation.  All Rights Reserved.
 * Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef VNC_ENC_TIGHT_H
#define VNC_ENC_TIGHT_H

#define VNC_TIGHT_EXPLICIT_FILTER       0x04
#define VNC_TIGHT_FILL                  0x08
#define VNC_TIGHT_JPEG                  0x09
#define VNC_TIGHT_PNG                   0x0A
#define VNC_TIGHT_MAX_SUBENCODING       0x0A

#define VNC_TIGHT_FILTER_COPY             0x00
#define VNC_TIGHT_FILTER_PALETTE          0x01
#define VNC_TIGHT_FILTER_GRADIENT         0x02

#define VNC_TIGHT_MIN_TO_COMPRESS 12

#define VNC_TIGHT_MIN_SPLIT_RECT_SIZE     4096
#define VNC_TIGHT_MIN_SOLID_SUBRECT_SIZE  2048
#define VNC_TIGHT_MAX_SPLIT_TILE_SIZE       16

#define VNC_TIGHT_JPEG_MIN_RECT_SIZE      4096
#define VNC_TIGHT_DETECT_SUBROW_WIDTH        7
#define VNC_TIGHT_DETECT_MIN_WIDTH           8
#define VNC_TIGHT_DETECT_MIN_HEIGHT          8

#endif
