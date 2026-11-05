#!/usr/bin/env python3
"""App logo: writes packaging/dev.keychainmanager.svg and prints the two SVG strings for src/icons.c.
Same canvas (624x456) and palette as File Explorer's logo: a back plate, a padlock in front with its shackle,
a white keyhole. src/icons.c recolours everything but the white towards the accent colour."""
import sys

BACK = '<rect x="205" y="115" width="387" height="316" rx="28" fill="url(#b)"/>'
SHACKLE = '<path d="M98 198V152A110 110 0 0 1 318 152V198" fill="none" stroke="url(#s)" stroke-width="46"/>'
BODY = "M31 228Q31 198 61 198H355Q385 198 385 228V401Q385 431 355 431H61Q31 431 31 401Z"

def shadow(dx):  # the body's right edge shifted by dx, clipped to the back plate
    return f"M205 198H{355 + dx}Q{385 + dx} 198 {385 + dx} 228V401Q{385 + dx} 431 {355 + dx} 431H205Z"

def keyhole(s):  # circle + tapered slot, centred at (208, 300); s scales it (bolder at small sizes)
    cx, cy, r = 208, 296, 34 * s
    w0, w1, b = 15 * s, 24 * s, cy + 74 * s
    return (f'<circle cx="{cx}" cy="{cy}" r="{r:.1f}" fill="#FFFFFF"/>'
            f'<path d="M{cx - w0:.1f} {cy + 10}H{cx + w0:.1f}L{cx + w1:.1f} {b:.1f}H{cx - w1:.1f}Z" fill="#FFFFFF"/>')

def svg(s, view="0 0 624 456", size='width="624" height="456"'):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" {size} viewBox="{view}"><defs>'
            '<linearGradient id="b" x1="205" y1="115" x2="592" y2="431" gradientUnits="userSpaceOnUse">'
            '<stop offset="0" stop-color="#2566DE"/><stop offset="1" stop-color="#193C96"/></linearGradient>'
            '<linearGradient id="s" x1="98" y1="19" x2="318" y2="198" gradientUnits="userSpaceOnUse">'
            '<stop offset="0" stop-color="#3FA6F5"/><stop offset="1" stop-color="#2566DE"/></linearGradient>'
            '<linearGradient id="f" x1="31" y1="198" x2="385" y2="431" gradientUnits="userSpaceOnUse">'
            '<stop offset="0" stop-color="#4CC8FF"/><stop offset="1" stop-color="#2D7BE8"/></linearGradient></defs>'
            + BACK
            + "".join(f'<path d="{shadow(dx)}" fill="#0A1E5C" opacity="{op}"/>' for dx, op in ((13, .08), (8, .12), (4, .18)))
            + SHACKLE + f'<path d="{BODY}" fill="url(#f)"/>' + keyhole(s) + "</svg>")

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--files":
        open("/tmp/logo.svg", "w").write(svg(1.0))
        open("/tmp/logo_small.svg", "w").write(svg(1.35))
    else:
        open("packaging/dev.keychainmanager.svg", "w").write(svg(1.0, "0 -84 624 624", 'width="256" height="256"') + "\n")
        for name, s in (("g_logo_svg", 1.0), ("g_logo_small_svg", 1.35)):
            print(f'{name}\t{svg(s)}')
