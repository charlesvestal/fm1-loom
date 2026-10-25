#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""tests/fixtures/presets_v10.bkp: a backup (fm1-base's backup format, as the simulator's sim_backup writes it) whose
only object is a presets object of PRESETS_FORMAT 10, as LOOM-8 saved it before user kits went from four to eight: the
user presets (none), then four user kits: K1 "first" (key 1 on sophie, model 20: a model of format 10's, kept), K4
"fourth" (key 1 on the x kick, model 11). tests/scenarios/kits8.xy restores it."""
import struct, zlib, os

NUSER, NAME_LEN, NTP_SOUND, NKITS = 16, 12, 39, 4
DP_N, NKEY, DPX_N = 20, 11, 4
KIT_KEYS = NKEY * DP_N + NKEY * DPX_N                 # par[TP_DRUM0..TP_DRUMX0 + 44): the keys, their knobs 5..8
OBJS = [1] + [2 + i for i in range(21)]              # app.h APP_BACKUP_OBJS: OBJ_PRESETS, the three slots' 7 objects

presets = bytes(NUSER * (NAME_LEN + 2 + NTP_SOUND))  # (user_preset_t: no presets, their names empty)
kits = bytearray()
for k in range(NKITS):
    img = bytearray(NAME_LEN + NTP_SOUND + KIT_KEYS)
    if k in (0, 3):
        img[:NAME_LEN] = (b"first" if k == 0 else b"fourth").ljust(NAME_LEN, b"\0")
        img[NAME_LEN:NAME_LEN + NTP_SOUND] = bytes([64] * NTP_SOUND)
        img[NAME_LEN] = 8                                # the kit engine (EG_DRUMLINE)
        keys = NAME_LEN + NTP_SOUND
        img[keys + 0] = 20 if k == 0 else 11            # key 1's model
        img[keys + 1] = 50
        img[keys + 7] = 76                              # (0 dB)
    kits += img
obj1 = struct.pack("<I", 10) + presets + bytes(kits)
head = struct.pack("<IHBB", 0x42384D4C, 1, len(OBJS), 0) + struct.pack("<I", 0)
img = bytearray(head + struct.pack("<I", zlib.crc32(head)))
for o in OBJS:
    data = obj1 if o == 1 else b""
    img += struct.pack("<HH", o, len(data)) + struct.pack("<I", zlib.crc32(data)) + data
img += struct.pack("<I", zlib.crc32(bytes(img)))
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "presets_v10.bkp"), "wb").write(img)
