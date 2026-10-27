"""Finds functions whose start the analyzer put one instruction late: right after a `blr` comes an
instruction that isn't a known start, and the next instruction is one (the analyzer reached the
function through a branch to its second instruction, while its real start is only in a pointer
table).

Usage: python find_late_starts.py <code.bin> <code base hex> <codegen.partition.json>
"""
import json
import struct
import sys

code = open(sys.argv[1], "rb").read()
base = int(sys.argv[2], 16)
starts = {int(k, 16) for k in json.load(open(sys.argv[3]))["assignments"]}
BLR = 0x4E800020
n = len(code) // 4
words = struct.unpack(">%dI" % n, code[: n * 4])
found = []
for i in range(1, n - 1):
    addr = base + 4 * i
    if words[i - 1] == BLR and addr not in starts and (addr + 4) in starts and words[i] != 0:
        found.append(addr)
print(len(starts), "known starts;", len(found), "late starts", file=sys.stderr)
for a in found:
    print(f"0x{a:08X} {words[(a - base) // 4]:08X}")
