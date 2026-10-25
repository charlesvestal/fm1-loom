#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""tests/fixtures/presets_v8.bkp: a backup (fm1-base's backup format, as the simulator's sim_backup writes it) whose
only object is a presets object of PRESETS_FORMAT 8 as breaks-out's development builds saved it (the user presets
without osc 2's fine, then the four user kits): U1 a knit sound (osc 2 at 75, its level 90), K1 "oldkit" (keys 1
and 2 on models 11 and 25, two of those the development builds had and LOOM-8 no longer does: project.c keys_fix
makes them a kick and a snare). tests/scenarios/osc2fine.xy restores it."""
import struct, zlib, os

NUSER, NAME_LEN, NTP_SOUND, NKITS = 16, 12, 39, 4
TP_X1, TP_X2, DP_N, NKEY, DPX_N = 34, 35, 20, 11, 4
KIT_KEYS = NKEY * DP_N + NKEY * DPX_N                 # par[TP_DRUM0..TP_DRUMX0 + 44): the keys, their knobs 5..8
OBJS = [1] + [2 + i for i in range(21)]              # app.h APP_BACKUP_OBJS: OBJ_PRESETS, the three slots' 7 objects

presets = bytearray()
for k in range(NUSER):
    par = bytearray(NTP_SOUND)
    if k == 0:
        par[:] = bytes([64] * NTP_SOUND)
        par[0], par[TP_X1], par[TP_X2] = 0, 75, 90
    presets += (b"oldpad" if k == 0 else b"").ljust(NAME_LEN, b"\0") + bytes([0]) + bytes(par)
kits = bytearray()
for k in range(NKITS):
    img = bytearray(NAME_LEN + NTP_SOUND + KIT_KEYS)
    if k == 0:
        img[:NAME_LEN] = b"oldkit".ljust(NAME_LEN, b"\0")
        img[NAME_LEN:NAME_LEN + NTP_SOUND] = bytes([64] * NTP_SOUND)
        img[NAME_LEN] = 8                                # the kit engine (EG_DRUMLINE)
        keys = NAME_LEN + NTP_SOUND
        img[keys + 0 * DP_N + 0] = 11                   # key 1: removed model 11 (a kick)
        img[keys + 0 * DP_N + 1] = 31
        img[keys + 0 * DP_N + 7] = 76                   # (0 dB)
        img[keys + 1 * DP_N + 0] = 25                   # key 2: removed model 25 (a snare)
        img[keys + NKEY * DP_N + 0] = 99                # key 1's knob 5
    kits += img
obj1 = struct.pack("<I", 8) + bytes(presets) + bytes(kits)
head = struct.pack("<IHBB", 0x42384D4C, 1, len(OBJS), 0) + struct.pack("<I", 0)
img = bytearray(head + struct.pack("<I", zlib.crc32(head)))
for o in OBJS:
    data = obj1 if o == 1 else b""
    img += struct.pack("<HH", o, len(data)) + struct.pack("<I", zlib.crc32(data)) + data
img += struct.pack("<I", zlib.crc32(bytes(img)))
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "presets_v8.bkp"), "wb").write(img)
