/**
 * Spaghetti Display Server
 * Copyright (C) 2026  SpaghettiFork
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xf86.h"
#include "xf86_OSlib.h"
#include "linux.h"

#define CGROUP_PATH "/sys/fs/cgroup/spaghetti"

Bool nocgroup = FALSE;
static Bool cgroup_active = FALSE;

void
xf86cgroupSetup(void)
{
    int fd;
    char buf[32];
    ssize_t len;
    struct stat st;

    if (nocgroup)
        return;

    if (geteuid() != 0)
        return;

    if (stat(CGROUP_PATH, &st) != 0) {
        if (mkdir(CGROUP_PATH, 0755) != 0) {
            xf86Msg(X_WARNING, "cgroup: failed to create %s: %s\n",
                    CGROUP_PATH, strerror(errno));
            return;
        }
    }

    fd = open(CGROUP_PATH "/cgroup.procs", O_WRONLY);
    if (fd < 0) {
        xf86Msg(X_WARNING, "cgroup: failed to open cgroup.procs: %s\n",
                strerror(errno));
        return;
    }

    len = snprintf(buf, sizeof(buf), "%d\n", getpid());
    if (write(fd, buf, len) != len) {
        xf86Msg(X_WARNING, "cgroup: failed to write PID to cgroup.procs: %s\n",
                strerror(errno));
        close(fd);
        return;
    }

    close(fd);

    fd = open(CGROUP_PATH "/cpu.weight.nice", O_WRONLY);
    if (fd < 0) {
        xf86Msg(X_WARNING, "cgroup: failed to open cpu.weight.nice: %s\n",
                strerror(errno));
        return;
    }

    if (write(fd, "-4", 3) != 3) {
        xf86Msg(X_WARNING, "cgroup: failed to write cpu.weight.nice: %s\n",
                strerror(errno));
        close(fd);
        return;
    }
    close(fd);

    fd = open(CGROUP_PATH "/cpu.uclamp.min", O_WRONLY);
    if (fd < 0) {
        xf86Msg(X_WARNING, "cgroup: failed to open cpu.uclamp.min: %s\n",
                strerror(errno));
        goto skip_uclamp;
    }

    if (write(fd, "10", 3) != 3) {
        xf86Msg(X_WARNING, "cgroup: failed to write cpu.uclamp.min: %s\n",
                strerror(errno));
        return;
    }
    close(fd);
skip_uclamp:

    cgroup_active = TRUE;
    xf86Msg(X_CONFIG, "cgroup: assigned to %s with cpu.weight.nice=-4 cpu.uclamp.min=10\n",
            CGROUP_PATH);
}

void
xf86cgroupCleanup(void)
{
    int fd;
    char buf[4096];
    ssize_t len;
    int pid_count = 0;
    char *p;

    if (!cgroup_active)
        return;

    fd = open(CGROUP_PATH "/cgroup.procs", O_RDONLY);
    if (fd < 0) {
        xf86Msg(X_WARNING, "cgroup: failed to open cgroup.procs for cleanup: %s\n",
                strerror(errno));
        cgroup_active = FALSE;
        return;
    }

    len = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (len > 0) {
        buf[len] = '\0';
        for (p = buf; *p; p++) {
            if (*p == '\n')
                pid_count++;
        }
    }

    if (pid_count > 1) {
        xf86Msg(X_WARNING, "cgroup: %s has %d processes, not removing\n",
                CGROUP_PATH, pid_count);
    } else {
        if (rmdir(CGROUP_PATH) != 0) {
            xf86Msg(X_WARNING, "cgroup: failed to remove %s: %s\n",
                    CGROUP_PATH, strerror(errno));
        } else {
            xf86Msg(X_CONFIG, "cgroup: removed %s\n", CGROUP_PATH);
        }
    }

    cgroup_active = FALSE;
}
