#!/usr/bin/env python3
"""App logo: writes packaging/dev.keychainmanager.svg and prints the two SVG strings for src/icons.c.
Same canvas (624x456) and palette as File Explorer's logo: a key lying left to right, its bow in the light front
gradient with a white hole, its shaft and teeth in the deep back gradient, the bow's layered shadow falling on the
shaft. Plain shapes only (no <use>/clipPath) so nanosvg can draw it. src/icons.c recolours everything but the white
towards the accent colour."""
import math
import sys

CX, CY, R = 182, 228, 134       # bow
SY0, SY1 = 189, 267             # shaft top / bottom
SHAFT = (f'<rect x="287" y="{SY0}" width="288" height="{SY1 - SY0}" rx="16" fill="url(#b)"/>'
         '<rect x="513" y="225" width="50" height="106" rx="14" fill="url(#b)"/>'
         '<rect x="437" y="225" width="50" height="78" rx="14" fill="url(#b)"/>')

def shadow(dx):  # the bow shifted right by dx, clipped to the shaft
    x = CX + dx + math.sqrt(R * R - (CY - SY0) ** 2)
    return f"M287 {SY0}H{x:.1f}A{R} {R} 0 0 1 {x:.1f} {SY1}H287Z"

def svg(s, view="0 0 624 456", size='width="624" height="456"'):  # s scales the hole (bolder at small sizes)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" {size} viewBox="{view}"><defs>'
            '<linearGradient id="b" x1="287" y1="189" x2="575" y2="331" gradientUnits="userSpaceOnUse">'
            '<stop offset="0" stop-color="#2566DE"/><stop offset="1" stop-color="#193C96"/></linearGradient>'
            '<linearGradient id="f" x1="48" y1="94" x2="316" y2="362" gradientUnits="userSpaceOnUse">'
            '<stop offset="0" stop-color="#4CC8FF"/><stop offset="1" stop-color="#2D7BE8"/></linearGradient></defs>'
            + SHAFT
            + "".join(f'<path d="{shadow(dx)}" fill="#0A1E5C" opacity="{op}"/>' for dx, op in ((14, .08), (9, .12), (5, .18)))
            + f'<circle cx="{CX}" cy="{CY}" r="{R}" fill="url(#f)"/>'
            + f'<circle cx="{CX}" cy="{CY}" r="{50 * s:.1f}" fill="#FFFFFF"/></svg>')

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--files":
        open("/tmp/logo.svg", "w").write(svg(1.0))
        open("/tmp/logo_small.svg", "w").write(svg(1.25))
    else:
        open("packaging/dev.keychainmanager.svg", "w").write(svg(1.0, "0 -84 624 624", 'width="256" height="256"') + "\n")
        for name, s in (("g_logo_svg", 1.0), ("g_logo_small_svg", 1.25)):
            print(f'{name}\t{svg(s)}')
