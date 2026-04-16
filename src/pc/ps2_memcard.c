#ifdef TARGET_PS2

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <malloc.h>
#include <libmc.h>
#include <sjis.h>

#include "ps2_memcard.h"

#define MAX_PORTS 2
#define ICN_FILE "sm64.icn"

static int memcard_port = -1;
static int memcard_type, memcard_free, memcard_format;

static char save_path[64] = "mc0:" PS2_SAVE_PATH;
static char save_file[64] = "mc0:" PS2_SAVE_PATH "/save.bin";
static char save_meta[64] = "mc0:" PS2_SAVE_PATH "/icon.sys";
static char save_icon[64] = "mc0:" PS2_SAVE_PATH "/" ICN_FILE;

extern unsigned int size_ps2_icon_data;
extern unsigned char ps2_icon_data;

#define ALIGN1K(x) (((x) + 1023) >> 10)
#define SAVE_SIZE (ALIGN1K(sizeof(mcIcon)) + ALIGN1K(size_ps2_icon_data) + 1 + 3) // in 1k blocks

static inline bool check_save(const int port) {
    save_meta[2] = '0' + port;
    int fd = open(save_meta, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
}

static inline bool create_save(void) {
    static const iconIVECTOR bgcolor[] = {
        {  68,  23, 116,  0 }, // top left
        { 255, 255, 255,  0 }, // top right
        { 255, 255, 255,  0 }, // bottom left
        {  68,  23, 116,  0 }, // bottom right
    };

    static const iconFVECTOR lightdir[] = {
        { 0.5, 0.5, 0.5, 0.0 },
        { 0.0,-0.4,-0.1, 0.0 },
        {-0.5,-0.5, 0.5, 0.0 },
    };

    static const iconFVECTOR lightcol[] = {
        { 0.3, 0.3, 0.3, 0.00 },
        { 0.3, 0.3, 0.3, 0.00 },
        { 0.3, 0.3, 0.3, 0.00 },
    };

    static const iconFVECTOR ambient = { 0.50, 0.50, 0.50, 0.00 };

    // PS2 icon.sys title must be manually encoded in SJIS.
    // DO NOT replace with strcpy_sjis(): it causes incorrect spacing on real hardware.
    //
    // Format:
    // - 0x0000 splits "Super" / "Mario 64" (used with nlOffset)
    // - 0x8140 = full-width SJIS space (fixes spacing)
    //
    // Verified on hardware; see commit history for details.
    static const uint16_t sjis_title[] = {
        'S','u','p','e','r', 0x0000,
        'M','a','r','i','o', 0x8140,'6','4', 0x0000
    };

    mcIcon icon_sys;

    memset(&icon_sys, 0, sizeof(mcIcon));
    strcpy(icon_sys.head, "PS2D");
    memcpy(&icon_sys.title, sjis_title, sizeof(sjis_title));
    icon_sys.nlOffset = 5;
    icon_sys.trans = 0x60;
    memcpy(icon_sys.bgCol, bgcolor, sizeof(bgcolor));
    memcpy(icon_sys.lightDir, lightdir, sizeof(lightdir));
    memcpy(icon_sys.lightCol, lightcol, sizeof(lightcol));
    memcpy(icon_sys.lightAmbient, ambient, sizeof(ambient));
    strcpy(icon_sys.view, ICN_FILE); // these filenames are relative to the directory
    strcpy(icon_sys.copy, ICN_FILE); // in which icon.sys resides.
    strcpy(icon_sys.del,  ICN_FILE);

    // save the icon
    int fd = open(save_icon, O_CREAT | O_WRONLY);
    if (fd < 0) return false;
    write(fd, &ps2_icon_data, size_ps2_icon_data);
    close(fd);

    // save the metadata
    fd = open(save_meta, O_WRONLY | O_CREAT);
    if (fd < 0) return false;
    write(fd, &icon_sys, sizeof(icon_sys));
    close(fd);

    return true;
}

static inline void memcard_detect(void) {
    int mctype[MAX_PORTS], mcfree[MAX_PORTS], mcformat[MAX_PORTS];
    int freeport = -1;
    int ret;

    // try looking for the save file, remember the first card with enough free space in case there's no save

    for (int port = 0; port < MAX_PORTS; ++port) {
        mcGetInfo(port, 0, mctype + port, mcfree + port, mcformat + port);
        mcSync(0, NULL, &ret);
        if (ret == 0 || ret == -1) {
            mcformat[port] = 1; // -2 means it's unformatted, which can't happen here
            if (check_save(port)) {
                memcard_port = port;
                memcard_type = mctype[port];
                memcard_free = mcfree[port];
                memcard_format = mcformat[port];
                printf("ps2_memcard: detected save on memcard type %d free %d on port %d\n", memcard_type, memcard_free, memcard_port);
                return;
            } else if (freeport == -1 && mcformat[port] && mcfree[port] >= SAVE_SIZE) {
                freeport = port;
            }
        }
    }

    // no save, check if we found free space
    if (freeport >= 0) {
        memcard_port = freeport;
        memcard_type = mctype[freeport];
        memcard_free = mcfree[freeport];
        memcard_format = mcformat[freeport];
        printf("ps2_memcard: detected memcard type %d free %d on port %d\n", memcard_type, memcard_free, memcard_port);
    } else {
        printf("ps2_memcard: could not detect any usable memcards\n");
    }
}

static inline bool memcard_check(void) {
    int ret;

    if (memcard_port < 0) {
        memcard_detect();
        return (memcard_port >= 0);
    }

    mcGetInfo(memcard_port, 0, &memcard_type, &memcard_free, &memcard_format);
    mcSync(0, NULL, &ret);
    if (ret != 0) {
        printf("ps2_memcard: memcard lost, re-detecting\n");
        memcard_port = -1;
        memcard_detect();
        return (memcard_port >= 0);
    }

    return true;
}

bool ps2_memcard_init(void) {
    int ret = -1;

    ret = mcInit(MC_TYPE_XMC);
    if (ret < 0) ret = mcInit(MC_TYPE_MC);
    if (ret < 0) {
        printf("ps2_memcard: mcInit failed: %d\n", ret);
        return false;
    }

    printf("ps2_memcard: SAVE_SIZE = %u\n", SAVE_SIZE);

    memcard_detect();

    return true;
}

bool ps2_memcard_save(const void *data, const int ofs, const uint32_t size) {
    if (!memcard_check()) return false;

    save_path[2] = '0' + memcard_port;
    save_file[2] = '0' + memcard_port;
    save_meta[2] = '0' + memcard_port;
    save_icon[2] = '0' + memcard_port;

    if (!check_save(memcard_port)) {
        // no save folder and icon, create it
        mkdir(save_path, 0777);
        if (!create_save()) return false;
    }

    int fd = open(save_file, O_WRONLY | O_CREAT);
    if (fd < 0) return false;
    if (lseek(fd, ofs, SEEK_SET) != (off_t)-1)
        write(fd, data, size);
    close(fd);

    return true;
}

bool ps2_memcard_load(void *data, const int ofs, const uint32_t size) {
    if (!memcard_check()) return false;

    save_path[2] = '0' + memcard_port;
    save_file[2] = '0' + memcard_port;
    save_meta[2] = '0' + memcard_port;

    if (!check_save(memcard_port)) return false;

    int fd = open(save_file, O_RDONLY);
    if (fd < 0) return false;
    if (lseek(fd, ofs, SEEK_SET) != (off_t)-1)
        read(fd, data, size);
    close(fd);

    return true;
}

#endif // TARGET_PS2
