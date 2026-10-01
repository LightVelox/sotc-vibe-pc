import ctypes
from ctypes import wintypes as wt
import time


class Resources:
    def __init__(self, process, path):
        self.handle = wt.HANDLE(int(process._handle))
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        pointer = ctypes.POINTER(wt.FILETIME)
        self.api.GetSystemTimes.argtypes = [pointer, pointer, pointer]
        self.api.GetProcessTimes.argtypes = [wt.HANDLE, pointer, pointer, pointer, pointer]
        self.api.QueryProcessCycleTime.argtypes = [wt.HANDLE, ctypes.POINTER(ctypes.c_ulonglong)]
        self.file = path.open("w")
        self.file.write("absolute_seconds,system_idle,system_kernel,system_user,process_kernel,process_user,process_cycles\n")
        self.next = 0.0

    def sample(self):
        now = time.perf_counter()
        if now < self.next:
            return
        self.next = now + 0.5
        idle, kernel, user, created, exited, pkernel, puser = [wt.FILETIME() for unused in range(7)]
        cycles = ctypes.c_ulonglong()
        if not self.api.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
            return
        if not self.api.GetProcessTimes(self.handle, ctypes.byref(created), ctypes.byref(exited), ctypes.byref(pkernel), ctypes.byref(puser)):
            return
        self.api.QueryProcessCycleTime(self.handle, ctypes.byref(cycles))
        ticks = [value.dwLowDateTime | (value.dwHighDateTime << 32) for value in [idle, kernel, user, pkernel, puser]]
        self.file.write(f"{now:.6f}," + ",".join(str(value) for value in ticks + [cycles.value]) + "\n")
        self.file.flush()

    def close(self):
        self.file.close()
