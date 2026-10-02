#!/usr/bin/env python3
"""Parse EasyEDA Pro .epru: JSON objects delimited by | / ||."""
from __future__ import annotations

import json
import re
from collections import Counter, defaultdict
from pathlib import Path

SRC = Path(
    r"c:\Users\xijie\Documents\Codex\2026-09-05\longlinshuoxi-wav\小云宝项目-完整交接包-2026-09-12\05_硬件与EDA\嘉立创板子\work-v103-drc\extracted2\New Project_2026-09-02_22-36-35.epru"
)
OUT = Path(
    r"c:\Users\xijie\Documents\Codex\2026-09-05\longlinshuoxi-wav\小云宝项目-完整交接包-2026-09-12\05_硬件与EDA\嘉立创板子\work-v103-drc\analysis"
)
OUT.mkdir(parents=True, exist_ok=True)


def extract_json_objects(raw: str) -> list[dict]:
    objs = []
    i = 0
    n = len(raw)
    bad = 0
    while i < n:
        while i < n and raw[i] in " \t\r\n|":
            i += 1
        if i >= n:
            break
        if raw[i] != "{":
            # skip until next brace
            nxt = raw.find("{", i)
            if nxt < 0:
                break
            i = nxt
        start = i
        depth = 0
        in_str = False
        esc = False
        while i < n:
            ch = raw[i]
            if in_str:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    in_str = False
            else:
                if ch == '"':
                    in_str = True
                elif ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    if depth == 0:
                        i += 1
                        chunk = raw[start:i]
                        try:
                            objs.append(json.loads(chunk))
                        except json.JSONDecodeError:
                            bad += 1
                            if bad <= 8:
                                print("bad json", chunk[:120])
                        break
            i += 1
        else:
            break
    print(f"parsed={len(objs)} bad={bad}")
    return objs


def brief(obj, n=500):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + f"...<{len(s)}>"


def main():
    raw = SRC.read_text(encoding="utf-8", errors="replace")
    objs = extract_json_objects(raw)
    types = Counter(o.get("type") or o.get("docType") or o.get("layerType") or "?" for o in objs)
    print("=== type counts ===")
    for k, v in types.most_common(80):
        print(f"  {k}: {v}")

    keys_by_type = defaultdict(Counter)
    for o in objs:
        t = o.get("type") or o.get("docType") or "?"
        for k in o.keys():
            keys_by_type[t][k] += 1
    (OUT / "keys_by_type.json").write_text(
        json.dumps({k: dict(v.most_common()) for k, v in keys_by_type.items()}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    seen = set()
    samples = []
    for o in objs:
        t = o.get("type") or o.get("docType") or "?"
        if t in seen:
            continue
        seen.add(t)
        samples.append({"type": t, "sample": o if len(json.dumps(o)) < 6000 else {"keys": list(o.keys()), "preview": brief(o, 2000)}})
    (OUT / "type_samples.json").write_text(json.dumps(samples, ensure_ascii=False, indent=2), encoding="utf-8")

    # documents
    print("=== documents ===")
    for o in objs:
        if o.get("docType") or o.get("type") in ("DOCHEAD", "DOCUMENT"):
            print(brief(o, 400))

    copper = [o for o in objs if str(o.get("type") or "").upper() in {
        "COPPER", "COPPERAREA", "POUR", "FILL", "PLANE", "POLYGON", "ZONE", "COPPER_POUR", "REGION", "SOLIDREGION"
    } or "COPPER" in str(o.get("type") or "").upper() or "POUR" in str(o.get("type") or "").upper()]
    print("copper-like types from all types:", [t for t in types if any(x in str(t).upper() for x in ("COPPER", "POUR", "FILL", "PLANE", "POLY", "ZONE", "REGION"))])
    print("copper count", len(copper))
    (OUT / "copper_like.json").write_text(json.dumps(copper[:100], ensure_ascii=False, indent=2)[:800000], encoding="utf-8")

    # nets
    net_objs = [o for o in objs if str(o.get("type") or "").upper() in ("NET", "NETS", "NETCLASS")]
    print("NET objects", len(net_objs))
    net_names = []
    for o in objs:
        for k in ("net", "netName", "name", "network"):
            v = o.get(k)
            if isinstance(v, str) and v:
                net_names.append((o.get("type"), k, v))
    name_counts = Counter(v for _, _, v in net_names)
    print("unique net-ish names", len(name_counts))
    print("top names:")
    for n, c in name_counts.most_common(40):
        print(f"  {n}: {c}")
    gnd_names = [n for n in name_counts if "GND" in n.upper() or n in ("0", "AGND", "PGND", "DGND")]
    print("GND-like names", gnd_names)

    # DRC rule objects
    rule_types = [t for t in types if any(x in str(t).upper() for x in ("RULE", "DRC", "CLEAR", "CONSTRAINT"))]
    print("rule types", rule_types)

    # vias / pads missing net
    pads = [o for o in objs if str(o.get("type") or "").upper() in ("PAD", "VIA", "THPAD", "SMDPAD")]
    print("pad/via count", len(pads))

    (OUT / "summary.txt").write_text(
        "\n".join(
            [
                f"objects={len(objs)}",
                "types:",
                *[f"  {k}: {v}" for k, v in types.most_common()],
                "",
                f"gnd-like names: {gnd_names}",
                f"copper: {len(copper)}",
            ]
        ),
        encoding="utf-8",
    )
    print("wrote", OUT)


if __name__ == "__main__":
    main()
