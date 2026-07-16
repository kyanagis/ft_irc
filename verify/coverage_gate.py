#!/usr/bin/env python3
# カバレッジゲート
#
# llvm-cov export の JSON を受け取り、含まれる全ファイルについて
#   region / line / branch を 100% 要求する（分岐が存在しないファイルの branch は対象外）。
# 純関数のパーサ/整形/文字列/状態機械/チャンネルロジックは全経路網羅を維持する契約。
#
# 使い方: python3 coverage_gate.py <cov.json>
# 未到達が 1 つでもあれば非ゼロ終了。

import json
import sys

METRICS = ("regions", "lines", "branches")


def main():
    if len(sys.argv) != 2:
        print("usage: coverage_gate.py <cov.json>", file=sys.stderr)
        sys.exit(2)

    d = json.load(open(sys.argv[1]))
    files = d["data"][0]["files"]

    bad = []
    print("%-34s %8s %8s %8s" % ("file", "region", "line", "branch"))
    print("-" * 62)
    for f in files:
        summary = f["summary"]
        name = f["filename"].split("/src/")[-1]
        cells = []
        for metric in METRICS:
            m = summary[metric]
            if m["count"] == 0:            # そのファイルに当該構造が無い（例: 分岐なし getter）
                cells.append("  -   ")
                continue
            cells.append("%6.2f%%" % m["percent"])
            if m["covered"] != m["count"]:
                bad.append("%s %s %d/%d" % (name, metric, m["covered"], m["count"]))
        print("%-34s %8s %8s %8s" % (name, cells[0], cells[1], cells[2]))
    print("-" * 62)

    if bad:
        print("FAIL: uncovered -> " + "; ".join(bad))
        sys.exit(1)
    print("OK: every file 100%% region+line+branch (%d files)" % len(files))


if __name__ == "__main__":
    main()
