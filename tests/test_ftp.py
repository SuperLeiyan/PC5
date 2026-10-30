#!/usr/bin/env python3
"""PC5's FTP server (title/pc5_ftp.c) as a real client sees it: Python's ftplib, as FileZilla
would, against ftp-serve (tests/pc5_ftp_host.c). Usage: test_ftp.py <ftp-serve> <work dir>."""
import ftplib
import io
import os
import shutil
import socket
import subprocess
import sys


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    serve, work = sys.argv[1], sys.argv[2]
    root = os.path.join(work, "ftp-root")
    outside = os.path.join(work, "ftp-outside.txt")
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    with open(outside, "w") as f:
        f.write("not for clients")
    port = free_port()
    server = subprocess.Popen([serve, root, str(port)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert server.stdout.readline().strip() == "ready"
    checks = 0
    try:
        ftp = ftplib.FTP()
        welcome = ftp.connect("127.0.0.1", port, timeout=10)
        assert welcome.startswith("220") and "INSTALL" in welcome, welcome
        ftp.login("anyone", "anything")
        assert ftp.pwd() == "/"
        checks += 2

        # A game's folder, as FileZilla sends one: folders made, then each file.
        ftp.mkd("Neon Drift")
        ftp.cwd("Neon Drift")
        ftp.mkd("data")
        big = os.urandom(3 * 1024 * 1024 + 17)
        ftp.storbinary("STOR NeonDrift.exe", io.BytesIO(b"MZ" + b"\0" * 100))
        ftp.storbinary("STOR data/level1.pak", io.BytesIO(big), blocksize=65536)
        with open(os.path.join(root, "Neon Drift", "data", "level1.pak"), "rb") as f:
            assert f.read() == big
        assert ftp.size("data/level1.pak") == len(big)
        assert ftp.pwd() == "/Neon Drift"
        checks += 4

        # Listed as ls does it, which FileZilla reads.
        lines = []
        ftp.retrlines("LIST", lines.append)
        names = sorted(line.split(None, 8)[8] for line in lines)
        assert names == ["NeonDrift.exe", "data"], lines
        assert any(line.startswith("d") and line.endswith(" data") for line in lines), lines
        assert sorted(ftp.nlst()) == ["NeonDrift.exe", "data"]
        ftp.cwd("/")
        lines = []
        ftp.retrlines("LIST -la", lines.append)
        assert len(lines) == 1 and lines[0].endswith("Neon Drift"), lines
        checks += 4

        # Downloaded back, from the start and resumed part way.
        got = io.BytesIO()
        ftp.retrbinary("RETR /Neon Drift/data/level1.pak", got.write)
        assert got.getvalue() == big
        got = io.BytesIO()
        ftp.retrbinary("RETR /Neon Drift/data/level1.pak", got.write, rest=1000)
        assert got.getvalue() == big[1000:]
        checks += 2

        # A cut-short upload resumed (REST then STOR, as FileZilla resumes).
        ftp.storbinary("STOR /Neon Drift/part.bin", io.BytesIO(big[:5000]))
        ftp.storbinary("STOR /Neon Drift/part.bin", io.BytesIO(big[5000:]), rest=5000)
        with open(os.path.join(root, "Neon Drift", "part.bin"), "rb") as f:
            assert f.read() == big
        checks += 1

        # Nothing outside the INSTALL folder: .. stops at its top.
        ftp.cwd("/../../..")
        assert ftp.pwd() == "/"
        for bad in ("RETR ../ftp-outside.txt", "RETR /../ftp-outside.txt", "DELE ../ftp-outside.txt"):
            try:
                if bad.startswith("RETR"):
                    ftp.retrbinary(bad, lambda b: None)
                else:
                    ftp.voidcmd(bad)
                raise AssertionError("allowed: " + bad)
            except ftplib.error_perm:
                pass
        assert os.path.exists(outside)
        checks += 2

        # Renamed, deleted; the folder itself can't be.
        ftp.rename("/Neon Drift/part.bin", "/Neon Drift/whole.bin")
        assert os.path.exists(os.path.join(root, "Neon Drift", "whole.bin"))
        ftp.delete("/Neon Drift/whole.bin")
        assert not os.path.exists(os.path.join(root, "Neon Drift", "whole.bin"))
        for bad in ("RMD /", "DELE /", "RNFR /"):
            try:
                ftp.voidcmd(bad)
                raise AssertionError("allowed: " + bad)
            except ftplib.error_perm:
                pass
        assert os.path.isdir(root)
        checks += 3

        # Extended passive, as IPv6-ready clients ask.
        resp = ftp.sendcmd("EPSV")
        assert resp.startswith("229") and "|||" in resp, resp
        resp = ftp.sendcmd("PORT 127,0,0,1,4,1")
        raise AssertionError("PORT accepted: " + resp)
    except ftplib.error_perm as e:
        assert str(e).startswith("502"), e
        checks += 2
    finally:
        try:
            ftp.quit()
        except Exception:
            pass

    # Several at once, as FileZilla transfers two or three files side by side.
    clients = []
    for i in range(3):
        c = ftplib.FTP()
        c.connect("127.0.0.1", port, timeout=10)
        c.login()
        clients.append(c)
    for i, c in enumerate(clients):
        c.storbinary("STOR /f%d.bin" % i, io.BytesIO(bytes([i]) * 100000))
    for i in range(3):
        with open(os.path.join(root, "f%d.bin" % i), "rb") as f:
            assert f.read() == bytes([i]) * 100000
    checks += 1

    # Stopped with a client still connected: it is let go.
    server.stdin.close()
    out = server.stdout.read()
    server.wait(timeout=10)
    assert "stopped running=0 sessions=0" in out, out
    checks += 1
    for c in clients:
        try:
            c.voidcmd("NOOP")
            raise AssertionError("still connected")
        except (EOFError, OSError, ftplib.Error):
            pass
    print("ftp passed: %d checks (folders, uploads, listings, downloads, resuming, the root kept, renames and "
          "deletes, passive modes, several clients, the stop)" % checks)


if __name__ == "__main__":
    main()
