"""The HDR diagnostic of the fire (AST-210): the source formula of a flame pixel (particle-rules.md 1, rules2 a) in cd/m2, its display value
after the source exposure and the source Hable curve (research/color-pipeline.md 7), and the client HDR inverted from display pixels.
Usage: python3 -B tools/lib/particle_hdr_diag.py (the pictures of the check in SWC_WORK/particles: rt2, g8, gmenu, rm10; docs/PARTICLES.md)"""
import json
import math
import os

import numpy as np
from PIL import Image

CONTENT = os.path.expanduser("~/Games/soulwarden-client/content/particles")
WORK = os.path.expanduser("~/Games/soulwarden-client/work/particles")
UNIT = 10000.0  # cd/m2 of one source unit (x swc.LightScale 1)


def hable(x, A=1.32, B=0.225, C=0.10, D=0.20, E=0.01, F=0.30):
    return (x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F) - E / F


def display(x, white=4.0):
    t = np.clip(hable(np.maximum(x, 0.0)) / hable(white), 0, 1)
    s = np.where(t < 0.0031308, 12.92 * t, 1.055 * np.power(t, 1 / 2.4) - 0.055)
    return np.round(s * 255).astype(int)


def inverse(byte, white=4.0):
    """@return x with display(x) = byte (None when the channel is at the top)."""
    if byte >= 254:
        return None
    lo, hi = 0.0, 1000.0
    for _ in range(80):
        mid = (lo + hi) / 2
        if display(np.array(mid), white) < byte:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


def exposure(ev):
    return 1.0 / (1.2 * 2 ** ev)


def srgb_decode(c):
    c = np.asarray(c, dtype=float) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def emitter(system, name):
    d = json.load(open(os.path.join(CONTENT, "systems.json")))
    s = next(s for s in d["systems"] if s["key"] == system)
    return next(e for e in s["emitters"] if e["name"] == name), d["textures"]


def core(e, textures, age, B):
    """The source HDR (cd/m2, RGB) of a bright atlas pixel of the emitter at the age (0..1), placement tint applied by the caller."""
    tex = next(t for t in textures.values() if t["asset"] == e["texture"])
    im = np.asarray(Image.open(os.path.join(os.path.dirname(CONTENT), tex["file"])).convert("RGBA")).astype(float)
    a = im[..., 3] / 255.0
    sel = a > np.percentile(a, 99.5)
    rgb = srgb_decode(im[..., :3][sel]).mean(0)
    alpha_tex = a[sel].mean()
    i = int(round(age * 31))
    color = np.array(e["color"]["base"]) * np.array(e["color"]["curve"][i])
    alpha = e["alpha"]["base"][1] * e["alpha"]["curve"][i]
    F = 1 + (B - 1) * e["tod"]
    opacity = min(1.0, alpha_tex * alpha)
    ambient = rgb * color * e["ambient"]
    glow = opacity * rgb * rgb * np.array(e["glowColor"])   # the glow map is the same atlas for the torch
    return (ambient, glow, F, opacity)


def report(label, ambient, glow, F, opacity, tint, ev):
    hdr = (ambient * np.array(tint) + glow) * F * UNIT
    x = hdr * exposure(ev)
    print(f"{label}: source HDR {np.round(hdr).astype(int).tolist()} cd/m2 (ambient {np.round(ambient * np.array(tint) * F * UNIT).astype(int).tolist()}, "
          f"glow {np.round(glow * F * UNIT).astype(int).tolist()}), F {F:.2f}, opacity {opacity:.2f}; at EV {ev:.2f}: X {np.round(x, 2).tolist()} "
          f"-> display {display(x).tolist()} (over the background with the opacity)")


e, tex = emitter("Buildables.Torch.Idle", "Torch.Idle")
tint = [0.952941, 0.494118, 0.090196]
for B in (0.4, 0.5):
    for age in (0.3, 0.46):
        report(f"torch core, B {B}, age {age}", *core(e, tex, age, B), tint, 10.11)
e, tex = emitter("Buildables.Campfire.Idle_Medium", "Flame_1")
report("menu Flame_1 core, B 1.5, age 0.4", *core(e, tex, 0.4, 1.5), [1, 1, 1], 11.35)
e, tex = emitter("Buildables.Campfire.Idle_Medium", "Flame_2")
report("menu Flame_2 core, B 1.5, age 0.4", *core(e, tex, 0.4, 1.5), [1, 1, 1], 11.35)


def client(label, picture, box, ev):
    im = np.asarray(Image.open(os.path.join(WORK, picture)).convert("RGB").crop(box)).astype(int)
    lum = im.mean(2)
    for name, sel in (("core (top 2 %)", lum >= np.percentile(lum, 98)), ("edge (80 to 90 %)", (lum >= np.percentile(lum, 80)) & (lum < np.percentile(lum, 90)))):
        px = np.round(im[sel].mean(0)).astype(int)
        xs = [inverse(int(c)) for c in px]
        hdr = [None if x is None else round(x / exposure(ev)) for x in xs]
        print(f"{label} {name}: display {px.tolist()} -> client HDR {hdr} cd/m2 (EV {ev})")


client("client torch post (p2g, 23:04)", "g8.png", (575, 280, 620, 330), 10.11)
client("retail torch post (retail-torch2)", "rt2.png", (650, 280, 700, 330), 10.11)
client("client menu fire (p2g)", "gmenu.png", (300, 460, 520, 580), 11.35)
client("retail menu fire (retail-menu1)", "rm10.png", (240, 430, 480, 560), 11.35)
