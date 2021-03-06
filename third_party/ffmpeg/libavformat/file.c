/*
 * Buffered file io for ffmpeg system
 * Copyright (c) 2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avstring.h"
#include "avformat.h"
#include <fcntl.h>
// for nanosleep on non-Win platforms
#ifndef __MINGW32__
#include <time.h>
#endif
#if HAVE_SETMODE || defined(__MINGW32__)
#include <io.h>
#endif
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "os_support.h"


/* standard file protocol */

static int file_read(URLContext *h, unsigned char *buf, int size)
{
    int fd = (intptr_t) h->priv_data;
    int rv;
	do
	{
		rv = read(fd, buf, size);
		if (rv <= 0 && ((h->flags & URL_ACTIVEFILE) == URL_ACTIVEFILE))
		{
#ifdef __MINGW32__
				usleep(20000);
#else
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 20000000;
				nanosleep(&ts, NULL);
#endif
	        if (url_interrupt_cb())
	            return -EINTR;
 		}
		else
			break;
	} while (1);
	return rv;
}

static int file_write(URLContext *h, const unsigned char *buf, int size)
{
    int fd = (intptr_t) h->priv_data;
    return write(fd, buf, size);
}

static int file_get_handle(URLContext *h)
{
    return (intptr_t) h->priv_data;
}

#if CONFIG_FILE_PROTOCOL

static int file_open(URLContext *h, const char *filename, int flags)
{
    int access;
    int fd;

    av_strstart(filename, "file:", &filename);

    if (flags & URL_RDWR) {
        access = O_CREAT | O_TRUNC | O_RDWR;
    } else if (flags & URL_WRONLY) {
        access = O_CREAT | O_TRUNC | O_WRONLY;
    } else {
        access = O_RDONLY;
    }
#ifdef O_BINARY
    access |= O_BINARY;
#endif
#ifdef __MINGW32__
	// Check for UTF-8 unicode pathname
	int strl = strlen(filename);
	wchar_t* wfilename = av_malloc(sizeof(wchar_t) * (1 + strl));
	int wpos = 0;
	int i = 0;
	for (i = 0; i < strl; i++)
	{
		wfilename[wpos] = 0;
		if ((filename[i] & 0x80) == 0)
		{
			// ASCII character
			wfilename[wpos++] = filename[i];
		}
		else if (i + 1 < strl && ((filename[i] & 0xE0) == 0xC0) && ((filename[i + 1] & 0xC0) == 0x80))
		{
			// two octets for this character
			wfilename[wpos++] = ((filename[i] & 0x1F) << 6) + (filename[i + 1] & 0x3F);
			i++;
		}
		else if (i + 2 < strl && ((filename[i] & 0xF0) == 0xE0) && ((filename[i + 1] & 0xC0) == 0x80) && 
			((filename[i + 2] & 0xC0) == 0x80))
		{
			// three octets for this character
			wfilename[wpos++] = ((filename[i] & 0x0F) << 12) + ((filename[i + 1] & 0x3F) << 6) + (filename[i + 2] & 0x3F);
			i+=2;
		}
		else
			wfilename[wpos++] = filename[i];
	}
	wfilename[wpos] = 0;
	fd = _wopen(wfilename, access, 0666);
	av_free(wfilename);
#else
    fd = open(filename, access, 0666);
#endif
    if (fd == -1)
        return AVERROR(errno);
    h->priv_data = (void *) (intptr_t) fd;
    return 0;
}

/* XXX: use llseek */
static int64_t file_seek(URLContext *h, int64_t pos, int whence)
{
    int fd = (intptr_t) h->priv_data;
    if (whence == AVSEEK_SIZE) {
        struct stat st;
        int ret = fstat(fd, &st);
        return ret < 0 ? AVERROR(errno) : st.st_size;
    }
    return lseek(fd, pos, whence);
}

static int file_close(URLContext *h)
{
    int fd = (intptr_t) h->priv_data;
    return close(fd);
}

URLProtocol file_protocol = {
    "file",
    file_open,
    file_read,
    file_write,
    file_seek,
    file_close,
    .url_get_file_handle = file_get_handle,
};

#endif /* CONFIG_FILE_PROTOCOL */

#if CONFIG_PIPE_PROTOCOL

static int pipe_open(URLContext *h, const char *filename, int flags)
{
    int fd;
    char *final;
    av_strstart(filename, "pipe:", &filename);

    fd = strtol(filename, &final, 10);
    if((filename == final) || *final ) {/* No digits found, or something like 10ab */
        if (flags & URL_WRONLY) {
            fd = 1;
        } else {
            fd = 0;
        }
    }
#if HAVE_SETMODE
    setmode(fd, O_BINARY);
#endif
    h->priv_data = (void *) (intptr_t) fd;
    h->is_streamed = 1;
    return 0;
}

URLProtocol pipe_protocol = {
    "pipe",
    pipe_open,
    file_read,
    file_write,
    .url_get_file_handle = file_get_handle,
};

#endif /* CONFIG_PIPE_PROTOCOL */
