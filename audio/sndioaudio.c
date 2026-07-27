/*
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2019 Alexandre Ratchov <alex@caoua.org>
 */

/*
 * TODO :
 *
 * Use a single device and open it in full-duplex rather than
 * opening it twice (once for playback once for recording).
 *
 * This is the only way to ensure that playback doesn't drift with respect
 * to recording, which is what guest systems expect.
 */

#include "qemu/osdep.h"
#include <poll.h>
#include <sndio.h>
#include "qemu/main-loop.h"
#include "qemu/audio.h"
#include "trace.h"

#define AUDIO_CAP "sndio"
#include "audio_int.h"

#define SNDIO_LATENCY_US   50000

typedef struct SndioVoice {
    union {
        HWVoiceOut out;
        HWVoiceIn in;
    } hw;
    struct sio_par par;
    struct sio_hdl *hdl;
    struct pollfd *pfds;
    struct pollindex {
        struct SndioVoice *self;
        int index;
    } *pindexes;
    unsigned char *buf;
    size_t buf_size;
    size_t sndio_pos;
    size_t qemu_pos;
    unsigned int mode;
    unsigned int nfds;
    bool enabled;
} SndioVoice;

typedef struct SndioConf {
    const char *devname;
    unsigned int latency;
} SndioConf;

static void sndio_poll_in(void *arg);
static void sndio_poll_out(void *arg);

static void sndio_poll_clear(SndioVoice *self)
{
    struct pollfd *pfd;
    int i;

    for (i = 0; i < self->nfds; i++) {
        pfd = &self->pfds[i];
        qemu_set_fd_handler(pfd->fd, NULL, NULL, NULL);
    }

    self->nfds = 0;
}

static void sndio_write(SndioVoice *self)
{
    size_t todo, n;

    todo = self->qemu_pos - self->sndio_pos;

    while (todo > 0) {
        n = sio_write(self->hdl, self->buf + self->sndio_pos, todo);
        if (n == 0) {
            break;
        }
        self->sndio_pos += n;
        todo -= n;
    }

    if (self->sndio_pos == self->buf_size) {
        self->sndio_pos = 0;
        self->qemu_pos = 0;
    }
}

static void sndio_read(SndioVoice *self)
{
    size_t todo, n;

    todo = self->buf_size - self->sndio_pos;

    while (todo > 0) {
        n = sio_read(self->hdl, self->buf + self->sndio_pos, todo);
        if (n == 0) {
            break;
        }
        self->sndio_pos += n;
        todo -= n;
    }
}

static void sndio_poll_wait(SndioVoice *self)
{
    struct pollfd *pfd;
    int events, i;

    events = 0;
    if (self->mode == SIO_PLAY) {
        if (self->sndio_pos < self->qemu_pos) {
            events |= POLLOUT;
        }
    } else {
        if (self->sndio_pos < self->buf_size) {
            events |= POLLIN;
        }
    }

    self->nfds = sio_pollfd(self->hdl, self->pfds, events);

    for (i = 0; i < self->nfds; i++) {
        pfd = &self->pfds[i];
        if (pfd->fd < 0) {
            continue;
        }
        qemu_set_fd_handler(pfd->fd,
            (pfd->events & POLLIN) ? sndio_poll_in : NULL,
            (pfd->events & POLLOUT) ? sndio_poll_out : NULL,
            &self->pindexes[i]);
        pfd->revents = 0;
    }
}

static void sndio_poll_event(SndioVoice *self, int index, int event)
{
    int revents;

    sndio_poll_clear(self);

    self->pfds[index].revents = event;

    revents = sio_revents(self->hdl, self->pfds);
    if (self->mode == SIO_PLAY) {
        if (revents & POLLOUT) {
            sndio_write(self);
        }

        if (self->qemu_pos < self->buf_size) {
            audio_run(self->hw.out.s, "sndio_out");
        }
    } else {
        if (revents & POLLIN) {
            sndio_read(self);
        }

        if (self->qemu_pos < self->sndio_pos) {
            audio_run(self->hw.in.s, "sndio_in");
        }
    }

    if (self->enabled) {
        sndio_poll_wait(self);
    }
}

static size_t sndio_buffer_get_free(HWVoiceOut *hw)
{
    SndioVoice *self = (SndioVoice *) hw;

    return self->buf_size - self->qemu_pos;
}

static void *sndio_get_buffer_out(HWVoiceOut *hw, size_t *size)
{
    SndioVoice *self = (SndioVoice *) hw;

    *size = self->buf_size - self->qemu_pos;
    return self->buf + self->qemu_pos;
}

static size_t sndio_put_buffer_out(HWVoiceOut *hw, void *buf, size_t size)
{
    SndioVoice *self = (SndioVoice *) hw;

    self->qemu_pos += size;
    sndio_poll_wait(self);
    return size;
}

static void *sndio_get_buffer_in(HWVoiceIn *hw, size_t *size)
{
    SndioVoice *self = (SndioVoice *) hw;
    size_t todo, max_todo;

    max_todo = *size;

    todo = self->sndio_pos - self->qemu_pos;
    if (todo > max_todo) {
        todo = max_todo;
    }

    *size = todo;
    return self->buf + self->qemu_pos;
}

static void sndio_put_buffer_in(HWVoiceIn *hw, void *buf, size_t size)
{
    SndioVoice *self = (SndioVoice *) hw;

    self->qemu_pos += size;
    if (self->qemu_pos == self->buf_size) {
        self->qemu_pos = 0;
        self->sndio_pos = 0;
    }
    sndio_poll_wait(self);
}

static void sndio_poll_out(void *arg)
{
    struct pollindex *pindex = (struct pollindex *) arg;

    sndio_poll_event(pindex->self, pindex->index, POLLOUT);
}

static void sndio_poll_in(void *arg)
{
    struct pollindex *pindex = (struct pollindex *) arg;

    sndio_poll_event(pindex->self, pindex->index, POLLIN);
}

static void sndio_fini(SndioVoice *self)
{
    if (self->hdl) {
        sio_close(self->hdl);
        self->hdl = NULL;
    }

    g_free(self->pfds);
    g_free(self->pindexes);
    g_free(self->buf);
}

static int sndio_init(SndioVoice *self,
                      struct audsettings *as, int mode, Audiodev *dev)
{
    AudiodevSndioOptions *opts = &dev->u.sndio;
    unsigned long long latency;
    const char *dev_name;
    struct sio_par req;
    unsigned int nch;
    int i, nfds;

    dev_name = opts->dev ?: SIO_DEVANY;
    latency = opts->has_latency ? opts->latency : SNDIO_LATENCY_US;

    self->hdl = sio_open(dev_name, mode, 1);
    if (self->hdl == NULL) {
        dolog("failed to open device\n");
        return -1;
    }

    self->mode = mode;

    sio_initpar(&req);

    switch (as->fmt) {
    case AUDIO_FORMAT_S8:
        req.bits = 8;
        req.sig = 1;
        break;
    case AUDIO_FORMAT_U8:
        req.bits = 8;
        req.sig = 0;
        break;
    case AUDIO_FORMAT_S16:
        req.bits = 16;
        req.sig = 1;
        break;
    case AUDIO_FORMAT_U16:
        req.bits = 16;
        req.sig = 0;
        break;
    case AUDIO_FORMAT_S32:
        req.bits = 32;
        req.sig = 1;
        break;
    case AUDIO_FORMAT_U32:
        req.bits = 32;
        req.sig = 0;
        break;
    default:
        dolog("unknown audio sample format\n");
        return -1;
    }

    if (req.bits > 8) {
        req.le = as->endianness ? 0 : 1;
    }

    req.rate = as->freq;
    if (mode == SIO_PLAY) {
        req.pchan = as->nchannels;
    } else {
        req.rchan = as->nchannels;
    }

    req.appbufsz = req.rate * latency / 1000000;

    if (!sio_setpar(self->hdl, &req)) {
        dolog("failed set audio params\n");
        goto fail;
    }

    if (!sio_getpar(self->hdl, &self->par)) {
        dolog("failed get audio params\n");
        goto fail;
    }

    nch = (mode == SIO_PLAY) ? self->par.pchan : self->par.rchan;

    if (self->par.bits != req.bits || self->par.bps != req.bits / 8 ||
        self->par.sig != req.sig || (req.bits > 8 && self->par.le != req.le) ||
        self->par.rate != as->freq || nch != as->nchannels) {
        dolog("unsupported audio params\n");
        goto fail;
    }

    self->buf_size = self->par.round * self->par.bps * nch;

    self->buf = g_malloc(self->buf_size);
    if (self->buf == NULL) {
        dolog("failed to allocate audio buffer\n");
        goto fail;
    }

    nfds = sio_nfds(self->hdl);

    self->pfds = g_malloc_n(nfds, sizeof(struct pollfd));
    if (self->pfds == NULL) {
        dolog("failed to allocate pollfd structures\n");
        goto fail;
    }

    self->pindexes = g_malloc_n(nfds, sizeof(struct pollindex));
    if (self->pindexes == NULL) {
        dolog("failed to allocate pollindex structures\n");
        goto fail;
    }

    for (i = 0; i < nfds; i++) {
        self->pindexes[i].self = self;
        self->pindexes[i].index = i;
    }

    return 0;
fail:
    sndio_fini(self);
    return -1;
}

static void sndio_enable(SndioVoice *self, bool enable)
{
    if (enable) {
        sio_start(self->hdl);
        self->enabled = true;
        sndio_poll_wait(self);
    } else {
        self->enabled = false;
        sndio_poll_clear(self);
        sio_stop(self->hdl);
    }
}

static void sndio_enable_out(HWVoiceOut *hw, bool enable)
{
    SndioVoice *self = (SndioVoice *) hw;

    sndio_enable(self, enable);
}

static void sndio_enable_in(HWVoiceIn *hw, bool enable)
{
    SndioVoice *self = (SndioVoice *) hw;

    sndio_enable(self, enable);
}

static int sndio_init_out(HWVoiceOut *hw, struct audsettings *as, void *opaque)
{
    SndioVoice *self = (SndioVoice *) hw;

    if (sndio_init(self, as, SIO_PLAY, opaque) == -1) {
        return -1;
    }

    audio_pcm_init_info(&hw->info, as);
    hw->samples = self->par.round;
    return 0;
}

static int sndio_init_in(HWVoiceIn *hw, struct audsettings *as, void *opaque)
{
    SndioVoice *self = (SndioVoice *) hw;

    if (sndio_init(self, as, SIO_REC, opaque) == -1) {
        return -1;
    }

    audio_pcm_init_info(&hw->info, as);
    hw->samples = self->par.round;
    return 0;
}

static void sndio_fini_out(HWVoiceOut *hw)
{
    SndioVoice *self = (SndioVoice *) hw;

    sndio_fini(self);
}

static void sndio_fini_in(HWVoiceIn *hw)
{
    SndioVoice *self = (SndioVoice *) hw;

    sndio_fini(self);
}

static void *sndio_audio_init(Audiodev *dev, Error **errp)
{
    assert(dev->driver == AUDIODEV_DRIVER_SNDIO);
    return dev;
}

static void sndio_audio_fini(void *opaque)
{
}

static struct audio_pcm_ops sndio_pcm_ops = {
    .init_out        = sndio_init_out,
    .fini_out        = sndio_fini_out,
    .enable_out      = sndio_enable_out,
    .write           = audio_generic_write,
    .buffer_get_free = sndio_buffer_get_free,
    .get_buffer_out  = sndio_get_buffer_out,
    .put_buffer_out  = sndio_put_buffer_out,
    .init_in         = sndio_init_in,
    .fini_in         = sndio_fini_in,
    .read            = audio_generic_read,
    .enable_in       = sndio_enable_in,
    .get_buffer_in   = sndio_get_buffer_in,
    .put_buffer_in   = sndio_put_buffer_in,
};

static struct audio_driver sndio_audio_driver = {
    .name           = "sndio",
    .init           = sndio_audio_init,
    .fini           = sndio_audio_fini,
    .pcm_ops        = &sndio_pcm_ops,
    .max_voices_out = INT_MAX,
    .max_voices_in  = INT_MAX,
    .voice_size_out = sizeof(SndioVoice),
    .voice_size_in  = sizeof(SndioVoice)
};

static void register_audio_sndio(void)
{
    audio_driver_register(&sndio_audio_driver);
}

type_init(register_audio_sndio);
