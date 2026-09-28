#!/usr/bin/env python3
"""Play the Pico's role on the Mac floppy-port bus against fujinet-nio.

Talks the one-character drive-side protocol (see
fujinet-nio include/fujinet/io/transport/mac_floppy_framer.h) over TCP and
checks: unit count, HD20 status block, block reads, and a FujiBus clock
request through the mailbox blocks past the end of the volume.

usage: pico_sim.py [host:port]
"""
import socket
import struct
import sys
import time


def fujibus_checksum(data):
    chk = 0
    for b in data:
        chk += b
        chk = ((chk >> 8) + (chk & 0xFF)) & 0xFFFF
    return chk & 0xFF


def build_fuji_packet_decoded(device, command, payload=b""):
    """A raw (un-SLIPped) FujiBus packet with no parameters."""
    out = bytearray([device, command, 0, 0, 0, 0]) + payload
    out[2:4] = len(out).to_bytes(2, "little")
    out[4] = fujibus_checksum(out)
    return bytes(out)


def parse_fuji_response(raw):
    """(status, payload) of a FujiBus response: one u8 parameter, then data."""
    assert int.from_bytes(raw[2:4], "little") == len(raw), "length mismatch"
    tmp = bytearray(raw)
    tmp[4] = 0
    assert fujibus_checksum(tmp) == raw[4], "checksum mismatch"
    assert raw[5] & 0x07 == 1, f"unexpected descriptor 0x{raw[5]:02X}"
    return raw[6], raw[7:]


MAILBOX_BLOCKS = 16


class Bus:
    def __init__(self, addr):
        host, port = addr.split(":")
        self.s = socket.create_connection((host, int(port)), timeout=5)
        self.units = None
        self.floppy = None
        self.floppy_writable = None

    def recv_exact(self, n):
        out = bytearray()
        while len(out) < n:
            chunk = self.s.recv(n - len(out))
            if not chunk:
                raise EOFError("fujinet-nio closed the connection")
            out += chunk
        return bytes(out)

    def drain(self):
        """Handle unsolicited 'h' n announcements before a request."""
        self.s.setblocking(False)
        try:
            while True:
                b = self.s.recv(1)
                if not b:
                    break
                if b == b"h":
                    self.s.setblocking(True)
                    self.units = self.recv_exact(1)[0]
                    self.s.setblocking(False)
                elif b in (b"s", b"d"):
                    self.s.setblocking(True)
                    self.floppy = (1 if b == b"s" else 2, self.recv_exact(1)[0] & 0x7F)
                    self.s.setblocking(False)
                elif b in (b"u", b"l"):
                    self.floppy_writable = b == b"u"
                elif b == b"r":
                    self.floppy = None
        except BlockingIOError:
            pass
        finally:
            self.s.setblocking(True)
            self.s.settimeout(5)

    def query_units(self):
        self.drain()
        self.s.sendall(b"?")
        assert self.recv_exact(1) == b"h"
        self.units = self.recv_exact(1)[0]
        return self.units

    def select(self, unit):
        self.s.sendall(bytes([ord("A") + unit]))

    def status(self):
        self.drain()
        self.s.sendall(b"T")
        return self.recv_exact(336)

    def read(self, block):
        self.drain()
        self.s.sendall(b"R" + block.to_bytes(3, "big"))
        return self.recv_exact(512)

    def floppy_cmd(self, c):
        self.drain()
        self.s.sendall(c)
        return self.recv_exact(1)

    def track(self, cyl, side):
        self.drain()
        self.s.sendall(b"#" + bytes([cyl, side]))
        nbits = int.from_bytes(self.recv_exact(4), "big")
        return nbits, self.recv_exact(nbits // 8)

    def put_track(self, cyl, side, nbits, data):
        self.drain()
        self.s.sendall(b"P" + bytes([cyl, side]) + nbits.to_bytes(4, "big") + data)
        r = self.recv_exact(2)
        assert r[:1] == b"p", r
        return r[1]

    def write(self, block, data):
        assert len(data) == 512
        self.drain()
        self.s.sendall(b"W" + block.to_bytes(3, "big") + data)
        return self.recv_exact(1)


def fujibus_call(bus, base, seq, packet, timeout=2.0):
    """One request/response through the mailbox at block `base`."""
    body = b"FNIO" + bytes([1, 0]) + struct.pack(">HI", seq, len(packet)) + bytes(4) + packet
    nblocks = (len(body) + 511) // 512
    body += bytes(nblocks * 512 - len(body))
    for i in range(nblocks):
        assert bus.write(base + i, body[i * 512:(i + 1) * 512]) == b"w"
    deadline = time.time() + timeout
    while time.time() < deadline:
        blk = bus.read(base)
        magic, ver, state, rseq, length = struct.unpack(">4sBBHI", blk[:12])
        assert magic == b"FNIO", blk[:16]
        if state == 1 and rseq == seq:
            data = bytearray(blk[16:])
            i = 1
            while len(data) < length:
                data += bus.read(base + i)
                i += 1
            return bytes(data[:length])
        time.sleep(0.01)
    raise TimeoutError(f"no response for seq {seq}")


def main():
    bus = Bus(sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:65510")
    n = bus.query_units()
    print(f"DCD units: {n}")
    assert n >= 1, "mount an image in unit 0"
    bus.select(0)
    st = bus.status()
    blocks = int.from_bytes(st[11 - 6:14 - 6], "big")
    print(f"unit 0: characteristics 0x{st[10 - 6]:02X}, {blocks} blocks "
          f"(image {blocks - MAILBOX_BLOCKS} + mailbox {MAILBOX_BLOCKS}), "
          f"location {st[327 - 6:336 - 6].decode()!r}")
    b0 = bus.read(0)
    mdb = bus.read(2)
    sig = mdb[:2]
    name = mdb[0x25:0x25 + 1 + mdb[0x24]][:mdb[0x24]].decode("mac_roman")
    print(f"block 2 signature {sig!r} ({'HFS' if sig == b'BD' else '?'}), volume {name!r}; "
          f"boot blocks {'present' if b0[:2] == b'LK' else 'absent'}")

    base = blocks - MAILBOX_BLOCKS
    idle = bus.read(base)
    print(f"mailbox at block {base}: {idle[:4]!r} v{idle[4]} state {idle[5]}")

    # FujiBus: clock device 0x45, GetTime (0x01), payload = version 1.
    req = build_fuji_packet_decoded(0x45, 0x01, bytes([1]))
    t0 = time.time()
    raw = fujibus_call(bus, base, 1, req)
    dt = (time.time() - t0) * 1000
    status, payload = parse_fuji_response(raw)
    unix = int.from_bytes(payload[4:12], "little")
    print(f"FujiBus clock GetTime over the mailbox: status {status}, "
          f"time {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(unix))} ({dt:.1f} ms)")
    assert status == 0 and abs(unix - time.time()) < 60

    # Floppy: mount an 800K image in slot 5 through the mailbox (FujiBus
    # DiskService Mount), then read a GCR track, write it back, eject.
    uri = b"host:/mac/Floppy800.dsk"
    payload = (bytes([1, 5, 0, 4]) + (512).to_bytes(2, "little") +
               len(uri).to_bytes(2, "little") + uri)
    status, _ = parse_fuji_response(fujibus_call(bus, base, 2, build_fuji_packet_decoded(0xFC, 0x01, payload)))
    assert status == 0, f"mount status {status}"
    time.sleep(0.1)
    bus.drain()
    print(f"floppy mounted over FujiBus: announced {bus.floppy}, writable {bus.floppy_writable}")
    assert bus.floppy == (2, 0) and bus.floppy_writable
    nbits, trk = bus.track(0, 0)
    marks = sum(1 for i in range(len(trk) - 2) if trk[i:i + 3] == b"\xd5\xaa\x96")
    print(f"cylinder 0 side 0: {nbits} bits, {marks} address marks")
    assert nbits == 74432 and marks == 12
    assert bus.put_track(0, 0, nbits, trk) == 0, "an unchanged track must write nothing"
    assert bus.floppy_cmd(b"2") == b"M" and bus.floppy_cmd(b"1") == bytes([0x81])
    assert bus.floppy_cmd(b"7") == b"E"
    print("floppy: track round trip, step and eject OK")
    print("OK")


if __name__ == "__main__":
    main()
