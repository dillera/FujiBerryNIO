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
    # Size, then 8 read handles: each window seeks all of them, then reads
    # all of them, in flight together (TNFS takes each datagram on its own).
    st, r = t.call(0x24, remote.encode() + b"\0")
    assert st == 0, f"stat status {st}"
    size = struct.unpack("<I", r[6:10])[0]
    fds = []
    for _ in range(8):
        st, r = t.call(0x29, struct.pack("<HH", 0x0001, 0) + remote.encode() + b"\0")
        assert st == 0, f"open status {st}"
        fds.append(r[0])
    out = bytearray(size)
    start = time.time()
    CH = 512
    blocks = (size + CH - 1) // CH
    b = 0
    while b < blocks:
        lanes = fds[:min(len(fds), blocks - b)]
        for attempt in range(5):
            try:
                got = batch(t, [(0x25, struct.pack("<BBi", fd, 0, (b + i) * CH)) for i, fd in enumerate(lanes)])
                got = batch(t, [(0x21, struct.pack("<BH", fd, CH)) for fd in lanes], resend=False)
                for i, d in enumerate(got):
                    n = struct.unpack("<H", d[:2])[0]
                    out[(b + i) * CH:(b + i) * CH + n] = d[2:2 + n]
                break
            except TimeoutError:
                continue
        else:
            raise SystemExit(f"failed at block {b}")
        b += len(lanes)
        if b % 2048 < len(lanes):
            print(f"{b * CH} bytes, {b * CH / 1024 / max(time.time() - start, 0.001):.0f} KB/s", flush=True)
    for fd in fds:
        t.call(0x23, bytes([fd]))
    t.call(0x01)
    open(local, "wb").write(out)
    print(f"downloaded {size} bytes in {time.time() - start:.0f} s")


def batch(t, requests, resend=True):
    """Send requests together; return their replies' payloads in order.
    Seeks can be sent again; reads cannot (a handle would move twice)."""
    pend, out = {}, [None] * len(requests)
    for i, (cmd, payload) in enumerate(requests):
        t.seq = (t.seq + 1) & 0xFF
        pend[t.seq] = (i, struct.pack("<HBB", t.conn, t.seq, cmd) + payload)
    for _ in range(3 if resend else 1):
        for _, pkt in pend.values():
            t.s.sendto(pkt, t.addr)
        deadline = time.time() + 1.0
        while pend and time.time() < deadline:
            try:
                data, _ = t.s.recvfrom(2048)
            except OSError:
                break
            seq = data[2]
            if seq in pend and data[4] == 0:
                out[pend.pop(seq)[0]] = data[5:]
            elif seq in pend:
                raise SystemExit(f"TNFS error {data[4]:#x}")
        if not pend:
            return out
    raise TimeoutError("replies missing")


if __name__ == "__main__":
    main()
