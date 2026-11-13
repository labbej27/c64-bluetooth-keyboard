#!/usr/bin/env python3
"""Génère les keymaps VICE (GTK3, positional user) correspondant au firmware.

Usage : python3 gen_vkm.py gtk3_pos.vkm
  gtk3_pos.vkm = keymap positionnelle officielle de VICE (pour les entêtes et
  les touches spéciales indépendantes de la disposition).
Produit c64_ble_mac_fr.vkm et c64_ble_linux_fr.vkm (dans le dossier courant).
gtk3_pos.vkm se trouve dans le dossier data/C64 (ou share/vice/C64) de VICE.
"""
import sys

# Touche C64 -> (PA, PB)
C64 = {
    "1": (7, 0), "2": (7, 3), "3": (1, 0), "4": (1, 3), "5": (2, 0),
    "6": (2, 3), "7": (3, 0), "8": (3, 3), "9": (4, 0), "0": (4, 3),
    "+": (5, 0), "-": (5, 3), "<-": (7, 1), "@": (5, 6), "*": (6, 1),
    "up": (6, 6), ":": (5, 5), ";": (6, 2), "=": (6, 5), ",": (5, 7),
    ".": (5, 4), "/": (6, 7),
}
LETTERS = {"A": (1, 2), "B": (3, 4), "C": (2, 4), "D": (2, 2), "E": (1, 6), "F": (2, 5),
           "G": (3, 2), "H": (3, 5), "I": (4, 1), "J": (4, 2), "K": (4, 5), "L": (5, 2),
           "M": (4, 4), "N": (4, 7), "O": (4, 6), "P": (5, 1), "Q": (7, 6), "R": (2, 1),
           "S": (1, 5), "T": (2, 6), "U": (3, 6), "V": (3, 7), "W": (1, 1), "X": (2, 7),
           "Y": (3, 1), "Z": (1, 4)}

# Touche physique envoyée par le firmware (code HID, position US) -> touche C64
FW = {"AE01": "1", "AE02": "2", "AE03": "3", "AE04": "4", "AE05": "5", "AE06": "6",
      "AE07": "7", "AE08": "8", "AE09": "9", "AE10": "0", "AE11": "+", "AE12": "-",
      "TLDE": "<-", "AD11": "@", "AD12": "*", "BKSL": "up", "AC11": ";",
      "AB07": ",", "AB08": ".", "AB09": ":", "AB10": "="}
# Touches indépendantes de la disposition (keysym -> touche C64)
NAV = {"End": "£", "Page_Down": "/"}
C64["£"] = (6, 0)

# keysyms produits par chaque touche physique (toutes variantes possibles)
LAYOUTS = {
    "mac_fr": {
        "AE01": ["ampersand", "1"], "AE02": ["eacute", "2"], "AE03": ["quotedbl", "3"],
        "AE04": ["apostrophe", "4"], "AE05": ["parenleft", "5"], "AE06": ["section", "6"],
        "AE07": ["egrave", "7"], "AE08": ["exclam", "8"], "AE09": ["ccedilla", "9"],
        "AE10": ["agrave", "0"], "AE11": ["parenright", "degree"], "AE12": ["minus", "underscore"],
        # touche en haut à gauche : macOS peut l'inverser avec la touche < > (clavier ISO)
        "TLDE": ["at", "numbersign", "less", "greater"],
        "AD11": ["dead_circumflex", "dead_diaeresis", "asciicircum", "diaeresis"],
        "AD12": ["dollar", "asterisk"],
        "BKSL": ["dead_grave", "grave", "sterling"],
        "AC11": ["ugrave", "percent"],
        "AB07": ["comma", "question"], "AB08": ["semicolon", "period"],
        "AB09": ["colon", "slash"], "AB10": ["equal", "plus"],
    },
    "linux_fr": {  # fr(latin9) tel que relevé sur le PC
        "AE01": ["ampersand", "1"], "AE02": ["eacute", "2"], "AE03": ["quotedbl", "3"],
        "AE04": ["apostrophe", "4"], "AE05": ["parenleft", "5"], "AE06": ["minus", "6"],
        "AE07": ["egrave", "7"], "AE08": ["underscore", "8"], "AE09": ["ccedilla", "9"],
        "AE10": ["agrave", "0"], "AE11": ["parenright", "degree"], "AE12": ["equal", "plus"],
        "TLDE": ["oe", "OE", "twosuperior", "threesuperior"],
        "AD11": ["dead_circumflex", "dead_diaeresis"],
        "AD12": ["dollar", "sterling"],
        "BKSL": ["asterisk", "mu"],
        "AC11": ["ugrave", "percent"],
        "AB07": ["comma", "question"], "AB08": ["semicolon", "period"],
        "AB09": ["colon", "slash"], "AB10": ["exclam", "section"],
    },
}

KEEP = {"BackSpace", "Delete", "Insert", "Return", "Left", "Right", "Up", "Down",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "Shift_L", "Shift_R",
        "Caps_Lock", "Home", "Page_Up", "Prior", "Tab", "ISO_Left_Tab",
        "space", "Control_L", "Escape"}


def build(pos_lines, layout, name):
    out = [f"# VICE keymap (GTK3, positional user) - clavier C64C via ESP32 BLE - hote {name}",
           "# Genere par gen_vkm.py. keysym  PA PB  flags (8 = shift transmis)", ""]
    for l in pos_lines:
        s = l.strip()
        if s.startswith("!"):
            out.append(s)
            continue
        t = s.split()
        if t and not s.startswith("#") and (t[0] in KEEP or t[1:2] == ["-5"]):
            out.append(s)
    out.append("")
    seen = set()

    def add(sym, key):
        assert sym not in seen, f"doublon {sym}"
        seen.add(sym)
        r, c = C64[key]
        out.append(f"{sym:<16} {r} {c} 8")

    for sym, key in NAV.items():
        add(sym, key)
    for phys, key in FW.items():
        for sym in layout[phys]:
            add(sym, key)
    for L, (r, c) in LETTERS.items():
        for sym in (L, L.lower()):
            assert sym not in seen
            seen.add(sym)
            out.append(f"{sym} {r} {c} 8")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    pos = open(sys.argv[1]).read().splitlines()
    for name, lay in LAYOUTS.items():
        with open(f"c64_ble_{name}.vkm", "w") as f:
            f.write(build(pos, lay, name))
        print("ok", name)
