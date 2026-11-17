#!/usr/bin/env python3
"""A2S_INFO humans check. Prints players/bots/humans and exits 3 if humans > 0 (2 on error).

Use it as its own step before any stop, restart, changelevel or plugin reload, and run the
action in a separate command only if this exits 0.

  python -I a2s.py <host> <port>
"""
import socket, struct, sys


def info(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(3)
    try:
        q = b"\xff\xff\xff\xffTSource Engine Query\x00"
        s.sendto(q, (host, port))
        d, _ = s.recvfrom(65535)
        if d[:5] == b"\xff\xff\xff\xffA":
            s.sendto(q + d[5:9], (host, port))
            d, _ = s.recvfrom(65535)
        if d[:5] != b"\xff\xff\xff\xffI":
            raise ValueError("unexpected reply")
        i = 6
        strs = []
        for _ in range(4):
            j = d.index(0, i)
            strs.append(d[i:j].decode("utf-8", "replace"))
            i = j + 1
        i += 2                                    # app id
        players, maxp, bots = d[i], d[i + 1], d[i + 2]
        return strs[1], players, maxp, bots
    finally:
        s.close()


if __name__ == "__main__":
    try:
        m, p, mx, b = info(sys.argv[1], int(sys.argv[2]))
    except Exception as e:
        print("a2s error:", type(e).__name__, e)
        sys.exit(2)
    humans = max(0, p - b)
    print("map=%s players=%d bots=%d max=%d humans=%d" % (m, p, b, mx, humans))
    sys.exit(3 if humans > 0 else 0)
