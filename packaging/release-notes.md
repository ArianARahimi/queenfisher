## What's new

A security update.

- ffmpeg (Windows) and Deno are now pinned: each Queenfisher release names one exact build and carries its SHA-256, so a file that was changed on the server is refused. Before, the checksum came from the same place as the file. On Windows, ffmpeg is fetched once more after updating.
- Browser sign-in is now off until you turn it on in Settings. When a site wants a sign-in, Queenfisher says where to turn it on.
- Queenfisher only runs its own signature-checked yt-dlp, never one found elsewhere on the computer.
