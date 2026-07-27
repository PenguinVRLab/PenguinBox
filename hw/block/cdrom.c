/*
 * QEMU ATAPI CD-ROM Emulator
 *
 * Copyright (c) 2006 Fabrice Bellard
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

/* ??? Most of the ATAPI emulation is still in ide.c.  It should be moved
   here.  */

#include "qemu/osdep.h"
#include "hw/scsi/scsi.h"

static void lba_to_msf(uint8_t *buf, int lba)
{
    lba += 150;
    buf[0] = (lba / 75) / 60;
    buf[1] = (lba / 75) % 60;
    buf[2] = lba % 75;
}

int cdrom_read_toc(int nb_sectors, uint8_t *buf, int msf, int start_track)
{
    uint8_t *q;
    int len;

    if (start_track > 1 && start_track != 0xaa)
        return -1;
    q = buf + 2;
    *q++ = 1;
    *q++ = 1;
    if (start_track <= 1) {
        *q++ = 0;
        *q++ = 0x14;
        *q++ = 1;
        *q++ = 0;
        if (msf) {
            *q++ = 0;
            lba_to_msf(q, 0);
            q += 3;
        } else {
            stl_be_p(q, 0);
            q += 4;
        }
    }
    *q++ = 0;
    *q++ = 0x16;
    *q++ = 0xaa;
    *q++ = 0;
    if (msf) {
        *q++ = 0;
        lba_to_msf(q, nb_sectors);
        q += 3;
    } else {
        stl_be_p(q, nb_sectors);
        q += 4;
    }
    len = q - buf;
    stw_be_p(buf, len - 2);
    return len;
}

int cdrom_read_toc_raw(int nb_sectors, uint8_t *buf, int msf, int session_num)
{
    uint8_t *q;
    int len;

    q = buf + 2;
    *q++ = 1;
    *q++ = 1;

    *q++ = 1;
    *q++ = 0x14;
    *q++ = 0;
    *q++ = 0xa0;
    *q++ = 0;
    *q++ = 0;
    *q++ = 0;
    *q++ = 0;
    *q++ = 1;
    *q++ = 0x00;
    *q++ = 0x00;

    *q++ = 1;
    *q++ = 0x14;
    *q++ = 0;
    *q++ = 0xa1;
    *q++ = 0;
    *q++ = 0;
    *q++ = 0;
    *q++ = 0;
    *q++ = 1;
    *q++ = 0x00;
    *q++ = 0x00;

    *q++ = 1;
    *q++ = 0x14;
    *q++ = 0;
    *q++ = 0xa2;
    *q++ = 0;
    *q++ = 0;
    *q++ = 0;
    if (msf) {
        *q++ = 0;
        lba_to_msf(q, nb_sectors);
        q += 3;
    } else {
        stl_be_p(q, nb_sectors);
        q += 4;
    }

    *q++ = 1;
    *q++ = 0x14;
    *q++ = 0;
    *q++ = 1;
    *q++ = 0;
    *q++ = 0;
    *q++ = 0;
    if (msf) {
        *q++ = 0;
        lba_to_msf(q, 0);
        q += 3;
    } else {
        *q++ = 0;
        *q++ = 0;
        *q++ = 0;
        *q++ = 0;
    }

    len = q - buf;
    stw_be_p(buf, len - 2);
    return len;
}
