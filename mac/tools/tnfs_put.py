#!/usr/bin/env python3
"""Upload a file to a TNFS server, reliably: every write is preceded by a
seek, so retransmitting after a lost reply cannot corrupt the file.

usage: tnfs_put.py host local_file remote_path [port]
"""
import socket, struct, sys, time

class Tnfs:
    def __init__(self, host, port=16384):
        self.addr = (socket.gethostbyname(host), port)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.settimeout(1.0)
        self.conn = 0
        self.seq = 0

    def call(self, cmd, payload=b"", tries=10):
        self.seq = (self.seq + 1) & 0xFF
        pkt = struct.pack("<HBB", self.conn, self.seq, cmd) + payload
        for _ in range(tries):
            self.s.sendto(pkt, self.addr)
            deadline = time.time() + 1.0
            while time.time() < deadline:
                try:
                    data, _ = self.s.recvfrom(2048)
                except socket.timeout:
                    break
                conn, seq, rcmd, status = struct.unpack("<HBBB", data[:5])
                if seq == self.seq and rcmd == cmd:
                    if cmd == 0x00:
                        self.conn = conn
                    return status, data[5:]
        raise TimeoutError(f"no reply to command 0x{cmd:02X}")

def main():
    host, local, remote = sys.argv[1:4]
    port = int(sys.argv[4]) if len(sys.argv) > 4 else 16384
    data = open(local, "rb").read()
    t = Tnfs(host, port)
    st, _ = t.call(0x00, struct.pack("<BB", 2, 1) + b"/\0\0\0")
    assert st == 0, f"mount status {st}"
    O_WRONLY, O_CREAT, O_TRUNC = 0x0002, 0x0100, 0x0200
    st, r = t.call(0x29, struct.pack("<HH", O_WRONLY | O_CREAT | O_TRUNC, 0o644) + remote.encode() + b"\0")
    assert st == 0, f"open status {st}"
    fd = r[0]
    start = time.time()
    CH = 512
    for off in range(0, len(data), CH):
        chunk = data[off:off + CH]
        st, _ = t.call(0x25, struct.pack("<BBi", fd, 0, off))      # seek
        assert st == 0, f"seek status {st} at {off}"
        st, r = t.call(0x22, struct.pack("<BH", fd, len(chunk)) + chunk)
        assert st == 0 and struct.unpack("<H", r[:2])[0] == len(chunk), f"write status {st} at {off}"
        if off % (CH * 1024) == 0:
            done = off + len(chunk)
            rate = done / max(time.time() - start, 0.001)
            print(f"{done}/{len(data)} bytes, {rate/1024:.0f} KB/s", flush=True)
    t.call(0x23, bytes([fd]))
    t.call(0x01)
    print(f"uploaded {len(data)} bytes in {time.time() - start:.0f} s")

if __name__ == "__main__":
    main()
