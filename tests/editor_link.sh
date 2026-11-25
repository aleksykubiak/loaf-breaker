#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The sound editor's link (LOAF BREAKER 0.9.43, the site's editor/ page; ui/main.c ed_sysex) end to end in the host
# simulator: the Studio's ED_X (status, pages, a track written, the patterns, RGB, transport, the live scope). BABSZTYL:
# the sound editor's own commands (7-12: ED_INFO, the kick's and the drums' parts, their sequencer) gone with the
# drums, unanswered, the screen where it was.
# Needs ./build.sh --host first.
#   tests/editor_link.sh
set -e
cd "$(dirname "$0")/.."
O=app/build/editor
mkdir -p "$O"
fr() { python3 -c "import sys; sys.path.insert(0,'tools'); import cu_frames as c; print('sysex ' + c.hexs(c.frame($1, bytes([$2]))))"; }
{
echo "wait 4200"
echo "sysex_log $PWD/$O/out.syx"
echo "sysex_out"
echo "expect page 0"
fr 7 ""                                   # ED_INFO: gone with the drums (BABSZTYL): no answer
echo "wait 50"
fr 12 "1, 0, 1, 0, 0"                      # ED_STEP: the same
echo "wait 50"
fr 13 "1, 0, 0, 0, 0, 0"                   # ED_X STATUS
echo "wait 50"
fr 13 "2, 5, 2, 0, 3, 1"                   # MIX's MASTER: its MAX OUT one click
echo "wait 50"
fr 13 "2, 2, 1, 0, 4, 1"                   # FX slot 2: the effect one on
echo "wait 50"
fr 13 "2, 3, 5, 0, 1, 5"                   # SYSTEM's RESET: refused
echo "wait 50"
fr 13 "4, 1, 1, 0, 5, 9, 254$(python3 -c "print(', 0' * 29 + ', 4, 2, 1' + ', 1' * 29 + ', 0' * 32 + ', 3, 0, 0' + ', 0' * 29)")"   # TRACK 2 written: P5 x4, P9 x2, a DROP
echo "wait 50"
fr 13 "6, 0"                               # the patterns P1-16
echo "wait 50"
fr 13 "7, 4, 0, 0"                         # the break's RGB wave
echo "wait 50"
fr 13 "8, 4, 0, 0"                         # the breaks' names
echo "wait 50"
fr 13 "8, 1, 124, 6"                       # the tempo 166.0
echo "wait 50"
fr 13 "8, 3, 0, 0"                         # PLAY
echo "wait 400"
fr 13 "1, 1, 0, 0, 0, 0"                   # STATUS with the live scope
echo "wait 50"
echo "expect page 0"
echo "sysex_log off"
} > "$O/editor.txt"
rm -f "$O/out.syx"
app/build/host/loaf-breaker_host "$O/editor.txt" app/build/scenarios > "$O/run.log" 2>&1 || { grep -E "FAIL|host:" "$O/run.log"; exit 1; }
python3 - "$O/out.syx" <<'P'
import sys
raw = open(sys.argv[1], "rb").read()
def unpack(p):
    u = bytearray()
    for i in range(0, len(p), 8):
        hi = p[i]
        u += bytes(p[i + j] | ((hi >> (j - 1)) & 1) << 7 for j in range(1, min(8, len(p) - i)))
    return bytes(u)
rep = []
for f in raw.split(b"\xf0")[1:]:
    f = f.split(b"\xf7")[0]
    if f[:3] == bytes([0x7D, 0x43, 0x48]) and 0x47 <= f[3] <= 0x4D:
        rep.append((f[3] & 15, f[4], unpack(f[5:])))
def knobs(d):
    return [(d[2 + 29 * i:14 + 29 * i].rstrip(b"\0").decode(), d[14 + 29 * i:30 + 29 * i].rstrip(b"\0").decode(), d[30 + 29 * i]) for i in range(4)]
assert not any(r[0] in (7, 8, 9, 10, 11, 12) for r in rep), "the sound editor's answers (gone with the drums)"
x = rep
assert all(r[0] == 13 for r in x) and len(x) == 11, f"ED_X {[(r[0], len(r[2])) for r in x]}"
st, mx, fxs, rst, trk, bnk, rgb, names, tempo, play, live = x
def s12(d, a, n=12):
    return d[a:a + n].split(b"\0")[0].decode()
assert st[2][0] == 1 and (st[2][3] | st[2][4] << 8) >= 400, "STATUS"
assert s12(mx[2], 4 + 3 * 29) == "MAX OUT" and mx[1] == 0, f"MIX MASTER {s12(mx[2], 4 + 3 * 29)}"
assert fxs[1] == 0 and len(fxs[2]) > 120 and s12(fxs[2], 120), "an FX slot's effect"
assert rst[1] == 2, "RESET refused"
t = trk[2]
assert list(t[7:11]) == [5, 9, 254, 0] and t[39] == 4 and t[40] == 2, f"TRACK 2 {list(t[7:11])} {list(t[39:42])}"
assert len(bnk[2]) == 2 + 16 * 40, "the patterns"
assert len(rgb[2]) >= 2 + 960 + 2 and rgb[2][963] > 0 and max(rgb[2][5:962:4]) > 0, "the break's RGB"
n = names[2][1]
assert n >= 1 and s12(names[2], 2), "the breaks' names"
assert (tempo[2][3] | tempo[2][4] << 8) == 1660, "the tempo"
assert play[2][0] == 1 and live[2][2] == 1 and live[2][31] > 0, f"PLAY, the live scope ({live[2][31]} columns)"
print(f"editor_link: the Studio: STATUS, MIX MASTER, FX slot 2 {s12(fxs[2], 120)}, RESET refused, TRACK 2 written, the patterns, the break's RGB ({rgb[2][963]} slices), {n} breaks, 166 BPM, PLAY, {live[2][31]} live columns")
print("editor_link: the sound editor's commands (7-12) unanswered: gone with the drums; the screen kept")
P
