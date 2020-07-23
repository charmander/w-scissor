/* wl-clipboard
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

void trim_trailing_newline(const char *file_path) {
    int fd = open(file_path, O_RDWR);
    if (fd < 0) {
        perror("open file for trimming");
        return;
    }

    int seek_res = lseek(fd, -1, SEEK_END);
    if (seek_res < 0 && errno == EINVAL) {
        /* It was an empty file */
        goto out;
    } else if (seek_res < 0) {
        perror("lseek");
        goto out;
    }
    /* If the seek was successful, seek_res is the
     * new file size after trimming the newline.
     */
    char last_char;
    int read_res = read(fd, &last_char, 1);
    if (read_res != 1) {
        perror("read");
        goto out;
    }
    if (last_char != '\n') {
        goto out;
    }

    int rc = ftruncate(fd, seek_res);
    if (rc < 0) {
        perror("ftruncate");
    }
out:
    close(fd);
}

/* Returns the name of a new file */
char *dump_stdin_into_a_temp_file() {
    /* Create a temp directory to host out file */
    char dirpath[] = "/tmp/wl-copy-buffer-XXXXXX";
    if (mkdtemp(dirpath) != dirpath) {
        perror("mkdtemp");
        exit(1);
    }

    char const *name = "stdin";

    /* Construct the path */
    char *res_path = malloc(strlen(dirpath) + 1 + strlen(name) + 1);
    memcpy(res_path, dirpath, sizeof(dirpath));
    res_path[sizeof(dirpath) - 1] = '/';
    strcpy(res_path + sizeof(dirpath), name);

    /* Spawn cat to perform the copy */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        int fd = creat(res_path, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            perror("creat");
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
        execlp("cat", "cat", NULL);
        perror("exec cat");
        exit(1);
    }

    int wstatus;
    wait(&wstatus);
    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
        bail("Failed to copy the file");
    }
    return res_path;
}
