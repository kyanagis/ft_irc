#!/usr/bin/env python3

import json
import sys

METRICS = ("regions", "lines", "branches", "mcdc")

def main():
    if len(sys.argv) != 2:
        print("usage: coverage_gate.py <cov.json>", file=sys.stderr)
        sys.exit(2)

    d = json.load(open(sys.argv[1]))
    files = d["data"][0]["files"]

    bad = []
    header = "%-34s %8s %8s %8s %8s" % ("file", "region", "line", "branch", "mcdc")
    print(header)
    print("-" * len(header))
    for f in files:
        summary = f["summary"]
        name = f["filename"].split("/src/")[-1]
        cells = []
        for metric in METRICS:
            m = summary.get(metric, {"count": 0})
            if m["count"] == 0:
                cells.append("  -   ")
                continue
            cells.append("%6.2f%%" % m["percent"])
            if m["covered"] != m["count"]:
                bad.append("%s %s %d/%d" % (name, metric, m["covered"], m["count"]))
        print("%-34s %8s %8s %8s %8s" % (name, cells[0], cells[1], cells[2], cells[3]))
    print("-" * len(header))

    if bad:
        print("FAIL: uncovered -> " + "; ".join(bad))
        sys.exit(1)
    print("OK: every file 100%% region+line+branch+mcdc (%d files)" % len(files))

if __name__ == "__main__":
    main()
