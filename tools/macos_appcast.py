#!/usr/bin/env python3
"""The macOS app's appcast (port/macos/README.md, "Updates"): appcast.xml,
which a release carries beside its OpenH1.dmg, and which the release apps
ask for at https://github.com/OpenH1/goath1/releases/latest/download/appcast.xml
(Sparkle's SUFeedURL, tools/macos_build.py's SPARKLE_FEED).

    python3 tools/macos_appcast.py --build 412 --url <the release's OpenH1.dmg> \\
        --signature <sign_update's output> --notes <file> --release-page <url> --output appcast.xml

One item, the release's: its build number as the version Sparkle compares
with the app's CFBundleVersion, the disk image's URL, its EdDSA signature
and length (Sparkle's sign_update, run in CI's macOS job with the
SPARKLE_PRIVATE_KEY secret: tools/setup_sparkle_key.sh), the oldest macOS
it runs on, and the release notes (one change a line in --notes) the update
window shows.
"""

import argparse
import email.utils
import html
import re
import sys
from pathlib import Path
from xml.sax.saxutils import escape, quoteattr

MINIMUM_SYSTEM = "14.4"  # (tools/macos_build.py's MACOS_MINIMUM)
SIGNATURE = re.compile(r'sparkle:edSignature="([A-Za-z0-9+/=]+)"\s+length="([0-9]+)"')


def notes_html(lines: list, release_page: str) -> str:
    items = "".join(f"<li>{html.escape(line)}</li>" for line in lines)
    body = f"<ul>{items}</ul>" if items else "<p>Fixes and improvements.</p>"
    link = f'<p><a href="{html.escape(release_page, quote=True)}">The release on GitHub</a></p>' if release_page else ""
    return f"<h2>What's new</h2>{body}{link}"


def appcast(build: int, url: str, signature: str, length: str, notes: str, release_page: str,
            minimum: str = MINIMUM_SYSTEM) -> str:
    version = str(build)
    full_notes = f"\n      <sparkle:fullReleaseNotesLink>{escape(release_page)}</sparkle:fullReleaseNotesLink>" \
        if release_page else ""
    return f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>GOAT H1</title>
    <link>https://github.com/OpenH1/goath1/releases</link>
    <language>en</language>
    <item>
      <title>Build {version}</title>
      <pubDate>{email.utils.formatdate(usegmt=True)}</pubDate>
      <sparkle:version>{version}</sparkle:version>
      <sparkle:shortVersionString>{version}</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>{escape(minimum)}</sparkle:minimumSystemVersion>{full_notes}
      <description><![CDATA[{notes.replace("]]>", "]]&gt;")}]]></description>
      <enclosure url={quoteattr(url)} type="application/octet-stream" sparkle:edSignature={quoteattr(signature)} length="{length}"/>
    </item>
  </channel>
</rss>
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build", type=int, required=True, help="the release's build number")
    parser.add_argument("--url", required=True, help="the release's OpenH1.dmg")
    parser.add_argument("--signature", required=True,
                        help='a file of sign_update\'s output: sparkle:edSignature="..." length="..."')
    parser.add_argument("--notes", help="a file of the release's changes, one a line")
    parser.add_argument("--release-page", default="", help="the release's page")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    found = SIGNATURE.search(Path(args.signature).read_text(encoding="utf-8"))
    if not found:
        sys.exit(f"{args.signature}: no sparkle:edSignature and length")
    if args.build <= 0:
        sys.exit("--build: a release's number")
    lines = []
    if args.notes:
        lines = [line.strip() for line in Path(args.notes).read_text(encoding="utf-8").splitlines() if line.strip()]
    text = appcast(args.build, args.url, found.group(1), found.group(2), notes_html(lines[:40], args.release_page),
                   args.release_page)
    Path(args.output).write_text(text, encoding="utf-8")
    print(f"{args.output}: build {args.build}, {args.url}")


if __name__ == "__main__":
    main()
