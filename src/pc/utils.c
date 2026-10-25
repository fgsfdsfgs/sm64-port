#ifdef TARGET_XBOX

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <nxdk/mount.h>
#include <winapi/fileapi.h>

#include "utils.h"

extern const unsigned char gXboxTitleImageXbx[];
extern const unsigned int gXboxTitleImageXbxSize;
extern const unsigned char gXboxSaveImageXbx[];
extern const unsigned int gXboxSaveImageXbxSize;

static void xbox_write_file(const char *path, const void *data, size_t size) {
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return;
    }

    fwrite(data, 1, size, fp);
    fclose(fp);
}

static void xbox_write_metadata_file(const char *path, const char *key, const char *value) {
    char line[64];
    const unsigned char bom[] = { 0xff, 0xfe };
    size_t i;
    FILE *fp;

    snprintf(line, sizeof(line), "%s=%s\r\n", key, value);

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return;
    }

    fwrite(bom, 1, sizeof(bom), fp);
    for (i = 0; line[i] != '\0'; i++) {
        const unsigned char utf16le[2] = { (unsigned char) line[i], 0x00 };
        fwrite(utf16le, 1, sizeof(utf16le), fp);
    }

    fclose(fp);
}

static void xbox_provision_title_metadata(const char *title_path) {
    char path[64];

    snprintf(path, sizeof(path), "%s\\TitleMeta.xbx", title_path);
    xbox_write_metadata_file(path, "TitleName", "Super Mario 64");

    snprintf(path, sizeof(path), "%s\\TitleImage.xbx", title_path);
    xbox_write_file(path, gXboxTitleImageXbx, gXboxTitleImageXbxSize);

    snprintf(path, sizeof(path), "%s\\SaveImage.xbx", title_path);
    xbox_write_file(path, gXboxSaveImageXbx, gXboxSaveImageXbxSize);
}

static void xbox_provision_save_metadata(const char *save_path) {
    char path[64];

    snprintf(path, sizeof(path), "%s\\SaveMeta.xbx", save_path);
    xbox_write_metadata_file(path, "Name", "Super Mario 64");

    /*
     * Compatibility mirror for the first dashboard-save candidate, which
     * placed SaveImage.xbx inside the fixed save directory. The retail
     * Halo 2 reference places the canonical SaveImage.xbx at title level.
     * Mirroring the same star image here migrates existing test installs
     * without touching the gameplay save/config payload.
     */
    snprintf(path, sizeof(path), "%s\\SaveImage.xbx", save_path);
    xbox_write_file(path, gXboxSaveImageXbx, gXboxSaveImageXbxSize);
}

void bcopy(const void *src, void *dst, size_t len) {
    memcpy(dst, src, len);
}

void bzero(void *dst, size_t len) {
    memset(dst, 0, len);
}

const char *get_user_path(void) {
    static char path[32] = { 0 };
    if (!path[0]) {
        if (!nxIsDriveMounted('E'))
            nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
        snprintf(path, sizeof(path), "E:\\UDATA\\%08x", XBE_TITLE_ID);
        CreateDirectoryA(path, NULL);
        xbox_provision_title_metadata(path);
        strncat(path, "\\000000000000", sizeof(path) - 1);
        CreateDirectoryA(path, NULL);
        xbox_provision_save_metadata(path);
        strncat(path, "\\", sizeof(path) - 1);
    }
    return path;
}

#else

const char *get_user_path(void) {
    return "";
}

#endif

const char *get_config_filename(void) {
    static char path[64] = { 0 };
    if (!path[0]) snprintf(path, sizeof(path), "%s%s", get_user_path(), CONFIG_FILE);
    return path;
}

const char *get_save_filename(void) {
    static char path[64] = { 0 };
    if (!path[0]) snprintf(path, sizeof(path), "%s%s", get_user_path(), SAVE_FILE);
    return path;
}
