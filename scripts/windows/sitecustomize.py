"""Build environment only: Wine 9 lacks Python 3.12's CopyFile2 fast path."""
import os
if os.environ.get('RESORT_CROSS_WINE') == '1':
    import shutil
    class PortableCopies:
        def __init__(self, api): self.api = api
        def __getattr__(self, name):
            if name == 'CopyFile2': raise AttributeError(name)
            return getattr(self.api, name)
    shutil._winapi = PortableCopies(shutil._winapi)
