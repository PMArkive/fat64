#define FUSE_USE_VERSION 26

#include <errno.h> // gets the E family of errors, e.g., EIO
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fuse.h>

#include "fs.h"
#include "common.h"

static int getattr(const char *path, struct stat *stbuf) {
    int ret = 0;

    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0555;
        stbuf->st_nlink = 2;
    }

    else {
        fat_file_t file;

        int fret = fat_open(path, NULL, &file);
        if (fret == FAT_SUCCESS) {
            // TODO: date
            if (fat_file_isdir(&file)) {
                stbuf->st_mode = S_IFDIR | 0555;
                stbuf->st_size = 0;
                stbuf->st_nlink = 2;
            }
            else {
                stbuf->st_mode = S_IFREG | 0444;
                stbuf->st_size = fat_file_size(&file);
                stbuf->st_nlink = 1;
            }
        }

        else {
            if (fret == FAT_NOTFOUND)
                ret = -ENOENT;

            // other error: something's amiss
            else
                ret = -EIO;
        }
    }

    return ret;
}

static int readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    fat_file_t dir;
    int ret;

    ret = fat_open(path, NULL, &dir);

    // file found
    if (ret == FAT_SUCCESS) {
        // directory: fill 'er up
        if (fat_file_isdir(&dir)) {
            while (fat_readdir(&dir.de) > 0)
                filler(buf, dir.de.name, NULL, 0);

            ret = 0;
        }

        // not a directory, facplam
        else
            ret = -EIO;
    }

    // file not found
    else if (ret == FAT_NOTFOUND)
        ret = -ENOENT;

    // something funky
    else
        ret = -EIO;

    return ret;
}

static int _fat_open(const char *path, struct fuse_file_info *fi) {
    // only support read-only mode
    if ((fi->flags & 3) != O_RDONLY)
        return -EACCES;

    fat_file_t file, *file_save;

    int ret = fat_open(path, NULL, &file);

    // success: malloc a copy of the file struct and store it in the file_info_t
    if (ret == FAT_SUCCESS) {
        file_save = malloc(sizeof(fat_file_t));
        *file_save = file;
        fi->fh = (uintptr_t)file_save;
        return 0;
    }

    if (ret == FAT_NOTFOUND)
        return -ENOENT;

    // some weird error
    return -EIO;
}

// release is the equivalent of close
// if the file is opened multiple times (a la dup/dup2) this is called the last
// time the file is closed
static int _fat_release(const char *path, struct fuse_file_info *fi) {
    free((fat_file_t *)(uintptr_t)fi->fh);
    return 0;
}

static int _fat_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    fat_file_t *file = (fat_file_t *)(uintptr_t)fi->fh;

    // seek if we're not at the right position
    if (offset != file->position) {
        int ret = fat_lseek(file, offset, SEEK_SET);
        if (ret != FAT_SUCCESS)
            return -EIO;
    }

    size = fat_read(file, (unsigned char *)buf, size);

    return size;
}

static struct fuse_operations fat_oper = {
    .getattr        = getattr,
    .readdir        = readdir,
    .open           = _fat_open,
    .release        = _fat_release,
    .read           = _fat_read,
};

int main(int argc, char **argv) {
    fat_disk_open("fat32.img");
    int ret = fat_init();
    if (ret != 0) {
        puts(message1);
        abort();
    }

    return fuse_main(argc, argv, &fat_oper, NULL);
}
