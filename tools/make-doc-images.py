#!/usr/bin/env python3
"""Generate documentation images for the repo (screenshots redaction + diagrams).

Usage: python tools/make-doc-images.py [workspace-root] [repo-root]
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

WS = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", ".."))
REPO = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "docs", "images")
os.makedirs(OUT, exist_ok=True)


def pixelate(img, box, blocks=10):
    crop = img.crop(box)
    small = crop.resize((blocks, blocks), Image.Resampling.BILINEAR)
    img.paste(small.resize(crop.size, Image.Resampling.NEAREST), box)


def save_scaled(im, path, max_width=1920):
    if im.width > max_width:
        h = int(im.height * max_width / im.width)
        im = im.resize((max_width, h), Image.Resampling.LANCZOS)
    im.save(path, optimize=True)


def redact_screenshots():
    src = os.path.join(WS, "backup", "steam-ui-capture.png")
    if os.path.exists(src):
        im = Image.open(src).convert("RGB")
        w, h = im.size
        s = w / 1610.0  # preview width used when choosing coordinates
        # QR code area (Steam sign-in QR should not be published)
        pixelate(im, (int(215 * s), int(345 * s), int(575 * s), int(705 * s)), 8)
        save_scaled(im, os.path.join(OUT, "steam-big-picture-signin.png"))

    src = os.path.join(WS, "backup", "steam-zh.png")
    if os.path.exists(src):
        im = Image.open(src).convert("RGB")
        w, h = im.size
        s = w / 1610.0
        # profile avatar (top-right)
        pixelate(im, (int(1525 * s), int(4 * s), int(1605 * s), int(56 * s)), 6)
        save_scaled(im, os.path.join(OUT, "steam-big-picture-home.png"))


def font(size, bold=False):
    candidates = [
        r"C:\Windows\Fonts\segoeui.ttf",
        r"C:\Windows\Fonts\arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    for p in candidates:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


def box(draw, xy, text, fill="#1b2838", outline="#66c0f4", text_fill="#e6f2ff", f=None):
    x1, y1, x2, y2 = xy
    draw.rounded_rectangle(xy, radius=10, fill=fill, outline=outline, width=2)
    lines = text.split("\n")
    line_h = f.getbbox("Ag")[3] if hasattr(f, "getbbox") else 18
    line_h = int(line_h * 1.35)
    total_h = line_h * len(lines)
    cy = (y1 + y2) / 2 - total_h / 2
    for i, line in enumerate(lines):
        tw = draw.textlength(line, font=f)
        draw.text(((x1 + x2 - tw) / 2, cy + i * line_h), line, font=f, fill=text_fill)


def arrow(draw, x1, y1, x2, y2, color="#66c0f4"):
    draw.line((x1, y1, x2, y2), fill=color, width=3)
    import math
    ang = math.atan2(y2 - y1, x2 - x1)
    L = 14
    for da in (2.5, -2.5):
        draw.line((x2, y2, x2 - L * math.cos(ang + da / 2), y2 - L * math.sin(ang + da / 2)),
                  fill=color, width=3)


def boot_chain_diagram():
    W, H = 1800, 1000
    im = Image.new("RGB", (W, H), "#0e141b")
    d = ImageDraw.Draw(im)
    f = font(26)
    ft = font(42, bold=True)
    d.text((40, 30), "J716F boot chain (Arch Linux ARM + SteamOS-style session)", font=ft, fill="#e6f2ff")

    y = 150
    box(d, (40, y, 300, y + 90), "ABL / UEFI bootloader", f=f)
    box(d, (420, y, 820, y + 90), "boot_a / boot_b\nAndroid boot.img", f=f)
    box(d, (940, y, 1240, y + 90), "initramfs /init\n(busybox)", f=f)
    box(d, (1360, y, 1760, y + 90), "mount rootfs\nsda18 (userdata ext4)", f=f)
    arrow(d, 300, y + 45, 420, y + 45)
    arrow(d, 820, y + 45, 940, y + 45)
    arrow(d, 1240, y + 45, 1360, y + 45)

    y2 = 380
    box(d, (40, y2, 360, y2 + 100), "systemd", f=f)
    box(d, (480, y2, 800, y2 + 100), "greetd + gamescope\n(DRM/KMS direct scanout)", f=f)
    box(d, (920, y2, 1240, y2 + 100), "Steam Big Picture\n(Steam-ARM aarch64)", f=f)
    box(d, (1360, y2, 1760, y2 + 100), "FEX / ARM64 Proton\n(x86 games)", f=f)
    arrow(d, 1560, y + 90, 200, y2)
    arrow(d, 360, y2 + 50, 480, y2 + 50)
    arrow(d, 800, y2 + 50, 920, y2 + 50)
    arrow(d, 1240, y2 + 50, 1360, y2 + 50)

    y3 = 620
    box(d, (40, y3, 420, y3 + 100), "recovery_a / recovery_b\nmini-recovery\n(BCB boot-recovery)", f=f, outline="#f0a35e")
    box(d, (520, y3, 900, y3 + 100), "qbootctl -m\n(GPT A/B success flag)", f=f, outline="#a5e075")
    box(d, (1000, y3, 1380, y3 + 100), "rootfs.img.gz\ncard-flash via SD/USB", f=f, outline="#f0a35e")
    arrow(d, 230, y3, 230, y2 + 100, color="#f0a35e")
    arrow(d, 710, y3, 710, y2 + 100, color="#a5e075")
    d.text((40, 880), "recovery / release tooling (docs/83, docs/84, docs/85)", font=font(24), fill="#9fb3c8")
    im.save(os.path.join(OUT, "boot-chain.png"), optimize=True)


def release_flow_diagram():
    W, H = 1800, 850
    im = Image.new("RGB", (W, H), "#0e141b")
    d = ImageDraw.Draw(im)
    f = font(24)
    ft = font(42, bold=True)
    d.text((40, 30), "Release pipeline: image build + two flash routes", font=ft, fill="#e6f2ff")

    box(d, (40, 140, 420, 240), "build-rootfs.sh\n(rsync live system -> ext4 image)", f=f)
    box(d, (520, 120, 900, 200), "boot.img\n(kernel + DTB + initramfs)", f=f)
    box(d, (520, 240, 900, 320), "rootfs.img.gz\n(card-flash payload)", f=f)
    box(d, (520, 360, 900, 440), "rootfs.simg\n(fastboot sparse image)", f=f)
    arrow(d, 420, 190, 520, 160)
    arrow(d, 420, 190, 520, 280)
    arrow(d, 420, 190, 520, 400)

    box(d, (1040, 100, 1420, 200), "SD card /j716f/\nboot.img + rootfs.img.gz", f=f, outline="#f0a35e")
    box(d, (1040, 260, 1420, 360), "mini-recovery\n(recovery_a/b)", f=f, outline="#f0a35e")
    box(d, (1040, 420, 1420, 520), "Windows install.ps1\nfastboot flash", f=f, outline="#66c0f4")
    box(d, (1500, 240, 1760, 340), "boot_a/b + userdata\n(no vendor partitions)", f=f, outline="#a5e075")
    arrow(d, 900, 280, 1040, 150, color="#f0a35e")
    arrow(d, 1140, 200, 1140, 260, color="#f0a35e")
    arrow(d, 1420, 310, 1500, 290, color="#f0a35e")
    arrow(d, 900, 400, 1040, 470)
    arrow(d, 1420, 470, 1500, 300)
    im.save(os.path.join(OUT, "release-flow.png"), optimize=True)


def partition_layout_diagram():
    W, H = 1800, 1000
    im = Image.new("RGB", (W, H), "#0e141b")
    d = ImageDraw.Draw(im)
    f = font(22)
    ft = font(40, bold=True)
    d.text((40, 25), "J716F storage layout (UFS LUNs + key partitions)", font=ft, fill="#e6f2ff")

    rows = [
        ("sda (117 GB)", ["ssd", "persist", "misc", "keystore", "frp", "recovery_a", "recovery_b",
                          "metadata", "super (8G, rescue)", "userdata (108.3G, rootfs)"]),
        ("sdb/sdc (8 MB)", ["xbl_a", "xbl_b", "xbl_config_a/b"]),
        ("sdd (32 MB)", ["ALIGN_TO_128K_1", "cdt", "ddr", "mdmddr"]),
        ("sde (2 GB)", ["aop", "tz", "hyp", "modem", "abl", "boot_a", "boot_b", "dtbo",
                        "vbmeta", "uefivarstore", "devinfo", "..."]),
        ("sdf (2 GB)", ["fsg", "fsc", "modemst1", "modemst2", "mdm1m9kefs*"]),
    ]
    y = 110
    for title, parts in rows:
        d.text((40, y), title, font=font(24, bold=True), fill="#9fb3c8")
        x = 360
        for p in parts:
            color = "#66c0f4"
            if "boot_a" in p or "boot_b" in p or "recovery" in p:
                color = "#a5e075"
            elif "userdata" in p or "super" in p:
                color = "#f0a35e"
            elif "persist" in p or "modem" in p or "fsg" in p:
                color = "#e06c75"
            box(d, (x, y - 8, x + 190, y + 42), p, f=font(18), outline=color)
            x += 200
            if x > W - 200:
                break
        y += 100

    d.text((40, 700), "green = boot/recovery (flashable, same boot.img format)", font=font(22), fill="#a5e075")
    d.text((40, 745), "orange = rootfs / rescue payload", font=font(22), fill="#f0a35e")
    d.text((40, 790), "red = device-unique calibration / modem data - NEVER flash or erase", font=font(22), fill="#e06c75")
    d.text((40, 870), "boot_a/boot_b = 96 MiB each; recovery_a/b = 100 MiB each; misc = 1 MiB (BCB)",
           font=font(22), fill="#9fb3c8")
    im.save(os.path.join(OUT, "partition-layout.png"), optimize=True)


def abl_flags_diagram():
    W, H = 1800, 760
    im = Image.new("RGB", (W, H), "#0e141b")
    d = ImageDraw.Draw(im)
    ft = font(40, bold=True)
    f = font(24)
    d.text((40, 25), "ABL slot flags: GPT partition attribute byte 54", font=ft, fill="#e6f2ff")

    bits = [
        ("7", "unbootable", "#e06c75"),
        ("6", "successful", "#a5e075"),
        ("5", "retry hi", "#66c0f4"),
        ("4", "retry mid", "#66c0f4"),
        ("3", "retry lo", "#66c0f4"),
        ("2", "active", "#f0a35e"),
        ("1", "priority", "#9fb3c8"),
        ("0", "priority", "#9fb3c8"),
    ]
    x = 120
    for bit, label, color in bits:
        box(d, (x, 140, x + 180, 260), f"bit {bit}\n{label}", f=f, outline=color)
        x += 200

    d.text((40, 330), "retry count = bits 3..5 (0-7); decremented on every boot unless bit 6 is set.",
           font=f, fill="#e6f2ff")
    d.text((40, 385), "qbootctl -m  ->  bit6 = 1  (mark boot successful; stops the counter)", font=f, fill="#a5e075")
    d.text((40, 440), "qbootctl -s  ->  writes 0x0F (overwrites retry bits) - do NOT use to switch slots",
           font=f, fill="#e06c75")
    d.text((40, 495), "fastboot set_active a  ->  ABL-side reset (retry=7, clears unbootable)", font=f, fill="#66c0f4")
    d.text((40, 580), "Observed via: strings /dev/disk/by-partlabel/logfs | grep -i retry",
           font=font(22), fill="#9fb3c8")
    d.text((40, 625), "Attribute diffs: tools/gpt_slot_attrs.py /dev/sde", font=font(22), fill="#9fb3c8")
    im.save(os.path.join(OUT, "abl-slot-flags.png"), optimize=True)


def mini_recovery_flow():
    W, H = 1800, 700
    im = Image.new("RGB", (W, H), "#0e141b")
    d = ImageDraw.Draw(im)
    ft = font(40, bold=True)
    f = font(23)
    d.text((40, 25), "Card-flash flow without a PC (mini-recovery)", font=ft, fill="#e6f2ff")

    box(d, (30, 130, 300, 230), "running system\n(sudo j716f-reboot-recovery)", f=f)
    box(d, (350, 130, 620, 230), "write misc BCB\nboot-recovery", f=f)
    box(d, (670, 130, 940, 230), "reboot -> ABL\nboots recovery_a/b", f=f)
    box(d, (990, 130, 1260, 230), "scan SD/USB\n/j716f/", f=f)
    box(d, (1310, 130, 1580, 230), "SHA256 verify\n(fail = no writes)", f=f)
    arrow(d, 300, 180, 350, 180)
    arrow(d, 620, 180, 670, 180)
    arrow(d, 940, 180, 990, 180)
    arrow(d, 1260, 180, 1310, 180)

    box(d, (990, 330, 1260, 440), "write boot_a/b\n(optional rootfs.img.gz)", f=f, outline="#f0a35e")
    box(d, (1310, 330, 1580, 440), "qbootctl -m\nmark slot successful", f=f, outline="#a5e075")
    box(d, (670, 330, 940, 440), "clear BCB\n(no recovery loop)", f=f)
    box(d, (350, 330, 620, 440), "reboot\nback to system", f=f)
    arrow(d, 1420, 230, 1420, 330)
    arrow(d, 1310, 385, 1260, 385)
    arrow(d, 990, 385, 940, 385)
    arrow(d, 670, 385, 620, 385)

    d.text((40, 540), "Safety: SHA256 gate, boot magic check, BCB cleared early, 90 s watchdog, auto-reboot.",
           font=font(22), fill="#9fb3c8")
    d.text((40, 585), "Payload: /j716f/{SHA256SUMS,boot.img,rootfs.img.gz,HOLD}", font=font(22), fill="#9fb3c8")
    im.save(os.path.join(OUT, "mini-recovery-flow.png"), optimize=True)


if __name__ == "__main__":
    redact_screenshots()
    boot_chain_diagram()
    release_flow_diagram()
    partition_layout_diagram()
    abl_flags_diagram()
    mini_recovery_flow()
    print("images written to", OUT)
