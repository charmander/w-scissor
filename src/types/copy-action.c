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

#include "types/copy-action.h"
#include "types/device.h"
#include "types/source.h"
#include "types/popup-surface.h"

#include "util/misc.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

static void do_set_selection(struct copy_action *self, uint32_t serial) {
    /* Set the selection and make sure it reaches
     * the display before we do anything else,
     * such as destroying the surface or exiting.
     */
    device_set_selection(self->device, self->source, serial, self->primary);
    wl_display_roundtrip(self->device->wl_display);

    /* Now, if we have used a popup surface, destroy it */
    if (self->device->needs_popup_surface) {
        popup_surface_destroy(self->popup_surface);
        self->popup_surface = NULL;
    }

    /* And invoke the callback */
    if (self->did_set_selection_callback != NULL) {
        self->did_set_selection_callback(self);
    }
}

static void on_focus(
    struct popup_surface *popup_surface,
    uint32_t serial
) {
    struct copy_action *self = (struct copy_action *) popup_surface->data;
    do_set_selection(self, serial);
}

__attribute__ ((nonnull, warn_unused_result))
static bool write_with_retry(
    int const fd,
    uint8_t const* const buf,
    size_t const nbyte
) {
    size_t remaining = nbyte;
    uint8_t const* buf_ptr = buf;

    while (remaining != 0) {
        ssize_t const w = write(fd, buf_ptr, remaining);

        if (w == -1) {
            if (errno == EINTR) {
                continue;
            }

            return false;
        }

        remaining -= (size_t)w;
        buf_ptr += w;
    }

    return true;
}

static void do_send(struct source *source, const char *mime_type, int fd) {
    struct copy_action* self = source->data;
    int const fd_to_copy = self->fd_to_copy;
    uint8_t const* const bytes_to_copy = self->bytes_to_copy;

     /* Unset O_NONBLOCK */
    fcntl(fd, F_SETFL, 0);

    if (fd_to_copy != -1) {
        for (;;) {
            uint8_t buf[8192];

            ssize_t const r = read(fd_to_copy, buf, sizeof buf);

            if (r == -1) {
                if (errno == EINTR) {
                    continue;
                }

                perror("read from file to copy failed");
                close(fd);
                return;
            }

            if (r == 0) {
                break;
            }

            if (!write_with_retry(fd, buf, (size_t)r)) {
                perror("write of file to copy failed");
                close(fd);
                return;
            }
        }
    } else if (bytes_to_copy != NULL) {
        if (!write_with_retry(fd, bytes_to_copy, self->bytes_to_copy_len)) {
            perror("write of bytes to copy failed");
            close(fd);
            return;
        }
    } else {
        bail("Unreachable: nothing to copy");
    }

    close(fd);


    if (self->pasted_callback != NULL) {
        self->pasted_callback(self);
    }
}

static void forward_cancel(struct source *source) {
    struct copy_action *self = source->data;
    if (self->cancelled_callback != NULL) {
        self->cancelled_callback(self);
    }
}

void copy_action_init(struct copy_action *self) {
    if (self->source != NULL) {
        self->source->send_callback = do_send;
        self->source->cancelled_callback = forward_cancel;
        self->source->data = self;
    }
    /* See if we can just set the selection directly */
    if (!self->device->needs_popup_surface) {
        /* If we can, it doesn't actually require
         * a serial, so passing zero will do.
         */
        do_set_selection(self, 0);
    } else {
        /* If we cannot, schedule to do it later,
         * when our popup surface gains keyboard focus.
         */
        self->popup_surface->on_focus = on_focus;
        self->popup_surface->data = self;
        popup_surface_init(self->popup_surface);
    }
}
