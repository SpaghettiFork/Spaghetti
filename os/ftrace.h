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
#ifndef _FTRACE_H_
#define _FTRACE_H_

#include "misc.h"

extern _X_EXPORT Bool
ftrace_enable(Bool enable);

#if FTRACE
extern _X_EXPORT void ftrace_print(const char* f, ...);

extern _X_EXPORT void ftrace_print_begin(unsigned long ctx, const char *f, ...);

extern _X_EXPORT void ftrace_print_end(unsigned long ctx, const char *f, ...);
#else
#define ftrace_print(...)       ((void)0)
#define ftrace_print_begin(...) ((void)0)
#define ftrace_print_end(...)   ((void)0)
#endif

#endif // _FTRACE_H_