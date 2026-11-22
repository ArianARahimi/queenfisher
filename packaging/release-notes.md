## What's new

### New

- One Queenfisher at a time: opening it again brings back the window that's already open.
- You only get the format you picked. Every finished file is checked, and one that isn't that format is removed. When a site has nothing that fits, Queenfisher says so instead of updating yt-dlp for nothing.

### Fixed

- File names on Windows keep full-width and other non-Latin characters.
- When a browser's cookies can't be read, Queenfisher says what to do instead of a vague error. No more doubled "Failed:" or repeated log lines.
- If fetching ffmpeg or Deno fails (say, the network isn't up yet right after logging in), Queenfisher tries again a minute later by itself. Before, it waited for your next click.

### Security

- Browser sign-in asks once per site before using your browser's cookies, and forgets those sites when you turn it off. yt-dlp then gets that one site's cookies, in a private file deleted after the download, never the browser's whole set.
- Site titles, channel names and notices are shown as plain text, so a site can't restyle the window with them.
- Thumbnails are only shown when they're real JPEG or PNG images, and are never fetched from your own computer or network.
- Text from a site can no longer trigger the sign-in question or the switch to yt-dlp's nightly build.
- yt-dlp runs with plugin folders turned off, so only the checked release's own code runs. Its signature must carry a date, can't be expired or dated in the future, and can't be older than the copy in use.
- An older yt-dlp release is never installed over a newer one, unless you switch channels yourself. If you put back the previous yt-dlp by hand, Queenfisher recognizes it and won't reinstall the version you left until a newer one comes out.
- ffmpeg and Deno are checked again each time Queenfisher starts or their files change. A changed file is fetched again, never run.
- Original audio only keeps the file types yt-dlp really makes for it (opus, m4a, mp3, ogg, flac, wav).
- On Windows, stopping a download runs Windows' own taskkill by its full path, and the single-instance check can't be taken over by another account.
- Your history, settings and partial downloads are kept in folders only your user can open.
