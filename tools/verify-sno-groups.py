#!/usr/bin/env python3
"""verify-sno-groups.py - score SnoIndex.cpp's group table against the game's own.

WHY THIS EXISTS. The group id -> name map in src/index/SnoIndex.cpp is load-bearing in a way
that hides its own failures: all 18 callers of groupIdByName pass a hard-coded fallback, and the
map WINS over that fallback. A wrong label therefore does not degrade to the fallback, it
overrides it - groupIdByName("MarkingShape", 115) once returned 123 and the wardrobe's marking
scan walked 468 territory records instead of 374 markings. The symptom was an empty list, which
is indistinguishable from "this class has no markings".

That table was wrong in 85 of 132 entries for most of this project's life, and nothing caught it,
because the only names anything asks for happened to be among the correct ones. This script is
the regression guard that would have caught it on the next build.

GROUND TRUTH, and why it is trusted. d4data ships json/snoGroups.json, the game's own group
id -> name table. A shipped table is still a table, so it was scored two independent ways before
being adopted (see the comment block in SnoIndex.cpp): against 24 json/base/meta/<Folder>
directories, 22 of which agree exactly and neither exception is a contradiction; and against 64
distinct (__group__, groupName) pairs carried on the sno references inside 1,770 records, with
zero disagreements.

If a future game update renumbers a group, snoGroups.json moves first and this script will say so
before the mislabel can reach a caller.

Exit codes:  0 = agree (or nothing to check)   1 = a disagreement was found
A missing d4data snapshot is NOT a failure - it is reported and skipped, because the build must
not depend on a sparse checkout being present.
"""

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CPP = os.path.join(ROOT, "src", "index", "SnoIndex.cpp")

# Where the snapshot normally sits. The ini is the authority at runtime, but this is a
# build-time check and the default layout is the only one worth hard-coding; anything else
# can be passed on the command line.
DEFAULTS = [
    os.path.join(ROOT, "build", "release", "data", "d4data", "json", "snoGroups.json"),
    os.path.join(ROOT, "build", "debug", "data", "d4data", "json", "snoGroups.json"),
]


def find_truth(argv):
    if len(argv) > 1:
        p = argv[1]
        if os.path.isdir(p):
            p = os.path.join(p, "json", "snoGroups.json")
        return p if os.path.isfile(p) else None
    for p in DEFAULTS:
        if os.path.isfile(p):
            return p
    return None


def parse_cpp_table(text):
    """Read only the kMap initializer, not every {n,"x"} pair in the file.

    Scoped deliberately: this file's comments quote old table entries verbatim as a record of
    what was wrong, and a whole-file regex would score those comments as if they were code.
    """
    start = text.find("static const QHash<int, QString> kMap")
    if start < 0:
        return None
    end = text.find("return kMap", start)
    if end < 0:
        return None
    body = text[start:end]
    body = re.sub(r"//[^\n]*", "", body)          # strip trailing line comments
    out = {}
    for m in re.finditer(r'\{\s*(\d+)\s*,\s*"([^"]*)"\s*\}', body):
        out[int(m.group(1))] = m.group(2)
    return out


def norm(s):
    return s.lower()


def main():
    quiet = "--quiet" in sys.argv
    argv = [a for a in sys.argv if a != "--quiet"]

    if not os.path.isfile(CPP):
        print("verify-sno-groups: SnoIndex.cpp not found at %s" % CPP)
        return 0

    truth_path = find_truth(argv)
    if not truth_path:
        if not quiet:
            print("verify-sno-groups: no d4data snapshot found - skipped.")
            print("                   pass a path to snoGroups.json or to the d4data folder.")
        return 0

    with open(truth_path, "r", encoding="utf-8") as f:
        truth = {int(k): v for k, v in json.load(f).items()}

    with open(CPP, "r", encoding="utf-8", errors="replace") as f:
        table = parse_cpp_table(f.read())

    if table is None:
        print("verify-sno-groups: could not locate the kMap initializer in SnoIndex.cpp.")
        print("                   if the table moved, update this script rather than deleting it.")
        return 1

    wrong, missing_truth, dupes = [], [], []

    seen = {}
    for gid, name in sorted(table.items()):
        if name in seen:
            dupes.append((name, seen[name], gid))
        seen[name] = gid

    for gid, name in sorted(table.items()):
        t = truth.get(gid)
        if t is None:
            missing_truth.append((gid, name))
            continue
        # The table spells the snapshot's Unknown_<n> as "Unknown (n)" to match house style and
        # keep names unique for groupIdByName's walk. That is a deliberate rename, not a mismatch.
        if t.startswith("Unknown_") and name == "Unknown (%d)" % gid:
            continue
        if norm(t) != norm(name):
            wrong.append((gid, name, t))

    absent = sorted(g for g in truth if g > 0 and g not in table)

    ok = not wrong and not dupes
    if not quiet or not ok:
        print("verify-sno-groups: %d entries scored against %s"
              % (len(table), os.path.relpath(truth_path, ROOT)))

    if dupes:
        print("  DUPLICATE NAMES - groupIdByName's QHash walk is nondeterministic across builds:")
        for name, a, b in dupes:
            print('    "%s" is on both %d and %d' % (name, a, b))

    if wrong:
        print("  MISLABELLED - each of these overrides a caller's correct fallback:")
        print("    %-5s %-26s %s" % ("id", "SnoIndex.cpp says", "snoGroups.json says"))
        for gid, name, t in wrong:
            print("    %-5d %-26s %s" % (gid, name, t))

    if missing_truth and not quiet:
        print("  ids in the table with no entry in snoGroups.json (left alone, not an error):")
        print("    " + ", ".join("%d=%s" % (g, n) for g, n in missing_truth))

    if absent and not quiet:
        print("  %d group ids the game names and this table omits (callers get \"Group N\"):"
              % len(absent))
        print("    " + ", ".join(str(g) for g in absent))

    if ok:
        if not quiet:
            print("  OK - every entry agrees, and no name is on two ids.")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
