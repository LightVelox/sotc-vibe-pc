import ctypes
from ctypes import wintypes as wt
import time


_exercise_windows = {}


def capture_window(pid, path):
    from PIL import Image

    user = ctypes.WinDLL("user32", use_last_error=True)
    gdi = ctypes.WinDLL("gdi32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wt.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
    user.IsWindowVisible.argtypes = [wt.HWND]
    user.IsWindow.argtypes = [wt.HWND]
    user.IsIconic.argtypes = [wt.HWND]
    user.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
    user.GetDC.argtypes = [wt.HWND]
    user.GetDC.restype = wt.HDC
    user.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
    user.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
    gdi.CreateCompatibleDC.argtypes = [wt.HDC]
    gdi.CreateCompatibleDC.restype = wt.HDC
    gdi.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]
    gdi.CreateCompatibleBitmap.restype = wt.HBITMAP
    gdi.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
    gdi.SelectObject.restype = wt.HGDIOBJ
    gdi.DeleteObject.argtypes = [wt.HGDIOBJ]
    gdi.DeleteDC.argtypes = [wt.HDC]
    windows = []

    @callback_type
    def visit(window, unused):
        owner = wt.DWORD()
        user.GetWindowThreadProcessId(window, ctypes.byref(owner))
        rect = wt.RECT()
        if owner.value == pid and user.IsWindowVisible(window) and user.GetClientRect(window, ctypes.byref(rect)):
            windows.append((rect.right * rect.bottom, window, rect.right, rect.bottom))
        return True

    user.EnumWindows(visit, 0)
    if not windows:
        raise RuntimeError("No visible window for capture process")
    unused, window, width, height = max(windows)
    dc = user.GetDC(window)
    memory = gdi.CreateCompatibleDC(dc)
    bitmap = gdi.CreateCompatibleBitmap(dc, width, height)
    previous = gdi.SelectObject(memory, bitmap)

    class Header(ctypes.Structure):
        _fields_ = [("size", wt.DWORD), ("width", wt.LONG), ("height", wt.LONG), ("planes", wt.WORD),
                    ("bits", wt.WORD), ("compression", wt.DWORD), ("image_size", wt.DWORD),
                    ("xppm", wt.LONG), ("yppm", wt.LONG), ("used", wt.DWORD), ("important", wt.DWORD)]

    gdi.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.POINTER(Header), wt.UINT]
    try:
        if not user.PrintWindow(window, memory, 3):
            raise ctypes.WinError(ctypes.get_last_error())
        gdi.SelectObject(memory, previous)
        header = Header(ctypes.sizeof(Header), width, -height, 1, 32)
        data = ctypes.create_string_buffer(width * height * 4)
        if not gdi.GetDIBits(memory, bitmap, 0, height, data, ctypes.byref(header), 0):
            raise ctypes.WinError(ctypes.get_last_error())
        Image.frombuffer("RGB", (width, height), data.raw, "raw", "BGRX", 0, 1).save(path)
    finally:
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(memory)
        user.ReleaseDC(window, dc)


def exercise_window(pid, action):
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wt.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
    user.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
    user.IsWindowVisible.argtypes = [wt.HWND]
    user.IsWindow.argtypes = [wt.HWND]
    user.IsIconic.argtypes = [wt.HWND]
    user.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
    user.ShowWindow.argtypes = [wt.HWND, ctypes.c_int]
    user.SetWindowPos.argtypes = [wt.HWND, wt.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wt.UINT]
    windows = []

    @callback_type
    def visit(window, unused):
        owner = wt.DWORD()
        rect = wt.RECT()
        user.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(window) and user.GetClientRect(window, ctypes.byref(rect)):
            windows.append((rect.right * rect.bottom, window))
        return True

    user.EnumWindows(visit, 0)
    if not windows:
        raise RuntimeError("No visible test window")
    window = _exercise_windows.get(pid)
    if not window or not user.IsWindow(window):
        window = max(windows)[1]
        _exercise_windows[pid] = window
    if action in ("save", "load", "fullscreen"):
        key, scan = {"save": (0x74, 0x3F), "load": (0x78, 0x43), "fullscreen": (0x7A, 0x57)}[action]
        user.PostMessageW(window, 0x100, key, 1 | (scan << 16))
        time.sleep(0.08)
        user.PostMessageW(window, 0x101, key, 1 | (scan << 16) | (3 << 30))
    elif action == "resize":
        user.ShowWindow(window, 9)
        if not user.SetWindowPos(window, None, 0, 0, 1280, 720, 6):
            raise ctypes.WinError(ctypes.get_last_error())
    elif action == "minimize":
        user.ShowWindow(window, 6)
    elif action == "restore":
        user.ShowWindow(window, 9)
    rect = wt.RECT()
    user.GetClientRect(window, ctypes.byref(rect))
    return {"window": window, "minimized": bool(user.IsIconic(window)), "width": rect.right, "height": rect.bottom}
