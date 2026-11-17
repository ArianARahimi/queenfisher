#!/usr/bin/env python3
"""Checks files on VirusTotal before they're published; fails if any engine flags one.

Usage: VT_API_KEY=<key> python3 -I tools/vt-check.py FILE...

Files VirusTotal hasn't seen are uploaded, as anyone downloading them would. The free
API allows 4 requests a minute, so new files take a few minutes.
"""
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request
import uuid

KEY = os.environ.get("VT_API_KEY", "").strip()
POLL = 30          # seconds between checks on a running analysis
MAX_POLLS = 60     # give up on one file after half an hour


def api(path, data=None, headers=()):
    for _ in range(30):
        url = path if path.startswith("https://") else "https://www.virustotal.com/api/v3/" + path
        req = urllib.request.Request(url, data,
                                     {"x-apikey": KEY, **dict(headers)})
        try:
            with urllib.request.urlopen(req, timeout=300) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            if e.code != 429:   # 429: over the free limit, wait and try again
                raise
        time.sleep(20)
    sys.exit("VirusTotal kept refusing requests (rate limit)")


def upload(name, data):
    boundary = uuid.uuid4().hex
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{name}\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n").encode() + data + f"\r\n--{boundary}--\r\n".encode()
    url = "files"
    if len(data) > 32 * 1024 * 1024:   # bigger files go to a one-off upload address, used as given
        url = api("files/upload_url")["data"]
    return api(url, body, {"Content-Type": f"multipart/form-data; boundary={boundary}"})["data"]["id"]


def check(path):
    """Returns a problem description, or None when no engine flags the file."""
    name = os.path.basename(path)
    data = open(path, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    link = f"https://www.virustotal.com/gui/file/{sha}"
    if api(f"files/{sha}") is None:
        analysis = upload(name, data)
        for _ in range(MAX_POLLS):
            time.sleep(POLL)
            status = api(f"analyses/{analysis}")   # None until VirusTotal lists it
            if status and status["data"]["attributes"]["status"] == "completed":
                break
        else:
            return f"{name}: VirusTotal didn't finish in time ({link})"
    # The file's own report can lag behind the analysis: wait for one with engines in it.
    attrs, stats = {}, {}
    for _ in range(20):
        report = api(f"files/{sha}")
        attrs = report["data"]["attributes"] if report else {}
        stats = attrs.get("last_analysis_stats", {})
        if attrs.get("last_analysis_date") and sum(stats.get(k, 0) for k in ("malicious", "suspicious", "harmless", "undetected")):
            break
        time.sleep(POLL)
    flags = {engine: r["result"] for engine, r in attrs.get("last_analysis_results", {}).items()
             if r["category"] in ("malicious", "suspicious")}
    total = sum(stats.get(k, 0) for k in ("malicious", "suspicious", "harmless", "undetected"))
    print(f"  {len(flags)}/{total}  {name}  {link}" + (f"  {flags}" if flags else ""), flush=True)
    if flags:
        return f"{name}: flagged by {', '.join(sorted(flags))} ({link})"
    if total == 0:
        return f"{name}: no engines reported ({link})"
    return None


def main():
    if not KEY:
        sys.exit("No VirusTotal API key (VT_API_KEY)")
    print("Checking every file on VirusTotal:", flush=True)
    problems = [p for p in (check(path) for path in sys.argv[1:]) if p]
    if problems:
        sys.exit("Not published. VirusTotal:\n  " + "\n  ".join(problems))


if __name__ == "__main__":
    main()
