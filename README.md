# Queenfisher

Free media downloader for Windows, macOS and Linux. Paste a link, pick a quality, done.

![Queenfisher downloading a few videos, with a preview of the next one](docs/screenshots/download.png)

Queenfisher is a window around [yt-dlp](https://github.com/yt-dlp/yt-dlp), which does the actual downloading.

- Free and open source (GPLv3)
- No ads, no tracking, no account, nothing bundled. Once a day it asks GitHub for the newest version number (Settings can turn that off)
- Video as MP4, MKV, WebM, MOV, AVI, FLV or GIF; sound as MP3, WAV, FLAC, OGG, M4A, ALAC or the site's original

## How to use it

1. Copy a video link and click **Paste** (or press Ctrl+Shift+V). A preview shows the picture, the length, the qualities the video really has and roughly how big the file will be.
2. Pick a format and a quality on the left. Qualities the video doesn't have are greyed out.
3. Click **Download**. Add as many as you like; they download one after another, into your Downloads folder unless you pick another one with **Change...**.

Finished downloads stay in the **Library** tab, where you can search them, open them or show them in their folder.

## Features

- YouTube previews appear almost at once; other sites take a few seconds.
- Remembers your format, quality, folder and extras between sessions.
- Extras: English subtitles, whole playlists, and skipping sponsor segments (SponsorBlock).
- Downloading the same video again asks whether to keep the file you have or overwrite it. Another quality is saved as its own file.
- Unfinished pieces stay out of your download folder, so only the finished file appears there.
- You only get the format you picked. Streams that can't become it aren't downloaded, and every finished file is checked with ffprobe: one that isn't the right container and codecs is deleted, and the download shows as failed.
- Sits in the system tray when minimized, with download progress on hover.

| Library | Sound formats |
| --- | --- |
| ![The Library tab with three finished downloads](docs/screenshots/library.png) | ![MP3 picked, with the quality options greyed out](docs/screenshots/audio.png) |

## Keeping yt-dlp working

Queenfisher keeps its own copy of yt-dlp and updates it for you.

- It downloads official yt-dlp releases from GitHub and only uses one after checking the yt-dlp developers' signature (key fingerprint `AC0C BBE6 848D 6A87 3464 AF4E 57CF 6593 3B5A 7581`) and the file's checksum.
- It checks for a new version once a day, between downloads.
- When a download fails in a way that usually means a site changed, it updates yt-dlp and tries again. If the newest release doesn't help, it asks whether to try yt-dlp's nightly build, which tends to get site fixes first, and goes back to the regular release once that catches up.
- The previous copy is kept, and a new one has to start and report its version before it replaces the old one.
- If a new yt-dlp breaks something, you can go back to the previous one by hand: close Queenfisher, then in its `bin` folder copy `yt-dlp.previous` over `yt-dlp` (`yt-dlp.exe.previous` over `yt-dlp.exe` on Windows). Queenfisher uses that copy and doesn't reinstall the version you left until a newer one comes out; **Check for yt-dlp Updates** installs the newest anyway. The `bin` folder is in `~/.local/share/Queenfisher/Queenfisher` on Linux, `%LOCALAPPDATA%\Queenfisher\Queenfisher` on Windows and `~/Library/Application Support/Queenfisher/Queenfisher` on macOS.
- A file put there that Queenfisher didn't check itself is never run; it fetches a checked copy instead. The same goes for the ffmpeg and Deno it fetches, which are checked again each time Queenfisher starts or their files change.
- It only ever runs its own checked copy, never a yt-dlp found elsewhere on the computer. Until that copy is in place, downloads wait.

## Download

Get the newest version from [Releases](https://github.com/ArianARahimi/queenfisher/releases).

- **Linux:** download `Queenfisher-<version>-x86_64.AppImage`, make it executable (`chmod +x`) and run it.
- **Windows:** download `Queenfisher-<version>-windows.exe` and run it. It's the whole program; nothing to install or unpack. The first time, Windows may say it protected your PC: click **More info**, then **Run anyway**.
- **macOS:** download the `-macos.zip` (Apple Silicon) or `-macos-intel.zip` (Intel), open it and open Queenfisher. The first time, macOS blocks it: System Settings → Privacy & Security → **Open Anyway**.

Queenfisher uses [ffmpeg](https://ffmpeg.org/) to join video and sound and to convert between formats. On Windows it fetches yt-dlp's own build the first time (about 90 MB); on Linux and macOS, install ffmpeg yourself. YouTube also needs [Deno](https://deno.com/): one you have installed is used, otherwise Queenfisher fetches the official build (about 45 MB). Each Queenfisher release names one exact build of each and carries its SHA-256, so a file that differs in any way is refused. Newer builds come with Queenfisher updates.

## Privacy

Queenfisher sends nothing about you anywhere. No ads, no accounts, no tracking. It only connects to:

- the sites whose links you paste
- YouTube's picture and title service, for YouTube previews
- GitHub, to fetch and update yt-dlp, Deno (when it isn't installed) and ffmpeg on Windows
- GitHub, once a day, to see if there's a newer Queenfisher (Settings: **Check for new versions once a day**)
- SponsorBlock, only if you turn on skipping sponsor segments

Browser sign-in is off until you turn it on in Settings (**Use my browser's sign-in when a site asks for one**). Then, the first time a site asks you to sign in, Queenfisher asks whether to use your default browser's cookies for that site. yt-dlp gets that site's cookies only, in a private file deleted after the download, and they go only to that site.

Your history, settings and partial downloads stay on your computer, in your own user folders.

## Building

Needs Qt 6.5 or newer, OpenSSL 3, CMake and a C++20 compiler.

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/queenfisher
```

To build the Linux AppImage (into `dist/`):

```
./packaging/linux/appimage.sh
```

To build the Windows `.exe` on Linux (MinGW, host Qt 6.12; builds a static Qt the first time):

```
./packaging/windows/build-static.sh
```

## Releases

GitHub Actions builds every push to `main`, checks each file on VirusTotal and puts it on the
"Latest build" pre-release for testing. A commit titled `Release vX.Y.Z`, with the same version in
`CMakeLists.txt`, also publishes that release. Nothing VirusTotal flags is published. The check
needs a VirusTotal API key in the repository secret `VT_API_KEY`.

## Known issues

- Fetching yt-dlp has been tested on Linux, Windows and macOS; updating it on Linux and Windows. Going back to the previous yt-dlp by hand has been tested on Linux and Windows.
- Checking the fetched ffmpeg and Deno again, and starting Windows' taskkill by its full path, haven't been tried on Windows or macOS yet. Keeping Queenfisher's folders private to the user has been tried on Linux and Windows (where your user folder already keeps other users out). The question about yt-dlp's nightly build and the check on thumbnail redirects have only been tested by automated tests so far. Copies fetched by an older Queenfisher are fetched once more, since they weren't noted file by file (tried on Windows).
- On Linux and macOS, ffmpeg must be installed; Queenfisher only fetches its own on Windows. On macOS it also looks in Homebrew's and MacPorts' folders, since apps opened from the Dock or Finder don't get the Terminal's search path. That's been checked, but not yet with a real Homebrew ffmpeg.
- Fetching Queenfisher's own Deno has been tried on Windows and macOS (Intel); on Linux (used only when no Deno is installed) it hasn't been tried yet.
- The Mac builds now use OpenSSL built from its pinned source instead of Homebrew's. Release builds no longer reuse the cached Windows Qt; that hasn't been through a release yet.
- The Mac apps aren't signed by Apple. The Intel build has been tried on macOS 26 (in a virtual machine); the Apple Silicon build hasn't been tried yet.
- Browser sign-in is off until you turn it on in Settings. Then, the first time a site asks for a sign-in, Queenfisher asks whether to use your default browser's cookies for that site, and remembers the answer until sign-in is turned off. yt-dlp then gets that site's cookies only, never the browser's whole set; that has been tried with a test browser profile on Linux so far, not with a real sign-in. Finding the default browser works on Linux and Windows; on macOS it hasn't been tried yet, and Safari's cookies need Full Disk Access for Queenfisher (System Settings, Privacy & Security). On Windows, Chrome and Edge often lock their cookies away from other programs, so yt-dlp can't use them; Firefox-based browsers work.
- If fetching yt-dlp, ffmpeg or Deno fails (say, the network isn't up yet right after logging in), Queenfisher tries again a minute later and only then reports it. Tried on Windows with Queenfisher's network blocked at startup.
- Opening Queenfisher again brings back the window that's already open. Whether that window comes to the front on Wayland, and on Windows, hasn't been tried yet.
- Clicking Download before the preview is in leaves the sound part's size to a guess, so the bar can jump a little when the sound starts.
- Checked on Linux (KDE), Windows 11 and macOS 26 (Intel, in a virtual machine). The window frame and title bar are the system's own.
- YouTube previews show the picture and title right away, but the qualities and size still take a few seconds. Other sites take a few seconds for everything, since yt-dlp has to ask the site.
- The tray icon has been tried on KDE and Windows 11 (where it starts in the hidden icons, under the ^). GNOME shows tray icons only with an extension; without one, minimizing works as usual.
- AVI is re-encoded so old players can open it, which is slow for long videos. GIF is offered only for videos up to a minute, and even those make big files.
- Skip sponsors only works on YouTube, and only for segments the SponsorBlock community has marked.

## Credits

Icons from the [Tango Desktop Project](http://tango.freedesktop.org/) (public domain), see `resources/icons/COPYING-tango.txt`.

The fish icon is from Microsoft's [Fluent UI Emoji](https://github.com/microsoft/fluentui-emoji) (MIT License), see `packaging/COPYING-fish-icon.txt`.

## License

[GPL-3.0-or-later](LICENSE)
