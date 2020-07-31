/* w-scissor
 *
 * Copyright © 2019 Sergey Bugaev <bugaevc@gmail.com>
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

#include "util/files.h"
#include "util/string.h"
#include "util/misc.h"

#include "config.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h> // open
#include <sys/stat.h> // open
#include <sys/types.h> // open
#include <stdlib.h> // exit
#include <libgen.h> // basename
#include <sys/wait.h>

#ifdef HAVE_MEMFD
#    include <sys/syscall.h> // syscall, SYS_memfd_create
#endif
#ifdef HAVE_SHM_ANON
#    include <sys/mman.h> // shm_open, SHM_ANON
#endif


int create_anonymous_file() {
    int res;
#ifdef HAVE_MEMFD
    res = syscall(SYS_memfd_create, "buffer", 0);
    if (res >= 0) {
        return res;
    }
#endif
#ifdef HAVE_SHM_ANON
    res = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
    if (res >= 0) {
        return res;
    }
#endif
    (void) res;
    return fileno(tmpfile());
}
