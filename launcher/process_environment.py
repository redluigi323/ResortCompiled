# SPDX-License-Identifier: GPL-3.0-only
"""Keep launcher-private DLLs out of native child processes."""
from contextlib import contextmanager
import os
import sys

@contextmanager
def native_library_path():
    # Windows children inherit SetDllDirectory, which PyInstaller points at its
    # private Qt bundle. Restore normal DLL lookup just while creating a child.
    frozen_windows = os.name == "nt" and getattr(sys, "frozen", False)
    if frozen_windows:
        import ctypes
        set_directory = ctypes.windll.kernel32.SetDllDirectoryW
        set_directory.argtypes = [ctypes.c_wchar_p]
        set_directory.restype = ctypes.c_int
        set_directory(None)
    try:
        yield
    finally:
        if frozen_windows:
            set_directory(str(sys._MEIPASS))

