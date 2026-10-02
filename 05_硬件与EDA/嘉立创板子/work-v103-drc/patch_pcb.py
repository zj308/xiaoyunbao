#!/usr/bin/env python3
"""Patch EasyEDA Pro PCB: move outline/pour onto the circuit, add bottom GND pour."""
from __future__ import annotations

import json
import re
import zipfile
from pathlib import Path

ROOT = Path(
    r"c:\Users\xijie\Documents\Codex\2026-09-05\longlinshuoxi-wav\小云宝项目-完整交接包-2026-09-12\05_硬件与EDA\嘉立创板子"
)
WORK = ROOT / "work-v103-drc"
EPRU = WORK / "extracted2" / "New Project_2026-09-02_22-36-35.epru"
OUT_EPRU = WORK / "patched" / "New Project_2026-09-02_22-36-35.epru"
OUT_ZIP = ROOT / "立创板子-v103-drc-fix.epro2"

# Board is 54.7 x 64.6 mm. Circuit lives at y<=0; original outline was y>=0.
OUTLINE = '["R",0,-2543.3071,2153.5433,2543.3071,0,0]'
# 0.3 mm = 11.811 mil inset from board edge (JLCPCB copper-to-outline).
POUR_PATH = '[["R",11.811,-2531.4961,2129.9213,2519.6851,0,0]]'
POUR2_ID = "a7c41e90b2d6f018"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected 1 occurrence, found {n}")
    return text.replace(old, new, 1)


def blank_pourfill(text: str, poured_id: str, ticket: str) -> str:
    # Match the POURED header and its body through the end of that line.
    pat = re.compile(
        r'\{\s*"type"\s*:\s*"POURED"\s*,\s*"ticket"\s*:\s*'
        + ticket
        + r'.*?\|$'
        ,
        re.M,
    )
    # Simpler: the id is unique; take from that header to end of line.
    key = f'"id":"[\\"POURED\\",\\"{poured_id}\\"]"'
    idx = text.find(key)
    if idx < 0:
        # try without extra escapes as stored
        key = '["POURED","' + poured_id + '"]'
        idx = text.find(key)
        if idx < 0:
            raise SystemExit(f"POURED {poured_id} not found")
    line_start = text.rfind("\n", 0, idx) + 1
    line_end = text.find("\n", idx)
    if line_end < 0:
        line_end = len(text)
    line = text[line_start:line_end]
    # keep header, replace body pourFill
    head, sep, _body = line.partition("||")
    if sep != "||":
        raise SystemExit(f"POURED {poured_id} has no || body")
    new_line = head + '||{"pourFill":[]}|'
    return text[:line_start] + new_line + text[line_end:]


def main() -> None:
    text = EPRU.read_text(encoding="utf-8")
    orig_len = len(text)

    text = replace_once(
        text,
        '"path":["R",0,0,2153.5433,2543.3071,0,0],"locked":false,"zIndex":-1,"polyType":"BOARD_OUTLINE"',
        '"path":' + OUTLINE + ',"locked":false,"zIndex":-1,"polyType":"BOARD_OUTLINE"',
        "BOARD_OUTLINE",
    )
    text = replace_once(
        text,
        '"path":[["R",0,0,2153.5433,2543.3071,0,0]]',
        '"path":' + POUR_PATH,
        "POUR1 path",
    )
    text = replace_once(
        text,
        '{"type":"ELE_PLACEHOLDER","ticket":24149,"id":"placeholder63"}||{"dataType":"POURED","max":1}|',
        '{"type":"ELE_PLACEHOLDER","ticket":24149,"id":"placeholder63"}||{"dataType":"POURED","max":2}|',
        "POURED placeholder max",
    )
    text = replace_once(
        text,
        '{"type":"DOCHEAD","ticket":24260}||{"docType":"PCB","client":"fca937f9d002ae05","uuid":"2c993d24c906d1cd","updateTime":1789392492524,"version":"1789392492524"',
        '{"type":"DOCHEAD","ticket":24264}||{"docType":"PCB","client":"fca937f9d002ae05","uuid":"2c993d24c906d1cd","updateTime":1789396800000,"version":"1789396800000"',
        "PCB DOCHEAD",
    )

    text = blank_pourfill(text, "0289b50091e0e561", "24150")
    text = blank_pourfill(text, "9b25ebc06f3ef24a", "15910")

    pour2 = (
        '{"type":"POUR","ticket":24262,"id":"' + POUR2_ID + '"}'
        '||{"partitionId":"","groupId":0,"netName":"GND","layerId":2,"width":0.2,"name":"POUR2","order":1,'
        '"path":' + POUR_PATH + ',"pourType":{"pourType":"SOLID","fineness":8},"keepIsland":false,"locked":false,"zIndex":-1}|\n'
        '{"type":"POURED","ticket":24263,"id":"[\\"POURED\\",\\"' + POUR2_ID + '\\"]"}||{"pourFill":[]}|'
    )
    marker = '{"type":"ELE_PLACEHOLDER","ticket":24155,"id":"placeholder64"}'
    if marker not in text:
        raise SystemExit("insert marker not found")
    # insert POUR2 before the next placeholder after POUR1's poured line
    # Find POUR1 poured header line end, then insert before placeholder64
    text = replace_once(text, marker, pour2 + "\n" + marker, "insert POUR2")

    OUT_EPRU.parent.mkdir(parents=True, exist_ok=True)
    OUT_EPRU.write_text(text, encoding="utf-8", newline="\n")
    print(f"patched epru {orig_len} -> {len(text)} bytes")

    # verify
    if '"polyType":"BOARD_OUTLINE"' not in text:
        raise SystemExit("outline missing")
    if OUTLINE not in text:
        raise SystemExit("new outline missing")
    if text.count(POUR_PATH) != 2:
        raise SystemExit(f"pour path count {text.count(POUR_PATH)}")
    if '"layerId":2,"width":0.2,"name":"POUR2"' not in text:
        raise SystemExit("POUR2 missing")
    if '"name":"POUR1"' not in text:
        raise SystemExit("POUR1 missing")

    img_dir = WORK / "extracted2" / "IMAGE"
    proj = {
        "title": "立创板子-v103-drc-fix",
        "cbb_project": False,
        "editorVersion": "3.2.149.88089769",
        "introduction": "GND pour/outline DRC fix: moved board outline onto circuit, added bottom GND pour.",
        "description": "",
        "tags": "[]",
    }
    proj_path = OUT_EPRU.parent / "project2.json"
    proj_path.write_text(json.dumps(proj, ensure_ascii=False, indent=2), encoding="utf-8")

    if OUT_ZIP.exists():
        OUT_ZIP.unlink()
    with zipfile.ZipFile(OUT_ZIP, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.write(proj_path, "project2.json")
        z.write(OUT_EPRU, OUT_EPRU.name)
        if img_dir.exists():
            for p in img_dir.rglob("*"):
                if p.is_file():
                    z.write(p, "IMAGE/" + p.name)
    print("wrote", OUT_ZIP, "size", OUT_ZIP.stat().st_size)


if __name__ == "__main__":
    main()
