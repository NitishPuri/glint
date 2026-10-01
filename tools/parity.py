#!/usr/bin/env python3
"""GL/VK parity check: render every technique in both apps and compare the images.

For each technique that exists in both glint_gl and glint_vk:
  1. run each app with the same window size, a fixed time step (--fixed-dt) and --screenshot
     (screenshots are taken before the UI is drawn, and camera input is ignored in screenshot runs)
  2. compare the two PNGs with ImageMagick: PSNR in dB ("inf" = identical) and the number of pixels that
     differ by more than --fuzz
  3. write <out>/<technique>_{gl,vk,diff,side}.png for a look

A low PSNR means the APIs disagree: a Y flip, depth range, winding, sRGB or uniform-layout mistake on one
side. Exit code 1 if any technique is below --min-psnr (or failed to run).

  tools/parity.py                         # all techniques, Debug build
  tools/parity.py 07_render_to_texture    # just one
  tools/parity.py --build build/release --min-psnr 35
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def techniques(app):
    out = subprocess.run([str(app), "--help"], capture_output=True, text=True, check=True).stdout
    names = out.split("techniques:", 1)[1].split()
    return names


def run(app, technique, png, args):
    cmd = [str(app), technique, "--size", args.size, "--fixed-dt", str(args.fixed_dt), "--frames", str(args.frames),
           "--screenshot", str(png), "--no-log-file"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    # Exit 2 = rendered fine but the debug layer reported issues: still worth comparing, but flag it.
    return result.returncode, result.stdout + result.stderr


def compare(a, b, fuzz):
    # ImageMagick prints the metric on stderr; exit code 1 just means "images differ".
    psnr = subprocess.run(["compare", "-metric", "PSNR", str(a), str(b), "null:"], capture_output=True, text=True)
    pixels = subprocess.run(["compare", "-metric", "AE", "-fuzz", fuzz, str(a), str(b), "null:"],
                            capture_output=True, text=True)
    value = psnr.stderr.strip().split()[0]
    return (float("inf") if value == "inf" else float(value)), int(float(pixels.stderr.strip().split()[0]))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("techniques", nargs="*", help="default: every technique both apps have")
    parser.add_argument("--build", default=str(ROOT / "build" / "debug"), help="build directory with the apps")
    parser.add_argument("--out", default=None, help="output directory (default: <build>/parity)")
    parser.add_argument("--size", default="800x600")
    parser.add_argument("--fixed-dt", type=float, default=1.0 / 60.0, help="seconds per frame")
    parser.add_argument("--frames", type=int, default=30, help="frames to run before the screenshot")
    parser.add_argument("--min-psnr", type=float, default=30.0, help="pass threshold in dB")
    parser.add_argument("--fuzz", default="2%", help="per-pixel tolerance for the differing-pixel count")
    args = parser.parse_args()

    build = Path(args.build)
    gl, vk = build / "glint_gl", build / "glint_vk"
    out = Path(args.out) if args.out else build / "parity"
    out.mkdir(parents=True, exist_ok=True)
    names = args.techniques or [t for t in techniques(gl) if t in set(techniques(vk))]

    print(f"{'technique':30} {'PSNR':>8} {'pixels':>8}  result")
    failed = 0
    for t in names:
        pngs = {api: out / f"{t}_{api}.png" for api in ("gl", "vk")}
        codes = {}
        for api, app in (("gl", gl), ("vk", vk)):
            codes[api], log = run(app, t, pngs[api], args)
            if not pngs[api].exists() or codes[api] not in (0, 2):
                print(f"{t:30} {'-':>8} {'-':>8}  FAIL ({api} exited {codes[api]})\n{log[-800:]}")
                failed += 1
                break
        else:
            psnr, pixels = compare(pngs["gl"], pngs["vk"], args.fuzz)
            diff = out / f"{t}_diff.png"
            subprocess.run(["compare", "-fuzz", args.fuzz, str(pngs["gl"]), str(pngs["vk"]), str(diff)],
                           capture_output=True)
            subprocess.run(["convert", str(pngs["gl"]), str(pngs["vk"]), "+append", str(out / f"{t}_side.png")],
                           capture_output=True)
            ok = psnr >= args.min_psnr
            notes = [api + " debug issues" for api in ("gl", "vk") if codes[api] == 2]
            failed += 0 if ok and not notes else 1
            status = "ok" if ok else f"FAIL (< {args.min_psnr:g} dB)"
            print(f"{t:30} {psnr:8.2f} {pixels:8d}  {status}{'  ' + ', '.join(notes) if notes else ''}")

    print(f"\n{len(names) - failed}/{len(names)} passed; images in {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
