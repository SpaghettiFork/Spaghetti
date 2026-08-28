/*
 * Copyright © 2020 Roman Gilg
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of the
 * copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <unistd.h>

#include "ftrace.h"

#if FTRACE

#define MAX_PATH_LENGTH 100

static FILE *trace_marker_file = NULL;

static Bool
ftrace_ready(void)
{
    return trace_marker_file != NULL;
}

static void _X_ATTRIBUTE_PRINTF(1, 0)
ftrace_write(const char *f, va_list args)
{
    char buf[1024];

    if (vsnprintf(buf, sizeof(buf), f, args) < 0)
        return;

    fprintf(trace_marker_file, "%s", buf);
    fflush(trace_marker_file);

}

void _X_ATTRIBUTE_PRINTF(1, 2)
ftrace_print(const char *f, ...)
{
    va_list args;

    va_start(args, f);
    ftrace_write(f, args);
    va_end(args);
}

void _X_ATTRIBUTE_PRINTF(2, 3)
ftrace_print_begin(unsigned long ctx, const char *f, ...)
{
    va_list args;
    char *f_ctx;

    f_ctx = malloc(strlen(f) + 30);

    if (sprintf(f_ctx, "%s (begin_ctx=%lu)", f, ctx) > 0) {
        va_start(args, f);
        ftrace_write(f_ctx, args);
        va_end(args);
    }
    free(f_ctx);
}

void _X_ATTRIBUTE_PRINTF(2, 3)
ftrace_print_end(unsigned long ctx, const char *f, ...)
{
    va_list args;
    char *f_ctx;

    f_ctx = malloc(strlen(f) + 30);

    if (sprintf(f_ctx, "%s (end_ctx=%lu)", f, ctx) > 0) {
        va_start(args, f);
        ftrace_write(f_ctx, args);
        va_end(args);
    }
    free(f_ctx);
}

static char*
ftrace_get_word(const char *line, int index)
{
    char delimiter[2] = " ";
    char *ptr, *linecpy, *ret;
    int i = 0;

    linecpy = malloc(strlen(line) + 1);
    strcpy(linecpy, line);

    ptr = strtok(linecpy, delimiter);

    while (ptr && i <= index) {
        if (i == index) {
            ret = malloc(strlen(ptr) + 1);
            strcpy(ret, ptr);
            free(linecpy);
            return ret;
        }
        ptr = strtok(NULL, delimiter);
        i++;
    }
    free(linecpy);
    return NULL;
}

static Bool
ftrace_set_trace_marker_file(char *line, Bool tracing)
{
    char ftrace_path[MAX_PATH_LENGTH] = {};
    const char path_add_tracing[] = "/trace_marker";
    const char path_add[] = "/tracing/trace_marker";
    const int max_path_len = MAX_PATH_LENGTH - strlen(tracing ? path_add_tracing : path_add);
    char *path;

    path = ftrace_get_word(line, 1);
    if (!path || strlen(path) > max_path_len)
        return FALSE;

    strcpy(ftrace_path, path);
    free(path);
    strcat(ftrace_path, tracing ? path_add_tracing : path_add);

    trace_marker_file = fopen(ftrace_path, "w");
    return trace_marker_file != NULL;
}

static Bool
ftrace_compare_line(char *line, const char *name)
{
    char *ptr;
    Bool ret;

    if (strncmp(line, name, strlen(name)) == 0)
        return TRUE;

    ptr = ftrace_get_word(line, 2);
    if (!ptr)
        return FALSE;

    ret = strncmp(ptr, name, strlen(name)) == 0;
    free(ptr);
    return ret;
}

static void
ftrace_find_trace_marker_file(void)
{
    FILE *fh;
    size_t len = 0;
    ssize_t read;
    char *line = NULL;

    fh = fopen("/proc/mounts", "r");
    if (!fh) {
        LogMessage(X_WARNING, "Failed to open mounts file for locating Ftrace trace marker.\n");
        return;
    }

    while ((read = getline(&line, &len, fh)) != -1) {
        if (ftrace_compare_line(line, "tracefs")) {
            if (ftrace_set_trace_marker_file(line, TRUE))
                break;
        }
        if (ftrace_compare_line(line, "debugfs")) {
            if (ftrace_set_trace_marker_file(line, FALSE))
                break;
        }
        len = 0;
    }
    fclose(fh);
    if (line)
        free(line);

    if (!ftrace_ready())
        LogMessage(X_WARNING, "Debug/tracing directory not found in mounts file.\n");
}

Bool
ftrace_enable(Bool enable)
{
    if (ftrace_ready() == enable) {
        /* No change */
        return TRUE;
    }

    if (enable) {
        ftrace_find_trace_marker_file();
        return ftrace_ready();
    }

    if (trace_marker_file) {
        fclose(trace_marker_file);
        trace_marker_file = NULL;
    }

    return TRUE;
}

#else
Bool
ftrace_enable(Bool enable)
{
    return FALSE;
}
#endif // __linux__