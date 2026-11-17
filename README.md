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

- Downloading and the preview aren't built yet: links only go into the list.
- Only checked on Linux (KDE); the look on Windows and macOS is untested.

## License

[GPL-3.0-or-later](LICENSE)
