#!/usr/bin/env python3
"""Lay out promotional pages around unmodified, real Simulator screenshots.

Generate HTML, then capture each page at 1320 x 2868 with a browser. This never
draws substitute game states or modifies the app screenshots.
"""
import argparse
import base64
import html
from pathlib import Path

TEMPLATE = """<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Still Solvable — {title}</title>
<style>
* {{ box-sizing: border-box }}
html, body {{ margin:0; width:1320px; height:2868px; overflow:hidden }}
body {{ background:#f1eee3; color:#153f2f; font-family:-apple-system,BlinkMacSystemFont,Arial,sans-serif }}
.page {{ height:100%; position:relative; padding:95px 90px 0; text-align:center;
background:radial-gradient(ellipse at 50% 73%,#b4c6b0 0%,#e5e8da 38%,#f1eee3 72%) }}
.brand {{ font-size:29px; font-weight:650; letter-spacing:7px }}
h1 {{ font:normal 94px/1.05 Georgia,serif; letter-spacing:-3px; margin:32px 0 0 }}
.phone {{ position:absolute; top:440px; left:138px; width:1044px; padding:12px;
border-radius:77px; background:#112b20; box-shadow:0 30px 90px #112b203a,0 2px 0 #ffffff99 inset }}
.phone img {{ display:block; width:1020px; height:auto; border-radius:65px }}
.footer {{ position:absolute; bottom:64px; left:0; width:100%; font-size:25px;
letter-spacing:2px; color:#52705b }}
</style>
<main class="page"><div class="brand">STILL SOLVABLE</div><h1>{headline}</h1>
<div class="phone"><img src="data:image/png;base64,{image}" alt="Actual Still Solvable iPhone gameplay"></div>
<div class="footer">{footer}</div></main></html>"""

SHOTS = [
    ("01-classic.png", "Every deal starts with a solution.",
     "Every deal starts<br>with a solution.", "CLASSIC KLONDIKE · VERIFIED WINNABLE DEALS"),
    ("02-in-play.png", "Your next move, one tap away.",
     "Your next move,<br>one tap away.", "TRADITIONAL CARDS · TAP TO MOVE · UNDO"),
    ("03-draw-three.png", "The game you know. A little wiser.",
     "The game you know.<br>A little wiser.", "DRAW ONE OR THREE · LIVE WINNABILITY CHECKS"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for filename, title, headline, footer in SHOTS:
        data = (args.captures / filename).read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"Not a PNG screenshot: {filename}")
        target = args.output / (Path(filename).stem + ".html")
        target.write_text(TEMPLATE.format(title=html.escape(title), headline=headline,
            footer=footer, image=base64.b64encode(data).decode()), encoding="utf-8")
        print(target)
    frames = "".join('<div><iframe title="Promotional screenshot" src="' +
                     Path(shot[0]).stem + '.html"></iframe></div>' for shot in SHOTS)
    (args.output / "overview.html").write_text('''<!doctype html><html lang="en">
<meta charset="utf-8"><title>Still Solvable screenshots</title><style>
* {box-sizing:border-box} html,body {margin:0;width:1320px;height:936px;overflow:hidden}
body {background:#e5e8da;display:flex;gap:24px;padding:36px 42px}
div {width:396px;height:861px;overflow:hidden;border-radius:12px;flex-shrink:0}
iframe {border:0;width:1320px;height:2868px;transform:scale(.3);transform-origin:top left}
</style>''' + frames + '</html>', encoding="utf-8")


if __name__ == "__main__":
    main()
