#!/usr/bin/env python3
"""Aggregate-only DPI-shape classifier for Slipstream stats lines.

Reads `dpi-stats:` lines from the Rust client and from the Owenclave DNS
adapter. Never prints QNAMEs or payloads.
"""

import argparse
import re
import statistics
import sys

FIELD = re.compile(r"([a-zA-Z0-9_]+)=([^\s]+)")


def parse(lines):
    sources = {}
    for raw in lines:
        if "dpi-stats:" not in raw:
            continue
        payload = raw.split("dpi-stats:", 1)[1]
        fields = {key: value for key, value in FIELD.findall(payload)}
        if not fields:
            continue
        source = fields.get("source", "tunnel")
        values = sources.setdefault(source, {})
        for key, value in fields.items():
            if key == "source":
                continue
            try:
                number = float(value)
            except ValueError:
                continue
            values.setdefault(key, []).append(number)
    return sources


def total(values, key):
    return sum(values.get(key, []))


def median(values, key, default=0.0):
    items = values.get(key, [])
    return statistics.median(items) if items else default


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*", help="log files (default: stdin)")
    args = parser.parse_args()
    if args.files:
        lines = []
        for path in args.files:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                lines.extend(handle)
    else:
        lines = sys.stdin

    sources = parse(lines)
    if not sources:
        raise SystemExit("no dpi-stats lines found")

    print(
        f"{'source':8} {'n':>4} {'qps':>8} {'txt%':>6} {'q50':>5} {'q95':>5} "
        f"{'l57%':>6} {'uniq/m':>7} {'lanes':>6} {'id+1%':>6} {'binTXT%':>8}  flags"
    )
    for source, values in sorted(sources.items()):
        qtypes = {
            "a": total(values, "qtype_a"),
            "aaaa": total(values, "qtype_aaaa"),
            "txt": total(values, "qtype_txt"),
            "https": total(values, "qtype_https"),
            "other": total(values, "qtype_other"),
        }
        qtype_total = sum(qtypes.values())
        txt_share = qtypes["txt"] / qtype_total if qtype_total else 0.0
        labels_long = total(values, "labels_long")
        label_share = (
            total(values, "label_56_58") / labels_long if labels_long else 0.0
        )
        id_total = total(values, "id_total")
        id_share = total(values, "id_delta1") / id_total if id_total else 0.0
        txt_total = total(values, "txt_total")
        binary_share = total(values, "txt_binary") / txt_total if txt_total else 0.0
        qps = median(values, "qps")
        q50 = median(values, "qname_p50")
        q95 = median(values, "qname_p95")
        unique = median(values, "unique_per_min")
        lanes = median(values, "lanes")

        flags = []
        if txt_share > 0.20:
            flags.append("H1-TXT")
        if q50 > 100 or q95 > 200:
            flags.append("H2-LEN")
        if label_share > 0.50:
            flags.append("H3-LABEL57")
        if unique > 30:
            flags.append("H4-UNIQ")
        if lanes > 4:
            flags.append("H5-LANES")
        if id_share > 0.50:
            flags.append("H6-ID1")
        if binary_share > 0.50:
            flags.append("H7-BINARY")
        if qps > 20:
            flags.append("H8-QPS")

        print(
            f"{source:8} {len(values.get('queries', [])):4d} {qps:8.2f} "
            f"{txt_share * 100:6.1f} {q50:5.0f} {q95:5.0f} {label_share * 100:6.1f} "
            f"{unique:7.1f} {lanes:6.0f} {id_share * 100:6.1f} {binary_share * 100:8.1f}  "
            f"{','.join(flags) if flags else 'ok'}"
        )


if __name__ == "__main__":
    main()
