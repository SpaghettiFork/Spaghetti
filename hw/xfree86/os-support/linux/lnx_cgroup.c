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

void
xf86SetupCGroup(void)
{
    int fd;
    char buf[32];
    ssize_t len;
    struct stat st;

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

    xf86Msg(X_CONFIG, "cgroup: assigned to %s successfully\n",
            CGROUP_PATH);
}