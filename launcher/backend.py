"""Installation and launch services. No game data is included in the launcher."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time
import uuid

import tomlkit
from PySide6.QtCore import QThread, Signal

from process_environment import native_library_path

from mii_data import Database, atomic_bytes, database_path, ensure_database
from mii_resources import recover_resource, validate_resource

GAME_ID = "RZTP01"
DOL_SHA256 = "855e4ffe4f8d69d172c44b05ec3a4ddb89645bebfadca41c98effab69111d448"
IMAGE_EXTENSIONS = {".iso", ".rvz", ".wbfs", ".wia", ".ciso", ".gcz"}
MII_FILES = ("RFL_Res.dat", "RFL_DB.dat")
APP_ID = "org.riisorted.launcher"


def atomic_text(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_text(text, encoding="utf-8")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def read_json(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"Invalid metadata in {path.name}")
    return value


def child_environment() -> dict[str, str]:
    env = os.environ.copy()
    # PyInstaller's private libraries belong to the launcher, not the game/dtk.
    if getattr(sys, "frozen", False):
        for key in ("LD_LIBRARY_PATH", "LIBPATH"):
            original = env.get(key + "_ORIG")
            if original is None:
                env.pop(key, None)
            else:
                env[key] = original
    return env




def payload_root() -> Path:
    # PyInstaller onedir places bundled data under _MEIPASS. Development uses
    # the exact same staged payload, prepared by scripts/release/package.py.
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS) / "payload"
    return Path(__file__).resolve().parent / ("payload-win32" if sys.platform == "win32" else "payload")


def game_executable() -> str:
    return "Resortcompiled.exe" if sys.platform == "win32" else "Resortcompiled"


def installation(path: Path, require_runtime=True) -> dict:
    meta = read_json(path / "installation.json")
    if meta.get("application") != APP_ID or meta.get("schema") != 1:
        raise ValueError("This is not a supported launcher installation.")
    if meta.get("game_id") != GAME_ID or meta.get("dol_sha256") != DOL_SHA256:
        raise ValueError("This installation uses a different game revision.")
    required = [path / "Game/sys/main.dol", path / "Game/sys/boot.bin"]
    if require_runtime:
        required += [path / "Runtime" / game_executable(), path / "Runtime/portable.txt"]
    for item in required:
        if not item.is_file():
            raise ValueError(f"Installation is incomplete: {item.name} is missing.")
    if not (path / "Game/files").is_dir():
        raise ValueError("The extracted game files are missing.")
    return meta


def file_hash(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def bundled_runtime():
    payload = payload_root()
    release = read_json(payload / 'release.json')
    if (release.get('game_id') != GAME_ID or release.get('dol_sha256') != DOL_SHA256
            or release.get('platform') != sys.platform):
        raise ValueError('The bundled runtime does not match this launcher/platform.')
    source = payload / 'runtime'
    files = release.get('runtime_files')
    if files is None:  # Allow older release payloads to be adopted safely.
        files = {p.relative_to(source).as_posix(): file_hash(p)
                 for p in source.rglob('*') if p.is_file()}
    if not isinstance(files, dict) or game_executable() not in files:
        raise ValueError('The launcher runtime payload is incomplete. Extract the entire release archive.')
    for name, digest in files.items():
        relative = Path(name)
        if (relative.is_absolute() or '..' in relative.parts or 'UserData' in relative.parts
                or not isinstance(digest, str) or len(digest) != 64):
            raise ValueError('Invalid bundled runtime manifest.')
    return source, release, files


def runtime_matches(runtime: Path, files: dict, checkpoint=lambda: None):
    for name, digest in files.items():
        checkpoint()
        item = runtime / name
        if not item.is_file() or item.is_symlink() or file_hash(item) != digest:
            return False
    return (runtime / 'portable.txt').is_file()


def stamp_runtime(runtime: Path, release: dict, files: dict):
    (runtime / 'portable.txt').touch()
    atomic_text(runtime / 'runtime-release.json', json.dumps({
        'version': release['version'], 'runtime_files': files,
        'netplay_simulation_id': release.get('netplay_simulation_id'),
    }, indent=2))


def ensure_runtime(root: Path, checkpoint=lambda: None, report=lambda text: None):
    """Install this launcher's native payload, preserving UserData and rollback.

    Caller holds the installation game lock. No compilation or downloads.
    """
    installation(root, require_runtime=False)
    journal = root / '.runtime-update.json'
    if journal.exists():
        interrupted = read_json(journal)
        names = (interrupted.get('stage', ''), interrupted.get('backup', ''))
        if any(not isinstance(name, str) or Path(name).name != name for name in names):
            raise ValueError('Invalid runtime recovery metadata.')
        previous_stage, previous_backup = (root / name for name in names)
        if not names[0].startswith('.runtime-install-') or not names[1].startswith('.runtime-backup-'):
            raise ValueError('Invalid runtime recovery directories.')
        if not (root / 'Runtime').exists() and previous_backup.exists():
            if (previous_stage / 'UserData').exists() and not (previous_backup / 'UserData').exists():
                (previous_stage / 'UserData').rename(previous_backup / 'UserData')
            previous_backup.rename(root / 'Runtime')
            report('Recovered the previous runtime after an interrupted update.')
        elif not (root / 'Runtime').exists() and previous_stage.exists():
            previous_stage.rename(root / 'Runtime')
            report('Recovered the staged runtime after an interrupted installation.')
        # Never discard a directory still holding user data during recovery.
        if (previous_stage / 'UserData').exists():
            raise ValueError(f'Runtime recovery needs attention; preserved data at {previous_stage}')
        shutil.rmtree(previous_stage, ignore_errors=True)
        journal.unlink()
    source, release, files = bundled_runtime()
    runtime = root / 'Runtime'
    if runtime_matches(runtime, files, checkpoint):
        stamp_runtime(runtime, release, files)
        report(f"Runtime ready · {release['version']}")
        return False
    stage = root / ('.runtime-install-' + uuid.uuid4().hex)
    backup = root / ('.runtime-backup-' + uuid.uuid4().hex)
    saved_old = moved_data = committed = False
    try:
        report(f"Installing bundled runtime · {release['version']}…")
        stage.mkdir()
        for name, digest in files.items():
            checkpoint()
            item = source / name
            if item.is_symlink() or not item.is_file() or file_hash(item) != digest:
                raise ValueError(f'Bundled runtime file is missing or damaged: {name}')
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)
        stamp_runtime(stage, release, files)
        checkpoint()
        atomic_text(journal, json.dumps({'stage': stage.name, 'backup': backup.name}))
        # The directory swap is on the installation's filesystem. Once it
        # starts, finish or roll back before acknowledging cancellation.
        if runtime.exists():
            runtime.rename(backup)
            saved_old = True
        if (backup / 'UserData').exists():
            (backup / 'UserData').rename(stage / 'UserData')
            moved_data = True
        else:
            (stage / 'UserData').mkdir()
        stage.rename(runtime)
        committed = True
        report(f"Runtime installed · {release['version']}; saves and Miis preserved.")
        if saved_old:
            report(f'Previous runtime retained at {backup}')
        return True
    finally:
        if not committed:
            if moved_data:
                (stage / 'UserData').rename(backup / 'UserData')
            if saved_old:
                backup.rename(runtime)
            shutil.rmtree(stage, ignore_errors=True)
        journal.unlink(missing_ok=True)


def missing_mii(path: Path) -> list[str]:
    folder = path / "Runtime/UserData/NAND/shared2/menu/FaceLib"
    return [name for name in MII_FILES
            if not (folder / name).is_file() or (folder / name).stat().st_size == 0]


def import_mii(path: Path, source: Path):
    installation(path)
    candidates = (source, source / "shared2/menu/FaceLib",
                  source / "Wii/shared2/menu/FaceLib")
    folder = next((p for p in candidates if any((p / n).is_file() for n in MII_FILES)), None)
    if folder is None:
        raise ValueError("Choose a FaceLib or Wii NAND folder containing RFL_DB.dat or RFL_Res.dat.")
    target = path / "Runtime/UserData/NAND/shared2/menu/FaceLib"
    resource = folder / "RFL_Res.dat"
    artwork = None
    if resource.is_file() and not (target / resource.name).exists():
        artwork = resource.read_bytes()
        validate_resource(artwork)
    database = folder / "RFL_DB.dat"
    destination = Database(database_path(path))
    added = 0
    if database.is_file():
        incoming = Database(database)
        identities = {record[24:32] for _, record in destination.records()}
        for _, record in incoming.records():
            if record[24:32] not in identities:
                destination.put(record)
                identities.add(record[24:32])
                added += 1
    # Validate all records and capacity before changing any existing data.
    if artwork is not None:
        atomic_bytes(target / "RFL_Res.dat", artwork)
    if added or destination.original is None:
        destination.save()
    return added


def configure_game(path: Path, edition: str):
    if edition not in ("original", "riisorted"):
        raise ValueError("Unknown edition.")
    installation(path)
    if missing_mii(path):
        raise ValueError("Finish Mii setup before playing: create a database and set up the face artwork.")
    Database(database_path(path))  # Validate the collection without changing it.
    validate_resource((path / "Runtime/UserData/NAND/shared2/menu/FaceLib/RFL_Res.dat").read_bytes())
    # Check identity at launch as well: a manually replaced DOL cannot silently
    # run against this fixed revision's translated code.
    verify_game(path / "Game")
    config = path / "Runtime/UserData/Config.toml"
    document = tomlkit.parse(config.read_text(encoding="utf-8")) if config.exists() else tomlkit.document()
    if "paths" not in document:
        document["paths"] = tomlkit.table()
    paths = document["paths"]
    # Paths are relative to UserData, making the complete installation movable.
    paths["dvd_root"] = "../../Game"
    paths["nand_root"] = "NAND"
    paths["overlay_roots"] = (["../../Editions/Riisorted/files"] if edition == "riisorted" else [])
    # A legacy MKW mod root must not bleed into the Original edition.
    paths.pop("retro_rewind_root", None)
    atomic_text(config, tomlkit.dumps(document))


def verify_game(root: Path):
    boot = root / "sys/boot.bin"
    dol = root / "sys/main.dol"
    if not boot.is_file() or not dol.is_file() or not (root / "files").is_dir():
        raise ValueError("The disc could not be extracted as a Wii game. Choose a complete disc dump.")
    with boot.open("rb") as stream:
        identity = stream.read(6).decode("ascii", errors="replace")
    if identity != GAME_ID:
        raise ValueError(f"This build supports PAL Wii Sports Resort ({GAME_ID}). "
                         f"The selected disc identifies itself as {identity}.")
    with dol.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != DOL_SHA256:
        raise ValueError("This Wii Sports Resort revision does not match the supported build. "
                         "Use an unmodified PAL RZTP01 dump matching the release instructions.")


class Cancelled(Exception):
    pass


class Installer(QThread):
    phase = Signal(str, str)
    output = Signal(str)
    installed = Signal(str)
    failed = Signal(str)
    cancelled = Signal()

    def __init__(self, image: Path, library: Path, log: Path):
        super().__init__()
        self.image, self.library, self.log = image, library, log
        self.stop = threading.Event()

    def cancel(self):
        self.stop.set()

    def checkpoint(self):
        if self.stop.is_set():
            raise Cancelled()

    def command(self, arguments: list[str]):
        self.log.parent.mkdir(parents=True, exist_ok=True)
        # A file-backed pipe cannot deadlock when an extractor emits a large
        # amount of output; cancellation never waits on a blocking readline.
        with self.log.open("ab", buffering=0) as output:
            with native_library_path():
                process = subprocess.Popen(arguments, stdout=output, stderr=subprocess.STDOUT,
                                           env=child_environment(),
                                           creationflags=(subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0))
            try:
                with self.log.open("rb") as reader:
                    while process.poll() is None:
                        if self.stop.wait(.15):
                            raise Cancelled()
                        chunk = reader.read(32768)
                        if chunk:
                            self.output.emit(chunk.decode("utf-8", errors="replace"))
                    remaining = reader.read(65536)
                    if remaining:
                        self.output.emit(remaining.decode("utf-8", errors="replace"))
                self.checkpoint()
                if process.returncode:
                    raise RuntimeError("Disc extraction failed. Check the installation details, "
                                       "then try another complete dump of your disc.")
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()

    def copy_runtime(self, source: Path, target: Path):
        target.mkdir()
        for item in source.rglob("*"):
            self.checkpoint()
            relative = item.relative_to(source)
            if "UserData" in relative.parts or item.is_symlink():
                raise ValueError("The release payload contains user data or a symbolic link.")
            destination = target / relative
            if item.is_dir():
                destination.mkdir(exist_ok=True)
            elif item.is_file():
                shutil.copy2(item, destination)

    def run(self):
        stage = None
        try:
            self.phase.emit("Preparing your island", "Checking the disc and available storage…")
            self.checkpoint()
            if not self.image.is_file() or self.image.suffix.lower() not in IMAGE_EXTENSIONS:
                raise ValueError("Choose an ISO, RVZ, WBFS, WIA, CISO or GCZ disc image.")
            payload = payload_root()
            if not (payload / "release.json").is_file():
                raise ValueError("The release payload is missing. Extract the complete launcher download again.")
            release = read_json(payload / "release.json")
            if release.get("game_id") != GAME_ID or release.get("dol_sha256") != DOL_SHA256:
                raise ValueError("The bundled runtime does not match this launcher.")
            dtk = payload / "tools" / ("dtk.exe" if os.name == "nt" else "dtk")
            runtime = payload / "runtime"
            if not dtk.is_file() or not (runtime / game_executable()).is_file():
                raise ValueError("The release package is incomplete. Extract the entire launcher download again.")
            self.library.mkdir(parents=True, exist_ok=True)
            destination = self.library / "Resort"
            if destination.exists():
                raise ValueError("This folder already contains a Resort installation. "
                                 "Use ‘Locate installation’ or choose another folder.")
            # Wii data partition plus room for runtime and extraction overhead.
            needed = int(release.get("required_free_bytes", 6 * 1024**3))
            if shutil.disk_usage(self.library).free < needed:
                raise ValueError(f"At least {needed / 1024**3:.1f} GB of free space is needed in this folder.")
            stage = self.library / (".resort-install-" + uuid.uuid4().hex)
            stage.mkdir()
            self.phase.emit("Unpacking your disc", "This can take a few minutes. Your original image stays untouched.")
            self.command([str(dtk), "disc", "extract", str(self.image), str(stage / "Game")])
            self.phase.emit("Checking the game", "Verifying the game ID and supported executable revision…")
            verify_game(stage / "Game")
            self.checkpoint()
            self.phase.emit("Getting ready to play", "Installing the native runtime and edition folders…")
            self.copy_runtime(runtime, stage / "Runtime")
            _, bundled_release, runtime_files = bundled_runtime()
            stamp_runtime(stage / 'Runtime', bundled_release, runtime_files)
            (stage / "Runtime/UserData").mkdir(exist_ok=True)
            (stage / "Editions/Riisorted/files").mkdir(parents=True)
            atomic_text(stage / "installation.json", json.dumps({
                "schema": 1, "application": APP_ID, "game_id": GAME_ID,
                "dol_sha256": DOL_SHA256, "release": release["version"],
                "installed_at": int(time.time()),
            }, indent=2))
            self.phase.emit("Setting up Miis", "Creating your collection and recovering face artwork from your disc…")
            ensure_database(stage)
            try:
                recover_resource(self.image, stage, dtk, self.command, self.checkpoint)
            except Cancelled:
                raise
            except (RuntimeError, ValueError) as exc:
                # Scrubbed discs may omit the update partition. Keep the game
                # installed and offer resource setup again with a complete dump.
                notice = f"Mii artwork setup needs attention: {exc}"
                self.output.emit(notice)
                with self.log.open("a", encoding="utf-8") as log:
                    log.write("\n" + notice + "\n")
            self.checkpoint()
            # Commit only a complete, verified installation; never replace an
            # existing installation or its saves during failure/cancellation.
            if destination.exists():
                raise ValueError("An installation appeared in this folder. Choose another folder.")
            stage.rename(destination)
            stage = None
            self.installed.emit(str(destination))
        except Cancelled:
            self.cancelled.emit()
        except Exception as exc:
            try:
                self.log.parent.mkdir(parents=True, exist_ok=True)
                with self.log.open("a", encoding="utf-8") as log:
                    log.write(f"\nInstallation error: {exc}\n")
            except OSError:
                pass
            self.failed.emit(str(exc))
        finally:
            if stage is not None:
                shutil.rmtree(stage, ignore_errors=True)


class RuntimeInstaller(Installer):
    """Repair/update native files without extracting the disc or replacing saves."""
    def __init__(self, root: Path, log: Path):
        super().__init__(Path(), root, log)

    def run(self):
        try:
            self.phase.emit('Preparing your runtime', 'Installing this release’s native game files…')
            def report(text):
                self.output.emit(text)
                self.log.parent.mkdir(parents=True, exist_ok=True)
                with self.log.open('a', encoding='utf-8') as stream:
                    stream.write(text + '\n')
            ensure_runtime(self.library, self.checkpoint, report)
            self.installed.emit(str(self.library))
        except Cancelled:
            self.cancelled.emit()
        except Exception as exc:
            self.failed.emit(str(exc))


class MiiResourceInstaller(Installer):
    """Use the same cancellation/logging lifecycle for an existing installation."""
    def __init__(self, image: Path, root: Path, log: Path):
        super().__init__(image, root, log)

    def run(self):
        try:
            installation(self.library)
            self.phase.emit("Setting up Mii artwork", "Reading the System Menu resources from your disc…")
            dtk = payload_root() / "tools" / ("dtk.exe" if os.name == "nt" else "dtk")
            recover_resource(self.image, self.library, dtk, self.command, self.checkpoint)
            self.checkpoint()
            ensure_database(self.library)
            self.installed.emit(str(self.library))
        except Cancelled:
            self.cancelled.emit()
        except Exception as exc:
            self.failed.emit(str(exc))
