# w-scissor: Wayland clipboard utilities

This project implements two command-line Wayland clipboard utilities, `ws-copy`
and `ws-paste`, that let you easily copy data between the clipboard and Unix
pipes, sockets, files and so on.

Usage is as simple as:

```bash
# copy a simple text message
$ printf 'Hello world!' | ws-copy

# copy the list of files in Downloads
$ ls ~/Downloads | ws-copy

# copy an image file
$ ws-copy --type=image/png < ~/Pictures/photo.png

# paste to a file
$ ws-paste > clipboard.txt

# grep each pasted word in file source.c
$ for word in $(ws-paste); do grep $word source.c; done

# copy the previous command
$ fc -ln -- -1 | ws-copy

# replace the current selection with the list of types it's offered in
$ ws-paste --list-types | ws-copy
```

Although `ws-copy` and `ws-paste` are particularly optimized for plain text and
other textual content formats, they fully support content of arbitrary MIME
types. You can specify the type to use with the `--type` option.

# Options

For `ws-copy`:

* `-o`, `--paste-once` Only serve one paste request and then exit. Unless a clipboard manager specifically designed to prevent this is in use, this has the effect of clearing the clipboard after the first paste, which is useful for copying sensitive data such as passwords. Note that this may break pasting into some clients, in particular pasting into XWayland windows is known to break when this option is used.
* `-c`, `--clear` Instead of copying anything, clear the clipboard so that nothing is copied.

For `ws-paste`:

* `-l`, `--list-types` Instead of pasting the selection, output the list of MIME types it is offered in.
* `-w command...`, `--watch command...` Instead of pasting once and exiting, continuously watch the clipboard for changes, and run the specified command each time a new selection appears. The spawned process can read the clipboard contents from its standard input. This mode requires a compositor that supports the [wlroots data-control protocol](https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-data-control-unstable-v1.xml).

For both:

* `-p`, `--primary` Use the "primary" clipboard instead of the regular clipboard.
* `-t mime/type`, `--type mime/type` Set the MIME type for the content. For `ws-copy` this option controls which type `ws-copy` will offer the content as. For `ws-paste` it controls which of the offered types `ws-paste` will request the content in. In addition to specific MIME types such as _image/png_, `ws-paste` also accepts generic type names such as _text_ and _image_ which make it automatically pick some offered MIME type that matches the given generic name.
* `-s seat-name`, `--seat seat-name` Specify which seat `ws-copy` and `ws-paste` should work with. Wayland natively supports multi-seat configurations where each seat gets its own mouse pointer, keyboard focus, and among other things its own separate clipboard. The name of the default seat is likely _default_ or _seat0_, and additional seat names normally come form `udev(7)` property `ENV{WL_SEAT}`. You can view the list of the currently available seats as advertised by the compositor using the `weston-info(1)` tool. If you don't specify the seat name explicitly, `ws-copy` and `ws-paste` will pick a seat arbitrarily. If you are using a single-seat system, there is little reason to use this option.
* `-v`, `--version` Display the version of w-scissor and some short info about its license.
* `-h`, `--help` Display a short help message listing the available options.

# Building

w-scissor is a simple Meson project, so building it is just:

```bash
# clone
$ git clone https://github.com/charmander/w-scissor
$ cd w-scissor

# build
$ meson build
$ cd build
$ ninja

# install
$ sudo ninja install
```

w-scissor supports Linux and BSD systems, and is also known to work on
Mac OS X and GNU Hurd. The only mandatory dependency is the `wayland-client`
library (try package named `wayland-devel` or `libwayland-dev`).

Optional dependencies for building:
* `wayland-scanner`
* `wayland-protocols` (version 1.12 or later)

If these are found during configuration, w-scissor gets built with
additional protocols support, which enables features such as primary selection
support and `--watch` mode.

# License

w-scissor is free software, available under the GNU General Public License
version 3 or later.
