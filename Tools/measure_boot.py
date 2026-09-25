import argparse, ctypes, ctypes.wintypes as wt, os, socket, struct, subprocess, sys, threading, time
from PIL import ImageGrab

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ISO = os.path.join(REPO, "Game", "SHADOW_COLOSSUS (PAL).iso")
PCSX2 = os.path.join(REPO, "Tools", "pcsx2-oracle", "pcsx2-qt.exe")
SOTC = os.path.join(REPO, "build", "port", "bin", "sotc.exe")
COUNTERS = [0x1DC7AC, 0x1DC9D8]

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.SetProcessDPIAware()
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def windows_of(pid):
    found = []

    def cb(hwnd, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            r = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(r))
            found.append((r.right * r.bottom, hwnd))
        return True

    user32.EnumWindows(EnumWindowsProc(cb), 0)
    found.sort(reverse=True)
    return [h for _, h in found]


def client_bbox(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return (pt.x, pt.y, pt.x + r.right, pt.y + r.bottom)


class Pine:
    def __init__(self, port=28011):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)

    def read32(self, addr):
        payload = struct.pack("<BI", 2, addr)
        self.sock.sendall(struct.pack("<I", len(payload) + 4) + payload)
        hdr = self._recv(4)
        body = self._recv(struct.unpack("<I", hdr)[0] - 4)
        if body[0] != 0:
            raise RuntimeError("pine fail")
        return struct.unpack_from("<I", body, 1)[0]

    def _recv(self, n):
        b = b""
        while len(b) < n:
            c = self.sock.recv(n - len(b))
            if not c:
                raise ConnectionError
            b += c
        return b


VK = {"cross": (0x4B, 0x25, False), "circle": (0x4C, 0x26, False), "triangle": (0x49, 0x17, False),
      "square": (0x4A, 0x24, False), "start": (0x0D, 0x1C, False), "up": (0x26, 0x48, True),
      "down": (0x28, 0x50, True), "left": (0x25, 0x4B, True), "right": (0x27, 0x4D, True)}


def all_windows(hwnd):
    out = [hwnd]
    def cb(h, _):
        out.append(h)
        return True
    user32.EnumChildWindows(hwnd, EnumWindowsProc(cb), 0)
    return out


def press(hwnd, name, hold):
    vk, scan, ext = VK[name]
    lp = 1 | (scan << 16) | ((1 << 24) if ext else 0)
    targets = all_windows(hwnd)
    for h in targets:
        user32.PostMessageW(h, 0x100, vk, lp)
    time.sleep(hold)
    for h in targets:
        user32.PostMessageW(h, 0x101, vk, lp | (3 << 30))


gdi32 = ctypes.WinDLL("gdi32")


def grab(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right, r.bottom
    hdc = user32.GetDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(hwnd, mdc, 3)
    class BIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long), ("biPlanes", wt.WORD), ("biBitCount", wt.WORD),
                    ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long), ("biYPelsPerMeter", ctypes.c_long),
                    ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]
    bih = BIH()
    bih.biSize = ctypes.sizeof(BIH)
    bih.biWidth = w
    bih.biHeight = -h
    bih.biPlanes = 1
    bih.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bih), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    from PIL import Image
    return Image.frombuffer("RGB", (w, h), buf.raw, "raw", "BGRX", 0, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["native", "pcsx2"])
    ap.add_argument("out")
    ap.add_argument("--duration", type=float, default=120)
    ap.add_argument("--fps", type=float, default=10)
    ap.add_argument("--script", default="290:cross,375:cross,770:down,800:cross")
    ap.add_argument("--counter-offset", type=int, default=35)
    ap.add_argument("--no-capture", action="store_true")
    args = ap.parse_args()
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    frames_dir = os.path.join(args.out, "frames")
    os.makedirs(frames_dir, exist_ok=True)
    t0 = time.perf_counter()
    log = open(os.path.join(args.out, "events.txt"), "w")

    def ev(*parts):
        log.write("%.4f %s\n" % (time.perf_counter() - t0, " ".join(str(p) for p in parts)))
        log.flush()

    if args.mode == "native":
        env = dict(os.environ)
        env["PS2X_PAD_SCRIPT"] = ",".join("%sv:%s" % tuple(s.split(":")) for s in args.script.split(","))
        env["SOTC_TIMELINE"] = os.path.join(args.out, "timeline.txt").replace("\\", "/")
        env["SOTC_TIMELINE_WATCH"] = ",".join("%x" % c for c in COUNTERS)
        proc = subprocess.Popen([SOTC], cwd=REPO, env=env, stdout=open(os.path.join(args.out, "stdout.txt"), "w"), stderr=subprocess.STDOUT)
    else:
        proc = subprocess.Popen([PCSX2, "-batch", "-fastboot", "--", ISO], cwd=os.path.dirname(PCSX2))
    ev("launched", proc.pid)
    ev("perf_counter_origin", "%.6f" % t0)

    stop = threading.Event()
    hwnd_box = [None]

    def find_window():
        while not stop.is_set() and hwnd_box[0] is None:
            ws = windows_of(proc.pid)
            if ws:
                hwnd_box[0] = ws[0]
                ev("window", ws[0])
                return
            time.sleep(0.1)

    def capture():
        n = 0
        period = 1.0 / args.fps
        next_t = time.perf_counter()
        while not stop.is_set():
            hwnd = hwnd_box[0]
            if hwnd is not None:
                try:
                    img = grab(hwnd)
                    t = time.perf_counter() - t0
                    img = img.resize((192, 144))
                    img.save(os.path.join(frames_dir, "%05d_%09.3f.png" % (n, t)))
                    n += 1
                except Exception as e:
                    ev("capture_error", e)
            next_t += period
            time.sleep(max(0, next_t - time.perf_counter()))

    threading.Thread(target=find_window, daemon=True).start()
    if not args.no_capture:
        threading.Thread(target=capture, daemon=True).start()

    if args.mode == "pcsx2":
        steps = [(int(s.split(":")[0]) - args.counter_offset, s.split(":")[1]) for s in args.script.split(",")]
        pine = None
        last = None
        last_busy = [None]
        while time.perf_counter() - t0 < args.duration and proc.poll() is None:
            if pine is None:
                try:
                    pine = Pine()
                    ev("pine_connected")
                except OSError:
                    time.sleep(0.2)
                    continue
            try:
                vals = tuple(pine.read32(a) for a in COUNTERS)
            except Exception as e:
                ev("pine_error", e)
                pine = None
                time.sleep(0.2)
                continue
            try:
                busy = pine.read32(0x1309B4)
                if busy != last_busy[0]:
                    if busy == 1:
                        ev("cdread", "%d" % pine.read32(0x130A80), "%d" % pine.read32(0x130A84), "%d" % vals[1])
                    else:
                        ev("cddone", busy, "%d" % vals[1])
                    last_busy[0] = busy
            except Exception:
                pass
            if vals != last:
                ev("counter", *("%d" % v for v in vals))
                last = vals
                while steps and vals[1] < 0x80000000 and vals[1] >= steps[0][0] and hwnd_box[0] is not None:
                    c, name = steps.pop(0)
                    ev("press", name, "at_counter", vals[1])
                    threading.Thread(target=press, args=(hwnd_box[0], name, 0.2), daemon=True).start()
            time.sleep(0.0005)
    else:
        while time.perf_counter() - t0 < args.duration and proc.poll() is None:
            time.sleep(0.5)
    stop.set()
    ev("end")
    if proc.poll() is None:
        proc.kill()


main()
