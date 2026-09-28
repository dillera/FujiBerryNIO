#!/usr/bin/env python3
# Origin: fujinet-mac-da tools/install_da.py (commit d029216). Changed here:
# the default name/ID, and it refuses to overwrite a different DA.
"""Inject a Desk Accessory (DRVR) into a System file (MacBinary).

Usage: install_da.py <System.bin> <flatcode.flt> <output.bin>
                     [--name "FujiNet CONFIG"] [--id 21] [--drop OtherDA]...

Takes a Retro68 flat code resource (single entry point at offset 0),
prepends the classic 30-byte DRVR header (all five routine offsets
pointing at the code start), and adds it to the System file's resource
fork as DRVR <id> with name "\\0<name>" and attributes 0x20 (purgeable)
-- exactly the convention used by the System 6 built-in DAs.
"""

import struct
import sys

DRVR_FLAGS = 0x4400        # dNeedLock | dCtlEnable
DRVR_EMASK = 0x016A        # mouseDown, keyDown, autoKey, update, activate


def parse_macbinary(raw):
    namelen = raw[1]
    if not (1 <= namelen <= 63) or raw[0] != 0:
        raise SystemExit("not a MacBinary file")
    dlen = struct.unpack(">I", raw[83:87])[0]
    rlen = struct.unpack(">I", raw[87:91])[0]
    dstart = 128
    rstart = dstart + ((dlen + 127) // 128) * 128
    return raw[:128], raw[dstart:dstart + dlen], raw[rstart:rstart + rlen]


def crc16_xmodem(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


def build_macbinary(header, data, rsrc):
    h = bytearray(header)
    struct.pack_into(">I", h, 83, len(data))
    struct.pack_into(">I", h, 87, len(rsrc))
    struct.pack_into(">H", h, 124, crc16_xmodem(h[:124]))

    def pad(b):
        return b + b"\0" * ((128 - len(b) % 128) % 128)

    return bytes(h) + pad(data) + pad(rsrc)


def parse_fork(fork):
    data_off, map_off, data_len, map_len = struct.unpack(">IIII", fork[:16])
    m = fork[map_off:map_off + map_len]
    attrs = struct.unpack(">H", m[22:24])[0]
    type_off, name_off = struct.unpack(">HH", m[24:28])
    ntypes = struct.unpack(">H", m[type_off:type_off + 2])[0] + 1

    resources = []
    for i in range(ntypes):
        rtype, cnt, ref_off = struct.unpack(
            ">4sHH", m[type_off + 2 + i * 8:type_off + 10 + i * 8])
        for j in range(cnt + 1):
            e = m[type_off + ref_off + j * 12:type_off + ref_off + (j + 1) * 12]
            rid, noff = struct.unpack(">hH", e[:4])
            rattr = e[4]
            doff = struct.unpack(">I", b"\0" + e[5:8])[0]
            name = None
            if noff != 0xFFFF:
                nl = m[name_off + noff]
                name = m[name_off + noff + 1:name_off + noff + 1 + nl]
            dlen = struct.unpack(
                ">I", fork[data_off + doff:data_off + doff + 4])[0]
            rdata = fork[data_off + doff + 4:data_off + doff + 4 + dlen]
            resources.append([rtype, rid, rattr, name, rdata])
    return attrs, resources


def build_fork(map_attrs, resources):
    # data section
    data = bytearray()
    offsets = []
    for r in resources:
        offsets.append(len(data))
        data += struct.pack(">I", len(r[4])) + r[4]

    # group by type, preserving first-seen type order
    types = []
    by_type = {}
    for i, r in enumerate(resources):
        if r[0] not in by_type:
            by_type[r[0]] = []
            types.append(r[0])
        by_type[r[0]].append(i)

    names = bytearray()
    refs_by_type = {}
    for t in types:
        refs = bytearray()
        for i in by_type[t]:
            rtype, rid, rattr, name, rdata = resources[i]
            if name is None:
                noff = 0xFFFF
            else:
                noff = len(names)
                names += bytes([len(name)]) + name
            refs += struct.pack(">hH", rid, noff)
            refs += struct.pack(">I", (rattr << 24) | offsets[i])
            refs += b"\0\0\0\0"
        refs_by_type[t] = refs

    type_list = struct.pack(">H", len(types) - 1)
    ref_start = 2 + len(types) * 8
    refs_all = bytearray()
    for t in types:
        type_list_entry_off = ref_start + len(refs_all)
        type_list += struct.pack(
            ">4sHH", t, len(by_type[t]) - 1, type_list_entry_off)
        refs_all += refs_by_type[t]

    type_area = type_list + refs_all
    # map: 16 reserved + 4 next + 2 refnum + 2 attrs + 2 typeoff + 2 nameoff
    map_hdr_len = 28
    type_off = map_hdr_len - 2  # type list offset counted from map start
    # NB: type list offset field traditionally = 28 (right after header)
    type_off = 28
    name_off = type_off + len(type_area)
    rmap = (b"\0" * 16 + b"\0" * 4 + b"\0" * 2
            + struct.pack(">H", map_attrs)
            + struct.pack(">HH", type_off, name_off)
            + type_area + names)

    data_off = 256
    map_off = data_off + len(data)
    fork = (struct.pack(">IIII", data_off, map_off, len(data), len(rmap))
            + b"\0" * 240 + data + rmap)
    # the map begins with a copy of the fork header
    fork = bytearray(fork)
    fork[map_off:map_off + 16] = fork[:16]
    return bytes(fork)


def build_drvr(flat, name):
    hdr = struct.pack(">HHHH", DRVR_FLAGS, 0, DRVR_EMASK, 0)
    namebytes = bytes([len(name)]) + name.encode("macroman")
    hdrlen = 8 + 10 + len(namebytes)
    if hdrlen % 2:
        namebytes += b"\0"
        hdrlen += 1
    entry = hdrlen
    hdr += struct.pack(">HHHHH", entry, entry, entry, entry, entry)
    return hdr + namebytes + flat


def main():
    args = sys.argv[1:]
    name, rid = "FujiNet CONFIG", 21
    if "--name" in args:
        i = args.index("--name"); name = args[i + 1]; del args[i:i + 2]
    if "--id" in args:
        i = args.index("--id"); rid = int(args[i + 1]); del args[i:i + 2]
    drop = []
    while "--drop" in args:            # remove another DA by name first
        i = args.index("--drop"); drop.append(args[i + 1]); del args[i:i + 2]
    sysfile, flatfile, outfile = args

    raw = open(sysfile, "rb").read()
    header, data, fork = parse_macbinary(raw)
    map_attrs, resources = parse_fork(fork)

    resname = b"\0" + name.encode("macroman")
    dropnames = [b"\0" + n.encode("macroman") for n in drop]
    for r in resources:
        if r[0] == b"DRVR" and r[3] in dropnames:
            print(f"removed DRVR {r[1]} {r[3][1:].decode('macroman')!r}")
    resources = [r for r in resources
                 if not (r[0] == b"DRVR" and r[3] in dropnames)]
    for r in resources:
        if r[0] == b"DRVR" and r[1] == rid and r[3] != resname:
            raise SystemExit(f"DRVR {rid} is already {r[3]!r}; pick another --id")
    resources = [r for r in resources
                 if not (r[0] == b"DRVR" and (r[1] == rid or r[3] == resname))]
    flat = open(flatfile, "rb").read()
    resources.append([b"DRVR", rid, 0x20, resname, build_drvr(flat, name)])

    newfork = build_fork(map_attrs, resources)
    open(outfile, "wb").write(build_macbinary(header, data, newfork))
    print(f"installed DRVR {rid} \"{name}\" ({len(flat)} bytes code) "
          f"-> {outfile}")


if __name__ == "__main__":
    main()
