#!/usr/bin/env python3
"""Download a file from a TNFS server, or list a directory.

usage: tnfs_get.py host remote_path local_file [port]
       tnfs_get.py host remote_dir/ --ls [port]
"""
import struct, sys, time
from tnfs_put import Tnfs

def main():
    host, remote, local = sys.argv[1:4]
    port = int(sys.argv[4]) if len(sys.argv) > 4 else 16384
    t = Tnfs(host, port)
    st, _ = t.call(0x00, struct.pack("<BB", 2, 1) + b"/\0\0\0")
    assert st == 0, f"mount status {st}"
    if local == "--ls":
        st, r = t.call(0x10, remote.encode() + b"\0")
        assert st == 0, f"opendir status {st}"
        h = r[0]
        while True:
            st, r = t.call(0x11, bytes([h]))
            if st != 0:
                break
            name = r.split(b"\0")[0].decode(errors="replace")
            if name not in (".", ".."):
                print(name)
        t.call(0x12, bytes([h]))
        t.call(0x01)
        return
    st, r = t.call(0x29, struct.pack("<HH", 0x0001, 0) + remote.encode() + b"\0")
    assert st == 0, f"open status {st}"
    fd = r[0]
    out = bytearray()
    start = time.time()
    CH = 512
    while True:
        st, _ = t.call(0x25, struct.pack("<BBi", fd, 0, len(out)))   # seek
        assert st == 0, f"seek status {st} at {len(out)}"
        st, r = t.call(0x21, struct.pack("<BH", fd, CH))
        if st == 0x21:        # EOF
            break
        assert st == 0, f"read status {st} at {len(out)}"
        n = struct.unpack("<H", r[:2])[0]
        out += r[2:2 + n]
        if n == 0:
            break
        if len(out) % (CH * 1024) == 0:
            print(f"{len(out)} bytes, {len(out)/1024/max(time.time()-start,0.001):.0f} KB/s", flush=True)
    t.call(0x23, bytes([fd]))
    t.call(0x01)
    open(local, "wb").write(out)
    print(f"downloaded {len(out)} bytes in {time.time() - start:.0f} s")

if __name__ == "__main__":
    main()
