#!/usr/bin/env python3
"""Printable STM32L071RBT6 (LQFP64) pinout + SWD continuity sheet for the ANDRTF3 board."""
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import Rectangle, Circle

# LQFP64, datasheet DS10685 figure 5 / table 15
PINS = [None,
    "VDD", "PC13", "PC14-OSC32_IN", "PC15-OSC32_OUT", "PH0-OSC_IN", "PH1-OSC_OUT", "NRST", "PC0",
    "PC1", "PC2", "PC3", "VSSA", "VDDA", "PA0", "PA1", "PA2",
    "PA3", "VSS", "VDD", "PA4", "PA5", "PA6", "PA7", "PC4",
    "PC5", "PB0", "PB1", "PB2", "PB10", "PB11", "VSS", "VDD",
    "PB12", "PB13", "PB14", "PB15", "PC6", "PC7", "PC8", "PC9",
    "PA8", "PA9", "PA10", "PA11", "PA12", "PA13", "VSS", "VDDIO2",
    "PA14", "PA15", "PC10", "PC11", "PC12", "PD2", "PB3", "PB4",
    "PB5", "PB6", "PB7", "BOOT0", "PB8", "PB9", "VSS", "VDD"]
assert len(PINS) == 65

SWD = {46: "SWDIO", 49: "SWCLK"}
COLORS = {"swd": "#d62728", "nrst": "#ff7f0e", "boot": "#1f77b4", "vdd": "#2ca02c", "gnd": "#555555"}

def pin_class(n):
    name = PINS[n]
    if n in SWD: return "swd"
    if name == "NRST": return "nrst"
    if name == "BOOT0": return "boot"
    if name.startswith("VDD"): return "vdd"
    if name.startswith("VSS"): return "gnd"
    return None

def std_pos(n):
    """Standard top view, dimple top-left. Returns (x, y) of pin tip on a [-1,1] body, side."""
    i = (n - 1) % 16
    t = -0.9375 + i * 0.125            # 16 pins spread along a side
    if n <= 16:  return (-1.0, -t)     # left, top->bottom
    if n <= 32:  return (t, -1.0)      # bottom, left->right
    if n <= 48:  return (1.0, t)       # right, bottom->top
    return (-t, 1.0)                   # top, right->left

def rotate(p, quarter_turns_cw):
    x, y = p
    for _ in range(quarter_turns_cw):
        x, y = y, -x
    return x, y

def disp(n):
    """Pin name as shown on the diagram (ground pins tagged GND)."""
    name = PINS[n]
    return f"{name}=GND" if name.startswith("VSS") else name

def draw_chip(ax, turns, title):
    ax.set_xlim(-2.6, 2.6); ax.set_ylim(-2.6, 2.6); ax.set_aspect("equal"); ax.axis("off")
    ax.add_patch(Rectangle((-1, -1), 2, 2, fc="#222", ec="black"))
    dx, dy = rotate((-0.78, 0.78), turns)
    ax.add_patch(Circle((dx, dy), 0.09, fc="#aaa"))              # pin-1 dimple
    ex, ey = rotate((0.72, -0.72), turns)
    ax.add_patch(Circle((ex, ey), 0.13, fc="#444", ec="#666"))   # ejector mark
    ax.text(0, 0.1, "STM32L071RBT6", color="white", ha="center", fontsize=8)
    ax.text(0, -0.15, "LQFP64 - top view", color="#bbb", ha="center", fontsize=6)
    ax.set_title(title, fontsize=9)
    for n in range(1, 65):
        x, y = rotate(std_pos(n), turns)
        cls = pin_class(n)
        col = COLORS.get(cls, "#999")
        lead = 0.14
        if abs(x) >= 0.99:   # left/right side
            s = 1 if x > 0 else -1
            ax.plot([x, x + s * lead], [y, y], color=col, lw=2.2 if cls else 1.2)
            label = f"{n} {disp(n)}" if s > 0 else f"{disp(n)} {n}"
            if n in SWD: label += f"  [{SWD[n]}]" if s > 0 else ""
            if n in SWD and s < 0: label = f"[{SWD[n]}]  " + label
            ax.text(x + s * (lead + 0.04), y, label, fontsize=4.6, va="center",
                    ha="left" if s > 0 else "right", color=col if cls else "black",
                    fontweight="bold" if cls else "normal")
        else:                # top/bottom side
            s = 1 if y > 0 else -1
            ax.plot([x, x], [y, y + s * lead], color=col, lw=2.2 if cls else 1.2)
            label = f"{n} {disp(n)}" if s > 0 else f"{disp(n)} {n}"
            if n in SWD: label = (label + f" [{SWD[n]}]") if s > 0 else (f"[{SWD[n]}] " + label)
            ax.text(x, y + s * (lead + 0.04), label, fontsize=4.6, rotation=90,
                    ha="center", va="bottom" if s > 0 else "top", color=col if cls else "black",
                    fontweight="bold" if cls else "normal")

def page_diagrams(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    fig.suptitle("ANDRTF3 controller - STM32L071RBT6 (LQFP64) pinout", fontsize=13, y=0.993)
    ax1 = fig.add_axes([0.02, 0.50, 0.96, 0.465])
    draw_chip(ax1, 0, "Datasheet orientation: pin-1 dimple top-left, pins count counter-clockwise")
    ax2 = fig.add_axes([0.02, 0.06, 0.96, 0.46])
    draw_chip(ax2, 1, "Rotated 90 deg CW: dimple top-right, as in photos/back-mcu-swd-header.jpg")
    leg = [("SWDIO / SWCLK", "swd"), ("NRST", "nrst"), ("BOOT0", "boot"), ("VDD / VDDA / VDDIO2", "vdd"), ("GND = VSS / VSSA", "gnd")]
    for i, (t, c) in enumerate(leg):
        fig.text(0.06 + i * 0.185, 0.035, "■ " + t, color=COLORS[c], fontsize=8)
    fig.text(0.06, 0.015, "Small dimple = pin 1. The big round mark is the mould ejector, NOT pin 1. "
             "The marking text on this chip is printed rotated - ignore it for orientation.", fontsize=7)
    pdf.savefig(fig); plt.close(fig)

def page_tables(pdf):
    fig = plt.figure(figsize=(8.27, 11.69))
    fig.text(0.06, 0.955, "SWD connections (measured 2026-10-24, see docs/connectors.md)", fontsize=13, weight="bold")

    rows = [["SWDIO (PA13)", "46", "row 2 right", "", "4  T_SWDIO"],
            ["SWCLK (PA14)", "49", "row 3 right", "", "6  T_SWCLK"],
            ["NRST", "7", "-", "row 5 left", "12  T_NRST"],
            ["BOOT0", "60", "", "", "- (do not connect)"],
            ["VDD", "1 / 64 / 19 / 32", "row 1 left", "", "3  T_VCC (sense only)"],
            ["GND (VSS)", "63 / 47 / 18 / 31 (VSSA 12)", "row 4 left", "", "5 / 7  GND"],
            ["SWO: PB3 is RS485 DE", "55", "", "", "8  T_SWO (leave open)"],
            ["TX (debug, PC12)", "53", "row 3 left", "", "13 T_VCP_RX (optional)"],
            ]
    ax = fig.add_axes([0.05, 0.56, 0.90, 0.37]); ax.axis("off")
    t = ax.table(cellText=rows,
                 colLabels=["Signal", "MCU pin(s)", "2x4 header contact", "2x5 pad", "STDC14 (STLINK-V3MINIE)"],
                 loc="upper center", cellLoc="left", colWidths=[0.22, 0.18, 0.2, 0.14, 0.26])
    t.auto_set_font_size(False); t.set_fontsize(8.5); t.scale(1, 2.2)
    for (r, c), cell in t.get_celld().items():
        if r == 0: cell.set_text_props(weight="bold"); cell.set_facecolor("#ddd")
        if c in (2, 3) and r > 0: cell.set_facecolor("#fffbe6")

    fig.text(0.06, 0.53, "STDC14 connector on STLINK-V3MINIE (1.27 mm, 2x7)", fontsize=11, weight="bold")
    stdc = [["1", "reserved (NC)", "2", "reserved (NC)"],
            ["3", "T_VCC (target voltage sense)", "4", "T_SWDIO / TMS"],
            ["5", "GND", "6", "T_SWCLK / TCK"],
            ["7", "GND", "8", "T_SWO / TDO"],
            ["9", "T_JRCLK (NC for SWD)", "10", "T_JTDI (NC for SWD)"],
            ["11", "GNDDetect", "12", "T_NRST"],
            ["13", "T_VCP_RX (probe input)", "14", "T_VCP_TX (probe output)"]]
    ax = fig.add_axes([0.05, 0.30, 0.90, 0.22]); ax.axis("off")
    t = ax.table(cellText=stdc, colLabels=["Pin", "Signal", "Pin", "Signal"], loc="upper center",
                 cellLoc="left", colWidths=[0.08, 0.40, 0.08, 0.40])
    t.auto_set_font_size(False); t.set_fontsize(8.5); t.scale(1, 1.7)
    for (r, c), cell in t.get_celld().items():
        if r == 0: cell.set_text_props(weight="bold"); cell.set_facecolor("#ddd")
    fig.text(0.06, 0.285, "STDC14 pinout per ST STLINK-V3 user manuals (UM2448/UM2910) - "
             "the data brief on disk has no pinout; confirm pin 1 marking on the cable.", fontsize=7, style="italic")

    notes = [
        "Rules",
        "1. Continuity test with the board UNPOWERED.",
        "2. For SWD, power the sensor from its own supply (24 V terminals). T_VCC only senses the",
        "   target voltage - the STLINK-V3MINIE does not power the board.",
        "3. Minimum wiring: SWDIO, SWCLK, GND, T_VCC. Add NRST for connect-under-reset.",
        "4. Leave BOOT0 unconnected. It is normally pulled to GND by a resistor - note its value.",
        "5. Nothing gets written to the chip before a verified dump exists (scripts/probe.sh, scripts/dump.sh).",
        "",
        "Board parts for reference: ISO3082 (isolated RS485), MCP3551/3553 (PT1000 ADC, SPI),",
        "2x 74HC165 (DIP switches S1/S2), LCD via zebra strip, B8S bridge + buck to 3.3 V.",
    ]
    for i, line in enumerate(notes):
        fig.text(0.06, 0.25 - i * 0.02, line, fontsize=9, weight="bold" if i == 0 else "normal")
    pdf.savefig(fig); plt.close(fig)

out = sys.argv[1] if len(sys.argv) > 1 else "STM32L071RBT6_LQFP64_pinout.pdf"
with PdfPages(out) as pdf:
    page_diagrams(pdf)
    page_tables(pdf)
print(out)
