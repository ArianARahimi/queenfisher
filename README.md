# QFisher

Free media downloader for Windows, macOS and Linux. Paste a link, pick a quality, done.

QFisher is a window around [yt-dlp](https://github.com/yt-dlp/yt-dlp), which does the actual downloading.

- Free and open source (GPLv3)
- No ads, no tracking, no account, nothing bundled

## Keeping yt-dlp working

QFisher keeps its own copy of yt-dlp and updates it for you.

- It downloads official yt-dlp releases from GitHub and only uses one after checking the yt-dlp developers' signature (key fingerprint `AC0C BBE6 848D 6A87 3464 AF4E 57CF 6593 3B5A 7581`) and the file's checksum.
- It checks for a new version once a day, between downloads.
- When a download fails in a way that usually means a site changed, it updates yt-dlp and tries again. If the newest release doesn't help, it switches to yt-dlp's nightly build, which tends to get site fixes first, and goes back to the regular release once that catches up.
- The previous copy is kept, and a new one has to start and report its version before it replaces the old one.

## Status

Early development. Not ready to use yet.

## Building

Needs Qt 6.5 or newer, CMake and a C++20 compiler.

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/qfisher
```

## Known issues

- Fetching and updating yt-dlp has only been tested on Linux. Until QFisher's own copy is in place, it uses a yt-dlp installed on the computer, if there is one.
- YouTube needs Deno installed to unlock most videos. QFisher tells you when it's missing but doesn't install it yet.
- ffmpeg must be installed on Linux and macOS.
- When a site asks for a sign-in, QFisher retries once with your default browser's cookies. That only works on Linux so far.
- Before the sound part of a video starts, its size is guessed, so the bar can pause briefly when it starts.
- Only checked on Linux (KDE); the look on Windows and macOS is untested. The window frame and title bar are the system's own.
- Previews take a few seconds, since yt-dlp has to ask the site. The preview clip is fetched in the background right after, so clicking within the first few seconds may still show "Loading clip...". Clips need a site that allows downloading part of a video.

## Credits

Icons from the [Tango Desktop Project](http://tango.freedesktop.org/) (public domain), see `resources/icons/COPYING-tango.txt`.

## License

[GPL-3.0-or-later](LICENSE)
