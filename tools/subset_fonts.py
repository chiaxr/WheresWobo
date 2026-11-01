# Rebuilds the embedded font subsets in assets/fonts. Usage:
#   python3 tools/subset_fonts.py <dir with DejaVuSans.ttf, DejaVuSans-Bold.ttf, LXGWWenKaiGBLite-Regular.ttf>
# (TeX Live has all three: texmf-dist/fonts/truetype/public/{dejavu,lxgw-fonts}.)
#
# The UI faces keep ASCII and a few symbols, plus the accented letters in map
# names. The chalk face (handwriting, for blackboards and signs) keeps ASCII
# plus every other character the maps' writing uses, so rerun this after
# adding writing to a map. Requires fontTools.
import glob
import os
import sys

from fontTools import subset

src = sys.argv[1]
root = os.path.join(os.path.dirname(__file__), "..")
out = os.path.join(root, "assets", "fonts")

ascii_chars = "".join(chr(c) for c in range(32, 127))
ui_extra = "°·×•…←↑→↓ö"
map_chars = set()
for path in glob.glob(os.path.join(root, "data", "maps", "*", "map.txt")):
    with open(path, encoding="utf-8") as f:
        map_chars |= {ch for ch in f.read() if ord(ch) > 126}


def make(font, name, chars):
    options = subset.Options()
    options.layout_features = []
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.hinting = False
    options.drop_tables += ["FFTM", "kern", "GPOS", "GSUB"]
    fnt = subset.load_font(os.path.join(src, font), options)
    sub = subset.Subsetter(options)
    sub.populate(unicodes=[ord(c) for c in chars])
    sub.subset(fnt)
    subset.save_font(fnt, os.path.join(out, name), options)
    print(f"{name}: {len(set(chars))} characters")


make("DejaVuSans.ttf", "dejavu_sans_subset.ttf", ascii_chars + ui_extra)
make("DejaVuSans-Bold.ttf", "dejavu_sans_bold_subset.ttf", ascii_chars + ui_extra)
make("LXGWWenKaiGBLite-Regular.ttf", "chalk_subset.ttf", ascii_chars + "".join(sorted(map_chars)))
