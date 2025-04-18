//
// fs_ram.c - in-memory file
//
// v0.1 / 2022-08-25 / Io Engineering / Terje
//

/*

Copyright (c) 2022, Terje Io
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its contributors may
be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "driver.h"
#include "grbl/hal.h"
#include "grbl/platform.h"
#include "grbl/vfs.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *data;
    const char *pos;
    bool write;
    size_t len;
    size_t remaining;
    vfs_file_t file;
    stream_block_tx_buffer_t txbuf;
} ram_file_t;

static ram_file_t v_file = {0};
static driver_reset_ptr driver_reset = NULL;

static bool ram_write (ram_file_t *file, const char *s, size_t length)
{
    char *data = realloc((void *)v_file.data, v_file.len + length);

    if(data == NULL) {

        if(v_file.data != NULL)
            free((void *)v_file.data);

        v_file.len = 0;
        v_file.data = NULL;

        return false;
    }

    v_file.data = data;

    memcpy((void *)(v_file.data + v_file.len), s, length);

    v_file.len += length;
    v_file.txbuf.s = v_file.txbuf.data;
    v_file.txbuf.length = 0;

    return true;
}

static vfs_file_t *fs_open (const char *filename, const char *mode)
{
    if((v_file.write = strchr(mode, 'w') != NULL)) {

        if(v_file.file.handle != 0)
            return NULL;

        v_file.len = 0;
        v_file.file.handle = 1;
        v_file.txbuf.s = v_file.txbuf.data;
        v_file.txbuf.length = 0;
        v_file.txbuf.max_length = sizeof(v_file.txbuf.data);

        if(v_file.data) {
            free((void *)v_file.data);
            v_file.data = NULL;
        }
    } else {
        v_file.file.size = v_file.len;
        v_file.pos = v_file.data;
        v_file.remaining = v_file.len;
    }

    return v_file.file.handle == 0 ? NULL : &v_file.file;
}

static void fs_close (vfs_file_t *file)
{
    if(v_file.write) {
        if(v_file.txbuf.length)
            ram_write(&v_file, v_file.txbuf.data, v_file.txbuf.length);
    } else {
        if(v_file.data != NULL) {
            free((void *)v_file.data);
            v_file.data = NULL;
        }

        v_file.file.handle = 0;
    }
}

static size_t fs_read (void *buffer, size_t size, size_t count, vfs_file_t *file)
{
    size_t rcount = 0;

    if(v_file.pos) {
        rcount = size * count > v_file.remaining ? v_file.remaining : size * count;
        memcpy(buffer, v_file.pos, rcount);
        v_file.pos += rcount;
    } else
        v_file.remaining = rcount = 0;

    v_file.remaining -= rcount;

    return rcount;
}

static size_t fs_write (const void *buffer, size_t size, size_t count, vfs_file_t *file)
{
    char *s = (char *)buffer;
    size_t length = size * count;

    if(length == 0 || v_file.file.handle == 0)
        return 0;

    if(v_file.txbuf.length && (v_file.txbuf.length + length) > v_file.txbuf.max_length) {
        if(!ram_write(&v_file, v_file.txbuf.data, v_file.txbuf.length))
            return 0;
    }

    if(v_file.txbuf.length == 0 && length > v_file.txbuf.max_length)
        return ram_write(&v_file, s, length) ? length : 0;

    memcpy(v_file.txbuf.s, s, length);
    v_file.txbuf.length += length;
    v_file.txbuf.s += length;

    return length;
}

static size_t fs_tell (vfs_file_t *file)
{
    return v_file.len - v_file.remaining;
}

static bool fs_eof (vfs_file_t *file)
{
    return v_file.remaining == 0;
}

static int fs_unlink (const char *filename)
{
    if(v_file.data != NULL) {
        free((void *)v_file.data);
        v_file.data = NULL;
    }

    v_file.file.handle = 0;

    return 0;
}

static int fs_dirop (const char *path)
{
    return -1;
}

static vfs_dir_t *fs_opendir (const char *path)
{
    return NULL;
}

static void fs_closedir (vfs_dir_t *dir)
{
}

static int fs_stat (const char *filename, vfs_stat_t *st)
{
    if (v_file.file.handle) {
        st->st_size = v_file.len;
    }

    return v_file.file.handle ? 0 : -1;
}

static void fs_reset (void)
{
    driver_reset();

    if(v_file.data != NULL) {
        free((void *)v_file.data);
        v_file.data = NULL;
    }

    v_file.file.handle = 0;
}

void fs_ram_mount (void)
{
    static const vfs_t fs = {
        .fopen = fs_open,
        .fclose = fs_close,
        .fread = fs_read,
        .fwrite = fs_write,
        .ftell = fs_tell,
        .feof = fs_eof,
        .funlink = fs_unlink,
        .fmkdir = fs_dirop,
        .fchdir = fs_dirop,
        .frmdir = fs_dirop,
        .fopendir = fs_opendir,
        .fclosedir = fs_closedir,
        .fstat = fs_stat
    };

    if(driver_reset == NULL) {
        driver_reset = hal.driver_reset;
        hal.driver_reset = fs_reset;
        vfs_mount("/ram", &fs, (vfs_st_mode_t){ .directory = true, .hidden = true });
    }
}
