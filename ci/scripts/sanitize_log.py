#!/usr/bin/env python3
"""Remove local checkout/work absolute paths from a CI log before upload."""
import re
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 4:
        print("usage: sanitize_log.py <log> <repo> <work>", file=sys.stderr)
        return 2
    path, repo, work = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
    text = path.read_text(errors="replace")
    text = text.replace(repo, "$REPO")
    text = text.replace(work, "$CI_WORK")
    text = re.sub(r"/Volumes/T2(?:/[^\s:\"']+)?", "$EXTERNAL", text)
    text = re.sub(r"/Users/[^\s:\"']+", "$HOME", text)
    text = re.sub(r"/home/[^\s:\"']+", "$HOME", text)
    path.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
