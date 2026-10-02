#!/usr/bin/env python3
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


def extract_json_objects(raw: str):
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


def is_header(o):
    return ("type" in o and "ticket" in o) or o.get("type") == "DOCHEAD" or "docType" in o


def pair_records(objs):
    pairs = []
    i = 0
    while i < len(objs):
        o = objs[i]
        if is_header(o) and i + 1 < len(objs) and not is_header(objs[i + 1]):
            pairs.append((o, objs[i + 1]))
            i += 2
        else:
            pairs.append((o, None))
            i += 1
    return pairs


def split_docs(pairs):
    docs = []
    cur = None
    for head, body in pairs:
        if head.get("type") == "DOCHEAD":
            cur = {"head": head, "meta": None, "items": []}
            docs.append(cur)
            continue
        if "docType" in head:
            if cur is None:
                cur = {"head": {}, "meta": head, "items": []}
                docs.append(cur)
            else:
                cur["meta"] = head
            continue
        if cur is None:
            cur = {"head": {}, "meta": {}, "items": []}
            docs.append(cur)
        cur["items"].append((head, body))
    return docs


def nums(obj, acc=None):
    if acc is None:
        acc = []
    if isinstance(obj, (int, float)):
        acc.append(obj)
    elif isinstance(obj, dict):
        for v in obj.values():
            nums(v, acc)
    elif isinstance(obj, list):
        for v in obj:
            nums(v, acc)
    return acc


def main():
    objs = extract_json_objects(SRC.read_text(encoding="utf-8", errors="replace"))
    docs = split_docs(pair_records(objs))
    pcb = next(d for d in docs if (d.get("meta") or {}).get("docType") == "PCB")

    def items(t):
        return [(h, b) for h, b in pcb["items"] if h.get("type") == t]

    print("RULE_SELECTOR count", len(items("RULE_SELECTOR")))
    ids = Counter(h.get("id") for h, _ in items("RULE_SELECTOR"))
    print("unique id prefixes:")
    prefixes = Counter()
    for hid in ids:
        s = str(hid)
        prefixes[s[:80]] += 1
    # group by first 2-3 tokens
    kinds = Counter()
    samples = defaultdict(list)
    for h, b in items("RULE_SELECTOR"):
        hid = str(h.get("id"))
        kinds[hid[:60]] += 1
        if len(samples[hid[:40]]) < 3:
            samples[hid[:40]].append({"head": h, "body": b})
    print("id start counts (top 30):")
    # better parse id json
    parsed_kinds = Counter()
    parsed_samples = defaultdict(list)
    empty_body = 0
    for h, b in items("RULE_SELECTOR"):
        hid = h.get("id")
        try:
            arr = json.loads(hid) if isinstance(hid, str) else hid
        except Exception:
            arr = hid
        if isinstance(arr, list):
            key = tuple(str(x) for x in arr[:3])
        else:
            key = (str(hid),)
        parsed_kinds[key] += 1
        if len(parsed_samples[key]) < 2:
            parsed_samples[key].append({"head": h, "body": b})
        if not b:
            empty_body += 1
    print("empty body", empty_body, "of", len(items("RULE_SELECTOR")))
    print("parsed kinds:")
    for k, c in parsed_kinds.most_common(40):
        print(f"  {c:4d}  {k}")
    print("\n=== samples ===")
    for k, c in parsed_kinds.most_common(15):
        print("KIND", k, "count", c)
        print(json.dumps(parsed_samples[k], ensure_ascii=False)[:800])
        print()

    # pour fill numeric range
    print("=== POUR FILL NUM RANGE ===")
    for h, b in items("POURED"):
        arr = nums(b)
        if arr:
            print(h.get("id"), "n", len(arr), "min", min(arr), "max", max(arr), "nfill", len((b or {}).get("pourFill") or []))
        else:
            print(h.get("id"), "empty")

    # collect all fill path xy more carefully
    for h, b in items("POURED"):
        fills = (b or {}).get("pourFill") or []
        xs, ys = [], []
        for f in fills:
            p = f.get("path") if isinstance(f, dict) else None
            if not p:
                continue
            # flatten
            flat = []
            def walk(a):
                if isinstance(a, list):
                    for x in a:
                        walk(x)
                elif isinstance(a, (int, float)):
                    flat.append(a)
            walk(p)
            # cannot know xy pairing easily if mixed with arc radii
        # dump 3 fill path lengths
        print("n fills", len(fills))
        for f in fills[:3]:
            print(" fill", json.dumps(f, ensure_ascii=False)[:350])

    # ATTR on pcb - maybe designators
    print("\n=== ATTR sample ===")
    for h, b in items("ATTR")[:8]:
        print(h, json.dumps(b, ensure_ascii=False)[:200])

    print("\n=== COMPONENT names ===")
    for h, b in items("COMPONENT"):
        attrs = (b or {}).get("attrs") or {}
        print(h.get("id"), attrs.get("Name") or attrs.get("Designator") or attrs, "xy", (b or {}).get("x"), (b or {}).get("y"), "layer", (b or {}).get("layerId"))

    print("\n=== PRIMITIVE sample ===")
    for h, b in items("PRIMITIVE")[:5]:
        print(h, json.dumps(b, ensure_ascii=False)[:300])

    # LAYER names in PCB
    print("\n=== LAYERS ===")
    for h, b in items("LAYER"):
        print(h.get("id"), json.dumps(b, ensure_ascii=False)[:180] if b else None)

    # unnamed net wires
    unnamed = [b for h, b in items("LINE") if b and not b.get("netName")]
    print("\nunnamed lines", len(unnamed))
    if unnamed:
        print(json.dumps(unnamed[0], ensure_ascii=False)[:300])

    # copper rule full
    for h, b in items("RULE"):
        if "COPPER" in str(h.get("id")) or "PLANE" in str(h.get("id")):
            (OUT / "rule_copper.json").write_text(json.dumps({"head": h, "body": b}, ensure_ascii=False, indent=2), encoding="utf-8")
            print("\nCOPPER RULE", json.dumps(b, ensure_ascii=False, indent=2)[:2500])


if __name__ == "__main__":
    main()
