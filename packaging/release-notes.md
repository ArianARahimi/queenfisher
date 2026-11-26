## What's new

### New

- On Linux and macOS, the log says when the installed ffmpeg is older than 5.1, the oldest version ffmpeg still fixes. ffmpeg reads files from any site, so updating it is safer.

### Fixed

- Opening Queenfisher again brings the open window to the front on Wayland, X11 and Windows. A maximized window stays maximized.
- If ffmpeg or Deno was being updated when Queenfisher closed or crashed, the previous copy is put back at the next start instead of being fetched again.

### Security

- Words a site picks, like its error text or a page name taken from the link, can no longer bring up the sign-in question or start a yt-dlp update. Only yt-dlp's own messages can.
- Every file on the Releases page carries a signed build attestation. `gh attestation verify <file> --repo ArianARahimi/queenfisher` shows it was built by this repository's GitHub Actions and hasn't changed since.
- The Mac builds use Ninja from its own release, pinned by SHA-256, instead of Homebrew's latest.
