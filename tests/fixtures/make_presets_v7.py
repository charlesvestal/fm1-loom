#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tests/fixtures/presets_v7.bkp: a backup (fm1-base's backup format, as the simulator's sim_backup writes it) whose
only object is a presets object of PRESETS_FORMAT 7 (before the user kits): user preset U1 "oldpad", a knit sound.
tests/scenarios/kits.xy restores it: the preset loads, the kits are empty, and a kit saved keeps the preset."""
import struct, zlib, os

NUSER, NAME_LEN, NTP_SOUND = 16, 12, 39
OBJS = [1] + [2 + i for i in range(21)]          # app.h APP_BACKUP_OBJS: OBJ_PRESETS, the three slots' 7 objects
presets = bytearray()
for k in range(NUSER):
    name = b"oldpad" if k == 0 else b""
    par = bytes([0] + [64] * (NTP_SOUND - 1)) if k == 0 else bytes(NTP_SOUND)
    presets += name.ljust(NAME_LEN, b"\0") + bytes([0]) + par
obj1 = struct.pack("<I", 7) + bytes(presets)
head = struct.pack("<IHBB", 0x42384D4C, 1, len(OBJS), 0) + struct.pack("<I", 0)
img = bytearray(head + struct.pack("<I", zlib.crc32(head)))
for o in OBJS:
    data = obj1 if o == 1 else b""
    img += struct.pack("<HH", o, len(data)) + struct.pack("<I", zlib.crc32(data)) + data
img += struct.pack("<I", zlib.crc32(bytes(img)))
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "presets_v7.bkp"), "wb").write(img)
