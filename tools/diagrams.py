#!/usr/bin/env python3
"""Regenerate the figures embedded in docs/ and techniques/*/NOTES.md.

Each figure is *computed* from the same formulas / algorithms the code uses (tone curves from color.glsl, the
Blelloch scan by running it, 13's per-mip layouts by replaying its barrier sequence...), so a change in the
technique should be a change here too. Output is deterministic SVG (fixed hash salt, no date, text kept as
text), so regenerating without changes gives a clean `git diff`.

  python3 -m venv tools/.venv && tools/.venv/bin/pip install -r tools/requirements.txt   # once
  tools/.venv/bin/python tools/diagrams.py            # all figures
  tools/.venv/bin/python tools/diagrams.py tone_curves blelloch
  tools/.venv/bin/python tools/diagrams.py --list
  tools/.venv/bin/python tools/diagrams.py --preview /tmp/figs   # + PNG copies to look at
"""

import argparse
import math
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.patches import FancyArrowPatch, Polygon, Rectangle  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
FIGURES = {}  # name -> (output path relative to ROOT, function)

# Deterministic, theme-proof SVGs: white background (Obsidian/GitHub dark mode would otherwise show black text
# on black), text as <text> (smaller, searchable), fixed ids.
plt.rcParams.update({
    "svg.hashsalt": "glint",
    "svg.fonttype": "none",
    "font.size": 10,
    "axes.titlesize": 11,
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "savefig.facecolor": "white",
})

# Colors used across figures (one meaning each).
GL_COLOR, VK_COLOR = "#2b6cb0", "#c05621"
LAYOUT_COLORS = {
    "UNDEFINED": "#e2e8f0",
    "COLOR_ATTACHMENT": "#fbd38d",
    "SHADER_READ_ONLY": "#9ae6b4",
    "TRANSFER_DST": "#bee3f8",
    "TRANSFER_SRC": "#d6bcfa",
}


def figure(name, path):
    def register(fn):
        FIGURES[name] = (path, fn)
        return fn
    return register


def save(fig, path, preview=None):
    out = ROOT / path
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, format="svg", bbox_inches="tight", metadata={"Date": None})
    if preview:  # PNG copies for a quick look (not committed)
        Path(preview).mkdir(parents=True, exist_ok=True)
        fig.savefig(Path(preview) / (out.stem + ".png"), dpi=110, bbox_inches="tight")
    plt.close(fig)
    print(f"  {path}")


def arrow(ax, start, end, color="black", **kw):
    ax.add_patch(FancyArrowPatch(start, end, arrowstyle="-|>", mutation_scale=12, color=color, lw=1.2, **kw))


# --- docs --------------------------------------------------------------------------------------------------------

@figure("clip_space", "docs/diagrams/clip_space.svg")
def clip_space():
    """NDC in GL vs VK: y direction and depth range, and what the negative-height viewport does."""
    fig, axes = plt.subplots(1, 3, figsize=(11, 4.2))
    panels = [
        ("GL: NDC → window", +1, "z ∈ [-1, 1]\nglm::perspectiveRH_NO\n(ClipDepth::NegOneToOne)", GL_COLOR),
        ("VK: NDC → framebuffer", -1, "z ∈ [0, 1]\nglm::perspectiveRH_ZO\n(ClipDepth::ZeroToOne)", VK_COLOR),
        ("VK + viewport height < 0\n(setFlippedViewport)", +1, "same picture as GL,\nsame camera code", VK_COLOR),
    ]
    for ax, (title, ydir, depth, color) in zip(axes, panels):
        ax.set_xlim(-1.5, 1.5)
        ax.set_ylim(-2.1, 1.5)
        ax.set_aspect("equal")
        ax.axis("off")
        ax.add_patch(Rectangle((-1, -1), 2, 2, fill=False, lw=1.5, ec="#4a5568"))
        arrow(ax, (-1.25, -1.25), (-0.6, -1.25), color)
        ax.text(-0.55, -1.25, "+x", va="center", color=color)
        # Where NDC +y ends up on screen: up (GL, flipped VK) or down (plain VK).
        if ydir > 0:
            arrow(ax, (-1.25, -1.25), (-1.25, -0.6), color)
            ax.text(-1.45, -0.5, "+y", color=color, fontsize=9)
        else:
            arrow(ax, (-1.25, -0.6), (-1.25, -1.25), color)
            ax.text(-1.48, -0.5, "+y ↓", color=color, fontsize=9)
        # 01's triangle, top vertex at NDC y = +0.6
        tri = np.array([[-0.5, -0.4], [0.5, -0.4], [0.0, 0.6]])
        tri[:, 1] *= ydir
        ax.add_patch(Polygon(tri, closed=True, fc=color, alpha=0.35, ec=color))
        ax.text(0, 0.75 * ydir, "vertex y = +0.6", ha="center", va="center", fontsize=8)
        ax.set_title(title, color=color)
        ax.text(0.2, -1.32, depth, fontsize=8, va="top")
    fig.suptitle("Clip space: the same vertices, two conventions (CLAUDE.md, docs/gl-vs-vk.md)", fontsize=11)
    return fig


@figure("row_order", "docs/diagrams/row_order.svg")
def row_order():
    """Where row 0 of a rendered image is, and why VK post passes flip v."""
    fig, axes = plt.subplots(1, 2, figsize=(9, 4))
    image = [["sky"] * 4, ["sky"] * 4, ["tree"] * 4, ["grass"] * 4]  # top to bottom as seen on screen
    colors = {"sky": "#bee3f8", "tree": "#68d391", "grass": "#9c4221"}
    for ax, (api, color, first_row_top) in zip(axes, [("GL", GL_COLOR, False), ("VK", VK_COLOR, True)]):
        ax.set_xlim(-2.2, 5)
        ax.set_ylim(-0.8, 4.6)
        ax.set_aspect("equal")
        ax.axis("off")
        for r, row in enumerate(image):  # r = 0 is the top row on screen
            y = 3 - r
            mem_row = r if first_row_top else 3 - r
            for c, cell in enumerate(row):
                ax.add_patch(Rectangle((c, y), 1, 1, fc=colors[cell], ec="white"))
            ax.text(-0.15, y + 0.5, f"memory row {mem_row}", ha="right", va="center", fontsize=8,
                    fontweight="bold" if mem_row == 0 else "normal", color=color if mem_row == 0 else "black")
        ax.plot(0, 0 if not first_row_top else 4, "o", color=color, ms=8, clip_on=False)
        ax.text(0.1, -0.45 if not first_row_top else 4.25, "uv (0,0) samples here", fontsize=8, color=color)
        ax.set_title(f"{api}: rendered image as stored", color=color)
    axes[1].text(1.5, -0.6, "→ post passes flip v once (common fullscreen.vert, #ifdef VULKAN)", ha="center",
                 fontsize=8)
    fig.suptitle("Render-target row order (07, 08, 12+): the viewport flip fixes the *picture*, not the memory",
                 fontsize=11)
    return fig


# --- 03 / 13: per-mip layout timelines --------------------------------------------------------------------------

def layout_timeline(title, level_labels, steps, legend_layouts, calls_note):
    """steps: list of (label, {level: (layout, access)}) — every level's layout during that step; access is
    '', 'R', 'W' or 'RW'. A ▲ marks a layout change since the previous step (= a barrier on that level)."""
    rows, cols = len(level_labels), len(steps)
    fig, ax = plt.subplots(figsize=(max(7, 0.62 * cols + 2.5), 0.42 * rows + 1.9))
    barriers = 0
    previous = None
    for c, (label, state) in enumerate(steps):
        for r in range(rows):
            layout, access = state[r]
            ax.add_patch(Rectangle((c, rows - 1 - r), 1, 1, fc=LAYOUT_COLORS[layout], ec="white", lw=1.5))
            if access:
                ax.text(c + 0.5, rows - 0.5 - r, access, ha="center", va="center", fontsize=8, fontweight="bold")
            if previous is not None and previous[r][0] != layout:
                barriers += 1
                ax.plot(c + 0.08, rows - 0.18 - r, marker="^", color="#c53030", ms=5)
        previous = state
    ax.set_xlim(0, cols)
    ax.set_ylim(0, rows)
    ax.set_xticks([c + 0.5 for c in range(cols)])
    ax.set_xticklabels([s[0] for s in steps], rotation=60, ha="right", fontsize=8)
    ax.set_yticks([rows - 0.5 - r for r in range(rows)])
    ax.set_yticklabels(level_labels, fontsize=8)
    ax.tick_params(length=0)
    for spine in ax.spines.values():
        spine.set_visible(False)
    handles = [Rectangle((0, 0), 1, 1, fc=LAYOUT_COLORS[l]) for l in legend_layouts]
    handles.append(plt.Line2D([], [], marker="^", color="#c53030", ls="", ms=6))
    ax.legend(handles, legend_layouts + [f"layout change on that level ({barriers})"], loc="upper left",
              bbox_to_anchor=(1.01, 1), fontsize=8, frameon=False)
    ax.text(cols + 0.15, 0.1, calls_note, fontsize=8, va="bottom")
    ax.set_title(title + "   (W write, R read; GL: no layouts, no barriers)", fontsize=10)
    return fig


@figure("mip_blit_layouts", "techniques/03_textured_cube/diagrams/mip_blit_layouts.svg")
def mip_blit_layouts():
    n = 9  # a 256x256 texture
    labels = [f"mip {i}  {256 >> i}²" for i in range(n)]
    state = {i: ("UNDEFINED", "") for i in range(n)}
    steps = []

    def step(label, changes, access):
        for i, layout in changes.items():
            state[i] = (layout, state[i][1])
        steps.append((label, {i: (state[i][0], access.get(i, "")) for i in range(n)}))

    step("copy → mip 0", {i: "TRANSFER_DST" for i in range(n)}, {0: "W"})
    for i in range(1, n):
        step(f"blit {i-1}→{i}", {i - 1: "TRANSFER_SRC"}, {i - 1: "R", i: "W"})
    final = {i: "SHADER_READ_ONLY" for i in range(n)}
    step("sampled", final, {i: "R" for i in range(n)})
    return layout_timeline("03: vkCmdBlitImage mip generation (glGenerateTextureMipmap in GL)", labels, steps,
                           ["TRANSFER_DST", "TRANSFER_SRC", "SHADER_READ_ONLY"],
                           f"barrier calls in vk/texture.cpp:\n1 (all UNDEFINED → DST)\n+ {n - 1} (one per blit)\n"
                           f"+ 2 (SRC levels, last level)\n= {n + 2}")


@figure("mip_layouts", "techniques/13_bloom/diagrams/mip_layouts.svg")
def mip_layouts():
    """Replays 13's vk.cpp record(): which layout each bloom level is in during each pass."""
    n, screen = 6, (1280, 720)
    labels = [f"level {i}  {max(screen[0] >> (i + 1), 1)}×{max(screen[1] >> (i + 1), 1)}" for i in range(n)]
    state = {i: "UNDEFINED" for i in range(n)}
    steps = []

    def step(label, access):
        steps.append((label, {i: (state[i], access.get(i, "")) for i in range(n)}))

    for i in range(n):
        state[i] = "COLOR_ATTACHMENT"  # frame start: all levels UNDEFINED -> attachment (one barrier)
    for i in range(n):
        acc = {i: "W"}
        if i > 0:
            acc[i - 1] = "R"
        step(f"down {i}" + (" (+HDR)" if i == 0 else ""), acc)
        state[i] = "SHADER_READ_ONLY"
    for i in range(n - 2, -1, -1):
        state[i] = "COLOR_ATTACHMENT"  # back to attachment, contents kept (LOAD)
        step(f"up {i+1}→{i}", {i: "RW", i + 1: "R"})
        state[i] = "SHADER_READ_ONLY"
    step("composite", {0: "R"})
    return layout_timeline("13: bloom mip chain, one VkImage — the layout of every level during every pass",
                           labels, steps, ["COLOR_ATTACHMENT", "SHADER_READ_ONLY"],
                           f"barrier calls per frame in vk.cpp:\n1 (whole chain UNDEFINED → attachment)\n"
                           f"+ {n} (after each down)\n+ {2 * (n - 1)} (before + after each up)\n"
                           f"= {1 + n + 2 * (n - 1)}")


# --- 04 ---------------------------------------------------------------------------------------------------------

@figure("phong", "techniques/04_basic_shading/diagrams/phong.svg")
def phong():
    fig, (ax, ax2) = plt.subplots(1, 2, figsize=(10, 4), gridspec_kw={"width_ratios": [1.3, 1]})
    ax.set_xlim(-2.2, 2.2)
    ax.set_ylim(-0.4, 2.3)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.fill_between([-2, 2], -0.4, 0, color="#e2e8f0")
    p = np.array([0.0, 0.0])
    n = np.array([0.0, 1.0])
    l = np.array([-0.8, 1.0]) / np.linalg.norm([-0.8, 1.0])
    v = np.array([1.2, 1.0]) / np.linalg.norm([1.2, 1.0])
    r = 2 * np.dot(n, l) * n - l
    labels = [(n, "n (normal)", "black", (0, 0.12)), (l, "l (to light)", "#d69e2e", (0, 0.12)),
              (v, "v (to eye)", GL_COLOR, (0.45, -0.1)), (r, "r = reflect(-l, n)", "#c53030", (-0.35, 0.2))]
    for vec, name, color, (dx, dy) in labels:
        arrow(ax, p, p + 1.6 * vec, color)
        ax.text(p[0] + 1.6 * vec[0] + dx, p[1] + 1.6 * vec[1] + dy, name, ha="center", fontsize=9, color=color)
    ax.text(0, -0.25, "fragment (camera space)", ha="center", fontsize=8)
    ax.set_title("ambient + diffuse·max(n·l, 0) + specular·max(r·v, 0)^k")
    d = np.linspace(0.5, 10, 200)
    ax2.plot(d, 1 / d**2, color="#d69e2e", label="1 / d² (04's falloff)")
    ax2.plot(d, 1 / d, color="#a0aec0", ls="--", label="1 / d (for comparison)")
    ax2.set_xlabel("distance to light")
    ax2.set_ylabel("light reaching the surface")
    ax2.set_ylim(0, 2)
    ax2.legend(frameon=False)
    ax2.set_title("inverse-square falloff")
    return fig


# --- 08 ---------------------------------------------------------------------------------------------------------

@figure("shadow_bias", "techniques/08_shadow_mapping/diagrams/shadow_bias.svg")
def shadow_bias():
    """Shadow acne: a sloped surface vs the depth stored per shadow-map texel, with and without bias."""
    texels = 8
    x = np.linspace(0, texels, 321)
    slope = 0.06
    depth = 0.4 + slope * x  # the surface's depth from the light, continuous
    stored = 0.4 + slope * (np.floor(x) + 0.5)  # what the map holds: depth at each texel's center
    fig, axes = plt.subplots(1, 2, figsize=(11, 3.6), sharey=True)
    for ax, bias in zip(axes, [0.0, 0.035]):
        shadowed = depth - bias > stored
        ax.step(np.arange(texels + 1), 0.4 + slope * (np.arange(texels + 1) + 0.5), where="post", color=VK_COLOR,
                label="stored depth (one per texel)")
        ax.plot(x, depth, color="black", label="surface depth")
        if bias:
            ax.plot(x, depth - bias, color="black", ls=":", label=f"surface depth − bias ({bias})")
        ax.fill_between(x, 0.3, 0.33, where=shadowed, color="#c53030", step="mid", label="wrongly in shadow")
        ax.fill_between(x, 0.3, 0.33, where=~shadowed, color="#68d391", step="mid", label="lit")
        ax.set_xlabel("position along the surface (shadow-map texels)")
        ax.set_title("no bias: acne (half of every texel self-shadows)" if not bias else "with bias: clean")
        ax.set_xticks(range(texels + 1))
    axes[0].set_ylabel("depth from the light")
    axes[1].legend(fontsize=8, frameon=False, loc="upper left")
    fig.suptitle("08: compare(fragment depth, stored depth) on a surface the light hits at an angle", fontsize=11,
                 y=1.04)
    return fig


# --- 09 ---------------------------------------------------------------------------------------------------------

@figure("ubo_alignment", "techniques/09_dynamic_uniform_buffer/diagrams/ubo_alignment.svg")
def ubo_alignment():
    block, objects = 64, 125  # one mat4 per cube
    # Measured (09 logs them): the same RTX reports different alignments per API.
    cases = [(4, "AMD iGPU, GL and VK: alignment 4"), (64, "RTX 3060, VK: alignment 64"),
             (256, "RTX 3060, GL: alignment 256")]
    fig, ax = plt.subplots(figsize=(11, 2.8))
    for row, (align, label) in enumerate(cases):
        stride = (block + align - 1) // align * align
        y = len(cases) - 1 - row
        for i in range(4):
            ax.add_patch(Rectangle((i * stride, y + 0.15), block, 0.7, fc="#90cdf4", ec="#2b6cb0"))
            ax.text(i * stride + block / 2, y + 0.5, f"cube {i}", ha="center", va="center", fontsize=7)
            if stride > block:
                ax.add_patch(Rectangle((i * stride + block, y + 0.15), stride - block, 0.7, fc="#edf2f7",
                                       ec="#cbd5e0", hatch="//"))
        ax.text(-12, y + 0.5, f"{label}\nstride {stride} B, buffer {stride * objects:,} B", ha="right",
                va="center", fontsize=8)
    ax.set_xlim(0, 4 * 256)
    ax.set_ylim(0, len(cases))
    ax.set_yticks([])
    ax.set_xlabel("byte offset (first 4 of 125 cubes; hatched = padding)")
    ax.set_title("09: per-object slices of one uniform buffer — offset i·stride must be a multiple of the alignment")
    return fig


# --- 12 ---------------------------------------------------------------------------------------------------------

def aces(x):
    return np.clip((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0, 1)


def linear_to_srgb(c):
    return np.where(c < 0.0031308, c * 12.92, 1.055 * np.power(np.maximum(c, 0), 1 / 2.4) - 0.055)


@figure("tone_curves", "techniques/12_hdr_tonemapping/diagrams/tone_curves.svg")
def tone_curves():
    """The three operators of common/shaders/color.glsl, and the sRGB encode after them."""
    fig, (ax, ax2) = plt.subplots(1, 2, figsize=(11, 4))
    x = np.logspace(-2, 2, 400)
    ax.plot(x, np.clip(x, 0, 1), label="clamp: everything above 1 is white", color="#718096")
    ax.plot(x, x / (1 + x), label="Reinhard: x / (1 + x), never reaches 1", color=GL_COLOR)
    ax.plot(x, aces(x), label="ACES fitted (Narkowicz): toe + shoulder", color=VK_COLOR)
    ax.axvline(1, color="#cbd5e0", lw=0.8)
    ax.set_xscale("log")
    ax.set_xlabel("scene radiance × exposure (log scale)")
    ax.set_ylabel("display value (linear, before sRGB encode)")
    ax.legend(fontsize=8, frameon=False, loc="upper left")
    ax.set_title("tone mapping: [0, ∞) → [0, 1]")
    c = np.linspace(0, 1, 300)
    ax2.plot(c, linear_to_srgb(c), color="black", label="linearToSrgb (exact, piecewise)")
    ax2.plot(c, c ** (1 / 2.2), color="#a0aec0", ls="--", label="pow(c, 1/2.2) approximation")
    ax2.plot(c, c, color="#cbd5e0", ls=":", label="no encode (01–11)")
    ax2.set_xlabel("linear value")
    ax2.set_ylabel("stored byte / 255")
    ax2.legend(fontsize=8, frameon=False)
    ax2.set_title("sRGB encode: more codes for dark values")
    fig.suptitle("12: HDR → display", fontsize=11)
    return fig


# --- 14 ---------------------------------------------------------------------------------------------------------

@figure("frame_timeline", "techniques/14_compute_particles/diagrams/frame_timeline.svg")
def frame_timeline():
    """Two frames on one queue: which stage touches the particle buffer when, and the barriers between."""
    lanes = ["COMPUTE_SHADER\n(simulate: R+W)", "VERTEX_ATTRIBUTE_INPUT\n(draw: R)", "fragment / color output\n"
             "(HDR + tone map)"]
    fig, ax = plt.subplots(figsize=(11, 3.4))
    work = []  # (lane, start, length, label)
    for f, t0 in enumerate([0, 10]):
        work += [(0, t0 + 1, 2.5, f"dispatch {f}"), (1, t0 + 4.5, 2, f"points {f}"),
                 (2, t0 + 5, 3.5, "rasterize + tone map")]
    for lane, start, length, label in work:
        ax.add_patch(Rectangle((start, 2 - lane + 0.2), length, 0.6, fc=["#fbd38d", "#90cdf4", "#c6f6d5"][lane],
                               ec="#4a5568"))
        ax.text(start + length / 2, 2 - lane + 0.5, label, ha="center", va="center", fontsize=8)
    for t0 in [0, 10]:
        # barrier 2: compute write -> vertex attribute read (GL: glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT))
        arrow(ax, (t0 + 3.5, 2.2), (t0 + 4.5, 1.8), VK_COLOR)
        ax.text(t0 + 2.2, 1.95, "B2 (GL + VK)", fontsize=7, color=VK_COLOR, ha="right")
    # barrier 1: frame 0's vertex reads -> frame 1's compute writes (write-after-read: VK only)
    arrow(ax, (6.5, 1.2), (11, 2.2), "#c53030")
    ax.text(7.6, 2.35, "B1: WAR across frames (VK only)", fontsize=7, color="#c53030")
    ax.axvline(10, color="#cbd5e0", ls="--")
    ax.text(0.1, 3.05, "frame N", fontsize=9)
    ax.text(10.1, 3.05, "frame N+1", fontsize=9)
    ax.set_xlim(0, 20)
    ax.set_ylim(0, 3.3)
    ax.set_yticks([2.5, 1.5, 0.5])
    ax.set_yticklabels(lanes, fontsize=8)
    ax.set_xticks([])
    ax.set_title("14: one particle buffer, one queue — the two barriers (sync validation needs the shader-access "
                 "heuristic to see them)", fontsize=10)
    return fig


# --- 15 ---------------------------------------------------------------------------------------------------------

@figure("blelloch", "techniques/15_prefix_sum/diagrams/blelloch.svg")
def blelloch():
    """Runs scan_shared.comp's algorithm on GG3 ch39's 8-element example and draws every step."""
    values = [3, 1, 7, 0, 4, 1, 6, 3]
    n = len(values)
    temp = list(values)
    rows = [("input", list(temp), [])]
    offset, d = 1, n // 2
    while d > 0:  # up-sweep
        ops = []
        for t in range(d):
            a, b = offset * (2 * t + 1) - 1, offset * (2 * t + 2) - 1
            temp[b] += temp[a]
            ops.append((a, b, "+"))
        rows.append((f"up d={d}", list(temp), ops))
        offset *= 2
        d //= 2
    total = temp[-1]
    temp[-1] = 0
    rows.append(("clear root", list(temp), [(n - 1, n - 1, "0")]))
    d = 1
    while d < n:  # down-sweep
        offset //= 2
        ops = []
        for t in range(d):
            a, b = offset * (2 * t + 1) - 1, offset * (2 * t + 2) - 1
            left = temp[a]
            temp[a] = temp[b]
            temp[b] += left
            ops.append((a, b, "swap+"))
        rows.append((f"down d={d}", list(temp), ops))
        d *= 2
    expected = [sum(values[:i]) for i in range(n)]
    assert temp == expected, (temp, expected)

    fig, ax = plt.subplots(figsize=(9, 0.62 * len(rows) + 1))
    for r, (label, vals, ops) in enumerate(rows):
        y = len(rows) - 1 - r
        touched = {b for _, b, _ in ops} | {a for a, _, _ in ops}
        for i, v in enumerate(vals):
            fc = "#fbd38d" if i in touched and r > 0 else "#edf2f7"
            ax.add_patch(Rectangle((i, y + 0.1), 0.9, 0.7, fc=fc, ec="#a0aec0"))
            ax.text(i + 0.45, y + 0.45, str(v), ha="center", va="center", fontsize=9)
        ax.text(-0.2, y + 0.45, label, ha="right", va="center", fontsize=8)
        if r > 0:
            for a, b, kind in ops:
                if a != b:
                    color = "#2f855a" if kind == "+" else "#2b6cb0"
                    arrow(ax, (a + 0.45, y + 1.1), (b + 0.45, y + 0.8), color)
                    if kind != "+":  # the swap's other half: left <- old right
                        arrow(ax, (b + 0.3, y + 1.1), (a + 0.6, y + 0.8), "#90cdf4")
    phase_y = len(rows) - 1 - 4
    ax.text(n + 0.2, phase_y + 2.5, "up-sweep (reduce):\nleft child added\ninto right", fontsize=8, color="#2f855a")
    ax.text(n + 0.2, phase_y - 2.0, "down-sweep:\nright ← left + right,\nleft ← old right", fontsize=8,
            color="#2b6cb0")
    ax.set_xlim(-1.6, n + 2.2)
    ax.set_ylim(0, len(rows))
    ax.axis("off")
    ax.set_title(f"15: work-efficient exclusive scan (Blelloch, GG3 ch39) on 8 values — total {total} goes to the"
                 f" level above;\nscan_shared.comp does this for 512 values, each step separated by barrier()",
                 fontsize=10)
    return fig


@figure("scan_levels", "techniques/15_prefix_sum/diagrams/scan_levels.svg")
def scan_levels():
    """params.h scanLevels() for 4M elements: where each level lives in the scan buffer, and the pass order."""
    block, n = 512, 1 << 22
    levels, offset = [], 0
    while True:
        blocks = (n + block - 1) // block
        levels.append((n, offset, blocks))
        if blocks == 1:
            break
        offset += n
        n = blocks
    fig, ax = plt.subplots(figsize=(11, 2.6))
    x = 0.0
    widths = [6, 3, 1.6, 0.8]
    for k, ((count, off, blocks), w) in enumerate(zip(levels, widths)):
        ax.add_patch(Rectangle((x, 0.4), w, 0.8, fc=["#90cdf4", "#fbd38d", "#9ae6b4"][k % 3], ec="#4a5568"))
        ax.text(x + w / 2, 0.8, f"level {k}\n{count:,} values\n@ {off:,}", ha="center", va="center", fontsize=8)
        ax.text(x + w / 2, 0.15, f"scan: {blocks:,} workgroup{'s' if blocks > 1 else ''}", ha="center", fontsize=7)
        x += w
    ax.add_patch(Rectangle((x, 0.4), 0.5, 0.8, fc="#e2e8f0", ec="#4a5568"))
    ax.text(x + 0.25, 0.8, "total", ha="center", va="center", fontsize=7, rotation=90)
    ax.text(0, 1.45, "pass order:  scan L0 → scan L1 → scan L2  (each writes one sum per block into the next level)"
            ",  then  add L1 → L0  (each element += its block's scanned sum);  a barrier after every pass",
            fontsize=8)
    ax.set_xlim(0, x + 0.7)
    ax.set_ylim(0, 1.7)
    ax.axis("off")
    ax.set_title("15: multi-level scan of 4M values in one storage buffer (not to scale)", fontsize=10)
    return fig


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("names", nargs="*", help="figures to build (default: all)")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--preview", metavar="DIR", help="also write PNG copies into DIR")
    args = parser.parse_args()
    if args.list:
        for name, (path, _) in FIGURES.items():
            print(f"{name:20} {path}")
        return 0
    names = args.names or list(FIGURES)
    unknown = [n for n in names if n not in FIGURES]
    if unknown:
        print(f"unknown figure(s): {unknown}; --list shows them", file=sys.stderr)
        return 1
    for name in names:
        path, fn = FIGURES[name]
        save(fn(), path, args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
