#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <xboxkrnl/xboxkrnl.h>
#include <hal/audio.h>

#include "macros.h"
#include "audio_api.h"

#define BUF_FRAMES 1024
#define BUF_SIZE (BUF_FRAMES * 2 * 2)
#define NUM_BUFFERS 2
#define NUM_SAMPLES_32KHZ 683 // (32000 / 48000) * BUF_FRAMES

#define SNDPACKETLEN (8 * 1024)

/** lut for converting 32000 -> 48000 */
static const uint16_t up_32000_lut[1024] = {
#include "audio_xbox_lut.inc.h"
};

// this is basically SDL_dataqueue but slightly less generic

typedef struct sndpacket {
    size_t datalen;         /* bytes currently in use in this packet. */
    size_t startpos;        /* bytes currently consumed in this packet. */
    struct sndpacket *next; /* next item in linked list. */
    uint8_t data[];         /* packet data */
} sndpacket_t;

static sndpacket_t *qhead;
static sndpacket_t *qtail;
static sndpacket_t *qpool;
static size_t queued;

static int16_t *audio_buffer[NUM_BUFFERS];
static uint32_t audio_buffer_cur = 0;

static void sndqueue_init(const size_t bufsize) {
    const size_t wantpackets = (bufsize + (SNDPACKETLEN - 1)) / SNDPACKETLEN;
    for (size_t i = 0; i < wantpackets; ++i) {
        sndpacket_t *packet = malloc(sizeof(sndpacket_t) + SNDPACKETLEN);
        if (packet) {
            packet->datalen = 0;
            packet->startpos = 0;
            packet->next = qpool;
            qpool = packet;
        }
    }
}

static size_t sndqueue_read(void *buf, size_t len) {
    sndpacket_t *packet;
    uint8_t *ptr = buf;

    while ((len > 0) && ((packet = qhead) != NULL)) {
        const size_t avail = packet->datalen - packet->startpos;
        const size_t tx = (len < avail) ? len : avail;

        memcpy(ptr, packet->data + packet->startpos, tx);
        packet->startpos += tx;
        ptr += tx;
        queued -= tx;
        len -= tx;

        if (packet->startpos == packet->datalen) {
            qhead = packet->next;
            packet->next = qpool;
            qpool = packet;
        }
    }

    if (qhead == NULL)
        qtail = NULL;

    return (size_t)(ptr - (uint8_t *)buf);
}

static inline sndpacket_t *alloc_sndpacket(void) {
    sndpacket_t *packet = qpool;

    if (packet) {
        qpool = packet->next;
    } else {
        packet = malloc(sizeof(sndpacket_t) + SNDPACKETLEN);
        if (!packet) return NULL;
    }

    packet->datalen = 0;
    packet->startpos = 0;
    packet->next = NULL;

    if (qtail == NULL)
        qhead = packet;
    else
        qtail->next = packet;
    qtail = packet;

    return packet;
}

static int sndqueue_push(const void *data, size_t len) {
    sndpacket_t *origtail = qtail;
    const uint8_t *ptr = data;

    while (len > 0) {
        sndpacket_t *packet = qtail;
        if (!packet || (packet->datalen >= SNDPACKETLEN)) {
            packet = alloc_sndpacket();
            if (!packet) {
                // out of memory, fuck everything
                return -1;
            }
        }

        const size_t room = SNDPACKETLEN - packet->datalen;
        const size_t datalen = (len < room) ? len : room;
        memcpy(packet->data + packet->datalen, ptr, datalen);
        ptr += datalen;
        len -= datalen;
        packet->datalen += datalen;
        queued += datalen;
    }

    return 0;
}

static inline void convert_block(int16_t *out, const int16_t *in) {
    for (int p = 0; p < BUF_FRAMES; p++) {
        const int q = up_32000_lut[p] << 1;
        *out++ = in[q + 0];
        *out++ = in[q + 1];
    }
}

static void audio_callback(UNUSED void *dev, UNUSED void *arg) {
    static int16_t inbuf[NUM_SAMPLES_32KHZ * 2];
    const size_t rx = sndqueue_read(inbuf, sizeof(inbuf));

    /*
     * A short queue read means the producer fell behind. Do not resample
     * stale bytes left in this persistent buffer from the previous callback.
     * Silence only the unread tail; queue synchronization is otherwise
     * unchanged.
     */
    if (rx < sizeof(inbuf)) {
        memset((uint8_t *) inbuf + rx, 0, sizeof(inbuf) - rx);
    }

    convert_block(audio_buffer[audio_buffer_cur], inbuf);
    XAudioProvideSamples((uint8_t *)audio_buffer[audio_buffer_cur], BUF_SIZE, FALSE);
    audio_buffer_cur = !audio_buffer_cur;
}


static bool audio_xbox_init(void) {
    qhead = NULL;
    qtail = NULL;
    qpool = NULL;
    queued = 0;
    audio_buffer_cur = 0;
    memset(audio_buffer, 0, sizeof(audio_buffer));

    XAudioInit(16, 2, audio_callback, NULL);

    // this double-buffers
    for (uint32_t i = 0; i < NUM_BUFFERS; ++i) {
        audio_buffer[i] = (int16_t *)MmAllocateContiguousMemoryEx(BUF_SIZE, 0, 0xFFFFFFFF, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (!audio_buffer[i]) return false;
        // feed silence into the thing
        memset(audio_buffer[i], 0, BUF_SIZE);
        XAudioProvideSamples((uint8_t *)audio_buffer[i], BUF_SIZE, FALSE);
    }

    // start playback
    XAudioPlay();

    return true;
}

static int audio_xbox_buffered(void) {
    const int ret = queued / 4;
    return ret;
}

static int audio_xbox_get_desired_buffered(void) {
    return 1100;
}

static void audio_xbox_play(const uint8_t *buf, size_t len) {
    if (queued / 4 < 6000)
        sndqueue_push(buf, len);
}

struct AudioAPI audio_xbox = {
    audio_xbox_init,
    audio_xbox_buffered,
    audio_xbox_get_desired_buffered,
    audio_xbox_play
};
