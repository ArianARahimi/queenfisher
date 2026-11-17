# QFisher

Free media downloader for Windows, macOS and Linux. Paste a link, pick a quality, done.

QFisher is a window around [yt-dlp](https://github.com/yt-dlp/yt-dlp), which does the actual downloading.

- Free and open source (GPLv3)
- No ads, no tracking, no account, nothing bundled

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

- The preview (picture, title, length) isn't built yet.
- Uses the yt-dlp installed on your computer; QFisher doesn't fetch or update it yet.
- When a site asks for a sign-in, QFisher retries once with your default browser's cookies. That only works on Linux so far.
- Before the sound part of a video starts, its size is guessed, so the bar can pause briefly when it starts.
- Only checked on Linux (KDE); the look on Windows and macOS is untested.

## License

[GPL-3.0-or-later](LICENSE)
