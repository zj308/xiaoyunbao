#!/usr/bin/env python3
"""Pair EasyEDA Pro header|body records and dump PCB pour/net/rule details."""
from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

SRC = Path(
    r"c:\Users\xijie\Documents\Codex\2026-09-05\longlinshuoxi-wav\小云宝项目-完整交接包-2026-09-12\05_硬件与EDA\嘉立创板子\work-v103-drc\extracted2\New Project_2026-09-02_22-36-35.epru"
)
OUT = Path(
    r"c:\Users\xijie\Documents\Codex\2026-09-05\longlinshuoxi-wav\小云宝项目-完整交接包-2026-09-12\05_硬件与EDA\嘉立创板子\work-v103-drc\analysis"
)


def extract_json_objects(raw: str) -> list[dict]:
    objs = []
    i = 0
    n = len(raw)
    while i < n:
        while i < n and raw[i] in " \t\r\n|":
            i += 1
        if i >= n:
            break
        if raw[i] != "{":
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
                        objs.append(json.loads(raw[start:i]))
                        break
            i += 1
        else:
            break
    return objs


def pair_records(objs: list[dict]) -> list[tuple[dict, dict | None]]:
    pairs = []
    i = 0
    while i < len(objs):
        o = objs[i]
        if "type" in o and ("ticket" in o or o["type"] == "DOCHEAD"):
            body = None
            if i + 1 < len(objs) and "type" not in objs[i + 1] or (
                i + 1 < len(objs)
                and objs[i + 1].get("type") not in (None,)
                and "ticket" not in objs[i + 1]
                and "docType" in objs[i + 1]
            ):
                pass
            if i + 1 < len(objs):
                nxt = objs[i + 1]
                # body usually has no "type"+"ticket"
                if not (nxt.get("type") and "ticket" in nxt) and "docType" not in nxt:
                    body = nxt
                    i += 1
            pairs.append((o, body))
        else:
            pairs.append(({"type": "_ORPHAN", "raw_keys": list(o.keys())}, o))
        i += 1
    return pairs


def slim(obj, n=8000):
    s = json.dumps(obj, ensure_ascii=False)
    if len(s) <= n:
        return obj
    if isinstance(obj, dict):
        out = {}
        for k, v in obj.items():
            vs = json.dumps(v, ensure_ascii=False)
            out[k] = v if len(vs) < 400 else f"<truncated {len(vs)} chars>"
        return out
    return s[:n]


def main():
    objs = extract_json_objects(SRC.read_text(encoding="utf-8", errors="replace"))
    pairs = pair_records(objs)
    print("pairs", len(pairs))

    want = {"POUR", "POURED", "NET", "RULE", "RULE_TEMPLATE", "PCB", "BOARD", "VIA", "PAD", "POLY", "LINE", "HOLE", "CANVAS", "PREFERENCE", "COMPONENT", "SIGNAL", "PAD_NET"}
    dump = defaultdict(list)
    for head, body in pairs:
        t = head.get("type")
        if t in want:
            dump[t].append({"head": head, "body": slim(body) if body else None})

    for t, items in dump.items():
        (OUT / f"pairs_{t}.json").write_text(
            json.dumps(items if t not in {"VIA", "PAD", "LINE", "POLY", "PAD_NET", "SIGNAL"} else items[:30] + [{"_count": len(items)}], ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        print(t, len(items))

    # all POUR/POURED fully
    pours = [x for x in dump["POUR"]] + [x for x in dump["POURED"]]
    print("=== POUR/POURED ===")
    print(json.dumps(pours, ensure_ascii=False, indent=2)[:20000])

    # NET names
    print("=== NETS ===")
    for x in dump["NET"]:
        b = x["body"] or {}
        print(x["head"].get("id"), b.get("name") or b.get("netName") or list(b.keys())[:20], json.dumps(b, ensure_ascii=False)[:300])

    # RULE names
    print("=== RULES ===")
    for x in dump["RULE"]:
        b = x["body"] or {}
        print(x["head"].get("id"), json.dumps(b, ensure_ascii=False)[:400])

    # via nets
    via_nets = Counter()
    pad_nets = Counter()
    via_no_net = 0
    for head, body in pairs:
        if head.get("type") == "VIA" and body:
            n = body.get("net") or body.get("netName") or body.get("name")
            via_nets[str(n)] += 1
            if not n:
                via_no_net += 1
        if head.get("type") == "PAD" and body:
            n = body.get("net") or body.get("netName") or body.get("name")
            pad_nets[str(n)] += 1
    print("via nets", via_nets)
    print("pad nets", pad_nets.most_common(20), "none", pad_nets.get("None", 0))

    # components
    print("=== COMPONENTS ===")
    for x in dump["COMPONENT"]:
        b = x["body"] or {}
        print(b.get("name") or b.get("designator") or b.get("ref") or list(b)[:15], json.dumps({k: b[k] for k in list(b)[:12]}, ensure_ascii=False)[:250])


if __name__ == "__main__":
    main()
