#!/usr/bin/env bash
# Reproducible Linux Mint/Ubuntu host tools; all files stay under work/windows.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
W="$ROOT/work/windows"; D="$W/downloads"
mkdir -p "$D"
fetch() {
    local name="$1" url="$2" digest="$3"
    [[ -f "$D/$name" ]] || curl -fL --retry 2 "$url" -o "$D/$name"
    echo "$digest  $D/$name" | sha256sum -c -
}
fetch llvm-mingw.tar.xz https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21
fetch python.zip https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip 4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3
fetch dawn-windows.tar.gz https://github.com/theofficialgman/dawn-build/releases/download/v20260603.191052/dawn-windows-amd64.tar.gz 13be9cff8b9b179c42dcd16aeabb6effcc8f0dfdcc14463eda2a5caeda225142
fetch cppwinrt-source.tar.gz https://github.com/microsoft/cppwinrt/archive/refs/tags/2.0.240405.15.tar.gz 1da61942ee0a4500440592b7daf7bf5151b2c8aa92e57f85259cfd13c57be0d0
fetch winmd.tar.gz https://github.com/microsoft/winmd/archive/0f1eae3bfa63fa2ba3c2912cbfe72a01db94cc5a.tar.gz e7d1755d763b7ad48acc66d04df7028951ec9af4e17496dfcfe2bc176f945af1
fetch contracts.zip https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.contracts/10.0.26100.4948/microsoft.windows.sdk.contracts.10.0.26100.4948.nupkg edb7383163900999ce96817ac451c9a7885599295c445f5ae01e62b93290e01c
fetch zlib.tar.gz https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
fetch vc_redist.x64.exe https://aka.ms/vs/17/release/vc_redist.x64.exe cc0ff0eb1dc3f5188ae6300faef32bf5beeba4bdd6e8e445a9184072096b713b
python3 scripts/windows/extract_vcredist.py
[[ -f "$W/dtk.exe" ]] || curl -fL https://github.com/encounter/decomp-toolkit/releases/download/v1.8.4/dtk-windows-x86_64.exe -o "$W/dtk.exe"
[[ -d "$W/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64" ]] || tar -xf "$D/llvm-mingw.tar.xz" -C "$W"
[[ -d "$W/cppwinrt-2.0.240405.15" ]] || tar -xf "$D/cppwinrt-source.tar.gz" -C "$W"
[[ -d "$W/winmd-0f1eae3bfa63fa2ba3c2912cbfe72a01db94cc5a" ]] || tar -xf "$D/winmd.tar.gz" -C "$W"
[[ -d "$W/zlib-1.3.2" ]] || tar -xf "$D/zlib.tar.gz" -C "$W"
# Wine is extracted rather than installed. Download its small extra dependencies
# even if absent on the host. Existing system libraries resolve the remaining ones.
(cd "$D"; apt-get download wine64 libwine libz-mingw-w64 libcapi20-3t64 libgphoto2-6t64 libgphoto2-port12t64 libpcsclite1 libxkbregistry0)
for deb in "$D/"*.deb; do dpkg-deb -x "$deb" "$W/wine"; done
cat > "$W/wine/usr/lib/wine/wineserver" <<'SERVER'
#!/bin/sh
exec "$(dirname "$0")/wineserver64" -p0 "$@"
SERVER
cp "$W/wine/usr/x86_64-w64-mingw32/lib/zlib1.dll" "$W/wine/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/"
python3 - <<'PY'
from pathlib import Path
import tarfile, zipfile
w=Path('work/windows')
for name in ('python','contracts'):
    if not (w/name).is_dir():
        with zipfile.ZipFile(w/'downloads'/f'{name}.zip') as z:z.extractall(w/name)
if not (w/'dawn').is_dir():
    with tarfile.open(w/'downloads/dawn-windows.tar.gz') as t:t.extractall(w/'dawn',filter='data')
(w/'python/python312._pth').write_text('python312.zip\n.\nLib/site-packages\nimport site\n')
PY
cmake -S "$W/cppwinrt-2.0.240405.15" -B "$W/cppwinrt-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCPPWINRT_BUILD_VERSION=2.0.240405.15 \
    -DEXTERNAL_WINMD_INCLUDE_DIR="$W/winmd-0f1eae3bfa63fa2ba3c2912cbfe72a01db94cc5a/src"
cmake --build "$W/cppwinrt-build" --parallel 1
"$W/cppwinrt-build/cppwinrt" -input "$W/contracts/ref/netstandard2.0" -output "$W/cppwinrt"
# Download with Linux pip; Wine does not need network access to install wheels.
python3 -m pip download --only-binary=:all: --platform win_amd64 --python-version 312 \
    --implementation cp --abi cp312 --dest "$W/wheels" \
    'PySide6==6.11.2' 'tomlkit==0.15.1' 'pyinstaller==6.22.3' 'numpy==1.26.4' \
    'PyOpenGL==3.1.10' pip setuptools wheel pefile pywin32-ctypes
python3 - <<'PY'
from pathlib import Path
import zipfile
w=Path('work/windows');site=w/'python/Lib/site-packages';site.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(next((w/'wheels').glob('pip-*.whl'))) as z:z.extractall(site)
PY
cp scripts/windows/sitecustomize.py "$W/python/Lib/site-packages/sitecustomize.py"
# Debian Wine's relocated loader needs the Windows system DLLs in its prefix.
bash scripts/windows/wine.sh "$W/python/python.exe" -V || true
mkdir -p "$W/wine-prefix/drive_c/windows/system32"
cp -asn "$W/wine/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/." "$W/wine-prefix/drive_c/windows/system32/"
bash scripts/windows/wine.sh "$W/python/python.exe" -m pip install \
    --no-index --find-links "$W/wheels" -r launcher/requirements.txt numpy==1.26.4
printf 'Windows tools ready in %s\n' "$W"
