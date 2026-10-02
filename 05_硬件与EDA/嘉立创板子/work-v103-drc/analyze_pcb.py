#!/usr/bin/env python3
"""Analyze PCB document geometry, pour vs board, vias, unconnected GND."""
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


def is_header(o: dict) -> bool:
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
        if head.get("type") == "DOCHEAD" or "docType" in head:
            if head.get("type") == "DOCHEAD":
                cur = {"head": head, "meta": None, "items": []}
                docs.append(cur)
                continue
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


def bbox_of_path(path):
    xs, ys = [], []
    if not path:
        return None
    for cmd in path:
        if not isinstance(cmd, (list, tuple)):
            continue
        nums = [c for c in cmd[1:] if isinstance(c, (int, float))]
        # typical: L x y, R x y w h, C ...
        if cmd[0] == "R" and len(nums) >= 4:
            x, y, w, h = nums[:4]
            xs += [x, x + w]
            ys += [y, y + h]
        else:
            # take pairs
            for a, b in zip(nums[0::2], nums[1::2]):
                xs.append(a)
                ys.append(b)
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def main():
    objs = extract_json_objects(SRC.read_text(encoding="utf-8", errors="replace"))
    pairs = pair_records(objs)
    docs = split_docs(pairs)
    print("docs", len(docs))
    for d in docs:
        meta = d.get("meta") or {}
        print(f"  {meta.get('docType')} uuid={meta.get('uuid')} items={len(d['items'])} ticket={d['head'].get('ticket')}")

    pcb = None
    sch = None
    for d in docs:
        dt = (d.get("meta") or {}).get("docType")
        if dt == "PCB":
            pcb = d
        if dt == "SCH_PAGE":
            sch = d
    assert pcb, "no PCB doc"
    print("\n=== PCB item types ===")
    print(Counter(h.get("type") for h, _ in pcb["items"]))

    # dump pour, outline, rules, vias, pads from PCB only
    def items(t):
        return [(h, b) for h, b in pcb["items"] if h.get("type") == t]

    print("\n=== PCB POUR ===")
    for h, b in items("POUR"):
        print(json.dumps({"head": h, "body": {k: v for k, v in (b or {}).items() if k != "path"} | {"path": (b or {}).get("path"), "path_bbox": bbox_of_path((b or {}).get("path"))}}, ensure_ascii=False, indent=2)[:4000])

    print("\n=== PCB POURED ===")
    for h, b in items("POURED"):
        fills = (b or {}).get("pourFill") or []
        print("head", h, "nfill", len(fills))
        if fills:
            # each fill item keys
            f0 = fills[0]
            print("fill0 keys", f0.keys() if isinstance(f0, dict) else type(f0), json.dumps(f0, ensure_ascii=False)[:500])
            bbs = []
            nets = Counter()
            layers = Counter()
            for f in fills:
                if isinstance(f, dict):
                    nets[str(f.get("netName") or f.get("net"))] += 1
                    layers[str(f.get("layerId"))] += 1
                    bb = bbox_of_path(f.get("path") or f.get("points") or f.get("poly"))
                    if bb:
                        bbs.append(bb)
            print("fill nets", nets, "layers", layers)
            if bbs:
                print("fill bbox", min(b[0] for b in bbs), min(b[1] for b in bbs), max(b[2] for b in bbs), max(b[3] for b in bbs))

    print("\n=== PCB RULE ids ===")
    for h, b in items("RULE"):
        print(h.get("id"), json.dumps(b, ensure_ascii=False)[:220])

    print("\n=== PCB lines by layer ===")
    layer_lines = defaultdict(list)
    for h, b in items("LINE"):
        if not b:
            continue
        layer_lines[b.get("layerId")].append(b)
    for lid, ls in sorted(layer_lines.items(), key=lambda x: str(x[0])):
        bbs = [bbox_of_path(x.get("path") or x.get("points")) for x in ls]
        bbs = [x for x in bbs if x]
        netc = Counter(str(x.get("netName") or x.get("net")) for x in ls)
        print(f"  layer {lid}: n={len(ls)} nets={netc.most_common(8)} bbox=", end="")
        if bbs:
            print(min(b[0] for b in bbs), min(b[1] for b in bbs), max(b[2] for b in bbs), max(b[3] for b in bbs))
        else:
            print("none sample", json.dumps(ls[0], ensure_ascii=False)[:250])

    print("\n=== PCB POLY ===")
    for h, b in items("POLY")[:8]:
        print(json.dumps({k: (b or {}).get(k) for k in list(b or {})[:20]}, ensure_ascii=False)[:400])

    print("\n=== PCB VIA ===")
    via_nets = Counter()
    via_sizes = Counter()
    vias = []
    for h, b in items("VIA"):
        if not b:
            continue
        vias.append(b)
        via_nets[str(b.get("netName") or b.get("net"))] += 1
        via_sizes[(b.get("holeR") or b.get("innerDiameter") or b.get("drill"), b.get("outerR") or b.get("diameter") or b.get("outerDiameter"))] += 1
    print("via nets", via_nets)
    print("via sizes", via_sizes)
    print("sample via", json.dumps(vias[0] if vias else {}, ensure_ascii=False)[:600])
    gnd_vias = [v for v in vias if (v.get("netName") or v.get("net")) == "GND"]
    print("GND vias", len(gnd_vias), "total", len(vias))

    print("\n=== PCB PAD ===")
    pad_nets = Counter()
    pads = []
    for h, b in items("PAD"):
        if not b:
            continue
        pads.append((h, b))
        pad_nets[str(b.get("netName") or b.get("net"))] += 1
    print("pad nets", pad_nets)
    print("sample pad", json.dumps(pads[0][1] if pads else {}, ensure_ascii=False)[:700])

    print("\n=== PCB COMPONENT bbox ===")
    xs, ys = [], []
    for h, b in items("COMPONENT"):
        if not b:
            continue
        if "x" in b:
            xs.append(b["x"])
            ys.append(b["y"])
    if xs:
        print("comp xy", min(xs), min(ys), max(xs), max(ys), "n", len(xs))

    print("\n=== PCB HOLE ===")
    for h, b in items("HOLE"):
        print(h, json.dumps(b, ensure_ascii=False)[:300])

    print("\n=== PCB NET list ===")
    for h, b in items("NET"):
        print(h.get("id"))

    print("\n=== PCB PREFERENCE ===")
    for h, b in items("PREFERENCE"):
        print(json.dumps(b, ensure_ascii=False)[:500])

    # PAD_NET connectivity
    print("\n=== PAD_NET ===")
    pn = Counter()
    for h, b in items("PAD_NET"):
        pn[str(h.get("id"))] += 1
    print("count", len(items("PAD_NET")), "sample", items("PAD_NET")[:5])

    # empty nets / unnamed
    print("\n=== SIGNAL ===")
    for h, b in items("SIGNAL")[:5]:
        print(h, json.dumps(b, ensure_ascii=False)[:250])

    # save PCB-only dump
    slim_items = []
    for h, b in pcb["items"]:
        t = h.get("type")
        if t in {"POUR", "POURED", "RULE", "VIA", "PAD", "HOLE", "NET", "PREFERENCE", "COMPONENT", "CANVAS"}:
            bb = dict(b) if b else None
            if isinstance(bb, dict) and "pourFill" in bb:
                bb["pourFill"] = f"<n={len(bb['pourFill'])}>"
            slim_items.append({"head": h, "body": bb})
    (OUT / "pcb_slim.json").write_text(json.dumps(slim_items, ensure_ascii=False, indent=2)[:900000], encoding="utf-8")

    if sch:
        print("\n=== SCH item types ===", Counter(h.get("type") for h, _ in sch["items"]))


if __name__ == "__main__":
    main()
