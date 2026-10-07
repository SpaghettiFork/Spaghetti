#!/usr/bin/env python3
"""Intersect a GCC warning log with added diff lines for the warning gate.

Reads a build log, keeps warnings whose file/line falls on a line added
by the diff under review, and emits GitHub workflow commands for the top
ranked findings plus a markdown summary table covering all of them.

Ranking puts analyzer lifetime/bounds findings first, then the targeted
-W options, then anything else alphabetically.

Exit status is always 0: the gate is advisory and must never red a run.
"""

import argparse
import os
import re
import sys

WARNING_RE = re.compile(
    r"^(?P<path>\S+?):(?P<line>\d+):(?:\d+:)?\s+warning:\s+"
    r"(?P<text>.*?)\s+\[(?P<flag>-W[^\]]+)\]\s*$"
)
HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,(?P<count>\d+))? @@")
NEW_FILE_RE = re.compile(r"^\+\+\+ b/(?P<path>.*)$")

MAX_ANNOTATIONS_DEFAULT = 10

# Lower rank value sorts first.
FLAG_RANK = {
    "-Wanalyzer-use-after-free": 0,
    "-Wanalyzer-double-free": 0,
    "-Wanalyzer-out-of-bounds": 1,
    "-Wanalyzer-use-of-uninitialized-value": 1,
    "-Wuse-after-free": 2,
    "-Warray-bounds": 3,
    "-Wstringop-overflow": 3,
    "-Walloc-zero": 4,
    "-Wdangling-pointer": 5,
}


def flag_rank(flag):
    base = flag.split("=")[0]
    if base in FLAG_RANK:
        return FLAG_RANK[base]
    if base.startswith("-Wanalyzer-"):
        return 6
    return 7


def repo_relative(raw_path, root, build_dir):
    """Resolve a compiler-log path to repo-relative form, or None."""
    # Strip ninja's ../../../ prefix chains by resolving against build dir.
    candidates = [raw_path]
    if not os.path.isabs(raw_path):
        candidates.append(os.path.normpath(os.path.join(build_dir, raw_path)))
    for cand in candidates:
        abs_cand = os.path.abspath(cand)
        try:
            rel = os.path.relpath(abs_cand, root)
        except ValueError:
            continue
        if not rel.startswith(".."):
            return rel
    # System headers and generated files outside the tree are out of scope.
    return None


def parse_log(log_path, root, build_dir):
    findings = []
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = WARNING_RE.match(line.rstrip("\n"))
            if not match:
                continue
            rel = repo_relative(match.group("path"), root, build_dir)
            if rel is None:
                continue
            findings.append(
                {
                    "path": rel,
                    "line": int(match.group("line")),
                    "flag": match.group("flag"),
                    "text": match.group("text").strip(),
                }
            )
    return findings


def parse_diff(diff_path):
    """Return the set of (path, added line number) from a -U0 diff."""
    added = set()
    current_path = None
    new_line = 0
    with open(diff_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            file_match = NEW_FILE_RE.match(line)
            if file_match:
                current_path = file_match.group("path")
                continue
            hunk_match = HUNK_RE.match(line)
            if hunk_match:
                new_line = int(hunk_match.group("start"))
                continue
            if current_path is None:
                continue
            if line.startswith("+") and not line.startswith("+++"):
                added.add((current_path, new_line))
                new_line += 1
            elif line.startswith("-") and not line.startswith("---"):
                continue
            else:
                new_line += 1
    return added


def escape_command(value):
    return (
        value.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", required=True, help="build log to scan")
    parser.add_argument("--diff", required=True, help="git diff -U0 output")
    parser.add_argument("--root", default=".", help="repository root")
    parser.add_argument("--build-dir", default=".",
                        help="directory ninja ran in")
    parser.add_argument("--max", type=int, default=MAX_ANNOTATIONS_DEFAULT,
                        help="maximum inline annotations to emit")
    parser.add_argument("--commands", default="-",
                        help="where to write workflow commands (- = stdout)")
    parser.add_argument("--summary", default="-",
                        help="where to write markdown summary (- = stdout)")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    findings = parse_log(args.log, root, os.path.abspath(args.build_dir))
    added = parse_diff(args.diff)

    seen = set()
    scoped = []
    for finding in findings:
        key = (finding["path"], finding["line"], finding["flag"],
               finding["text"])
        if key in seen:
            continue
        seen.add(key)
        if (finding["path"], finding["line"]) in added:
            scoped.append(finding)

    scoped.sort(key=lambda f: (flag_rank(f["flag"]), f["path"], f["line"]))

    commands = []
    for finding in scoped[: args.max]:
        message = "%s [%s]" % (finding["text"], finding["flag"])
        commands.append(
            "::warning file=%s,line=%d,title=%s::%s"
            % (finding["path"], finding["line"],
               escape_command(finding["flag"]), escape_command(message))
        )

    summary = [
        "## Compiler warnings (advisory)",
        "",
        "New warnings on added lines: **%d** (%d shown inline)."
        % (len(scoped), min(len(scoped), args.max)),
        "",
    ]
    if scoped:
        summary += ["| File | Line | Warning | Detail |",
                    "| --- | --- | --- | --- |"]
        for finding in scoped:
            summary.append("| %s | %d | `%s` | %s |"
                           % (finding["path"], finding["line"],
                              finding["flag"], finding["text"]))

    def emit(dest, lines):
        if dest == "-":
            sys.stdout.write("\n".join(lines) + ("\n" if lines else ""))
            return
        with open(dest, "a", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + ("\n" if lines else ""))

    emit(args.commands, commands)
    if args.summary == args.commands and args.summary != "-":
        emit(args.summary, [""] + summary)
    else:
        emit(args.summary, summary)

    return 0


if __name__ == "__main__":
    sys.exit(main())
