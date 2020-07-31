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
#include "types/source.h"
#include "types/device.h"
#include "types/device-manager.h"
#include "types/registry.h"
#include "types/popup-surface.h"

#include "util/files.h"
#include "util/string.h"
#include "util/misc.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <getopt.h>

static struct {
    int clear;
    char *mime_type;
    int buffer;
    int primary;
    const char *seat_name;
} options;

struct buffer {
    uint8_t* bytes;
    size_t length;
};

enum read_error {
    READ_ERROR_NONE,
    READ_ERROR_MALLOC,
    READ_ERROR_ERRNO,
};

__attribute__ ((nonnull, warn_unused_result))
static enum read_error read_to_end(int const fd, struct buffer* const buf) {
    size_t capacity = 8192;
    size_t length = 0;
    uint8_t* bytes = malloc(capacity);

    if (bytes == NULL) {
        return READ_ERROR_MALLOC;
    }

    for (;;) {
        if (length == capacity) {
            if ((size_t)SSIZE_MAX - capacity / 2 > capacity) {
                free(bytes);
                return READ_ERROR_MALLOC;
            }

            capacity += capacity / 2;

            uint8_t* const resized_bytes = realloc(bytes, capacity);

            if (resized_bytes == NULL) {
                free(bytes);
                return READ_ERROR_MALLOC;
            }

            bytes = resized_bytes;
        }

        ssize_t const r = read(fd, &bytes[length], capacity - length);

        if (r == -1) {
            if (errno == EAGAIN) {
                continue;
            }

            free(bytes);
            return READ_ERROR_ERRNO;
        }

        if (r == 0) {
            break;
        }

        length += (size_t)r;
    }

    buf->bytes = bytes;
    buf->length = length;
    return READ_ERROR_NONE;
}

static void did_set_selection_callback(struct copy_action *copy_action) {
    if (options.clear) {
        exit(0);
    }
}

static void cancelled_callback(struct copy_action *copy_action) {
    exit(0);
}

static void pasted_callback(struct copy_action *copy_action) {
    if (!options.buffer) {
        exit(0);
    }
}

static void print_usage(FILE *f, const char *argv0) {
    fprintf(
        f,
        "Usage:\n"
        "\t%s [options] < file-to-copy\n\n"
        "Copy content to the Wayland clipboard.\n\n"
        "Options:\n"
        "\t-b, --buffer\t\tServe multiple paste requests by buffering input in memory.\n"
        "\t-c, --clear\t\tInstead of copying anything, clear the clipboard.\n"
        "\t-p, --primary\t\tUse the \"primary\" clipboard.\n"
        "\t-t, --type mime/type\t"
        "Set the MIME type for the content.\n"
        "\t-s, --seat seat-name\t"
        "Pick the seat to work with.\n"
        "\t-v, --version\t\tDisplay version info.\n"
        "\t-h, --help\t\tDisplay this message.\n"
        "Mandatory arguments to long options are mandatory"
        " for short options too.\n\n"
        "See w-scissor(1) for more details.\n",
        argv0
    );
}

static void parse_options(int argc, argv_t argv) {
    if (argc < 1) {
        bail("Empty argv");
    }

    static struct option long_options[] = {
        {"version", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {"primary", no_argument, 0, 'p'},
        {"buffer", no_argument, 0, 'b'},
        {"clear", no_argument, 0, 'c'},
        {"type", required_argument, 0, 't'},
        {"seat", required_argument, 0, 's'},
        {0, 0, 0, 0}
    };
    while (1) {
        int option_index;
        const char *opts = "vhpbct:s:";
        int c = getopt_long(argc, argv, opts, long_options, &option_index);
        if (c == -1) {
            break;
        }
        if (c == 0) {
            c = long_options[option_index].val;
        }
        switch (c) {
        case 'v':
            print_version_info();
            exit(0);
        case 'h':
            print_usage(stdout, argv[0]);
            exit(0);
        case 'p':
            options.primary = 1;
            break;
        case 'b':
            options.buffer = 1;
            break;
        case 'c':
            options.clear = 1;
            break;
        case 't':
            options.mime_type = strdup(optarg);
            break;
        case 's':
            options.seat_name = strdup(optarg);
            break;
        default:
            /* getopt has already printed an error message */
            print_usage(stderr, argv[0]);
            exit(1);
        }
    }

    if (optind != argc) {
        fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
        print_usage(stderr, argv[0]);
        exit(1);
    }
}

int main(int argc, argv_t argv) {
    parse_options(argc, argv);

    struct wl_display *wl_display = wl_display_connect(NULL);
    if (wl_display == NULL) {
        bail("Failed to connect to a Wayland server");
    }

    struct registry *registry = calloc(1, sizeof(struct registry));
    registry->wl_display = wl_display;
    registry_init(registry);

    /* Wait for the initial set of globals to appear */
    wl_display_roundtrip(wl_display);

    struct seat *seat = registry_find_seat(registry, options.seat_name);
    if (seat == NULL) {
        if (options.seat_name != NULL) {
            bail("No such seat");
        } else {
            bail("Missing a seat");
        }
    }

    /* Create the device */
    struct device_manager *device_manager
        = registry_find_device_manager(registry, options.primary);
    if (device_manager == NULL) {
        complain_about_selection_support(options.primary);
    }

    struct device *device = device_manager_get_device(device_manager, seat);

    if (!device_supports_selection(device, options.primary)) {
        complain_about_selection_support(options.primary);
    }

    /* Create and initialize the copy action */
    struct copy_action *copy_action = calloc(1, sizeof(struct copy_action));
    copy_action->device = device;
    copy_action->primary = options.primary;
    copy_action->fd_to_copy = -1;

    if (!options.clear) {
        if (options.buffer) {
            struct buffer buf;

            switch (read_to_end(STDIN_FILENO, &buf)) {
            case READ_ERROR_NONE:
                break;

            case READ_ERROR_MALLOC:
                fputs("read from file to copy failed: allocation failed\n",
                stderr);
                return EXIT_FAILURE;

            case READ_ERROR_ERRNO:
                perror("read from file to copy failed");
                return EXIT_FAILURE;
            }

            copy_action->bytes_to_copy = buf.bytes;
            copy_action->bytes_to_copy_len = buf.length;
        } else {
            copy_action->fd_to_copy = STDIN_FILENO;
        }

        /* Create the source */
        copy_action->source = device_manager_create_source(device_manager);
        if (options.mime_type != NULL) {
            source_offer(copy_action->source, options.mime_type);
        }
        if (options.mime_type == NULL || mime_type_is_text(options.mime_type)) {
            /* Offer a few generic plain text formats */
            source_offer(copy_action->source, text_plain);
            source_offer(copy_action->source, text_plain_utf8);
            source_offer(copy_action->source, "TEXT");
            source_offer(copy_action->source, "STRING");
            source_offer(copy_action->source, "UTF8_STRING");
        }
        free(options.mime_type);
        options.mime_type = NULL;
    }

    if (device->needs_popup_surface) {
        copy_action->popup_surface = calloc(1, sizeof(struct popup_surface));
        copy_action->popup_surface->registry = registry;
        copy_action->popup_surface->seat = seat;
    }

    copy_action->did_set_selection_callback = did_set_selection_callback;
    copy_action->pasted_callback = pasted_callback;
    copy_action->cancelled_callback = cancelled_callback;
    copy_action_init(copy_action);

    while (wl_display_dispatch(wl_display) >= 0);

    perror("wl_display_dispatch");
    return 1;
}
