"""Riisorted release launcher. Run with Python 3.11+ or freeze with package.py."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import time

from PySide6.QtCore import (Qt, QRectF, QSize, QLockFile, QProcess, QProcessEnvironment,
                            QStandardPaths, QUrl)
from PySide6.QtGui import QColor, QDesktopServices, QPainter, QPainterPath, QPixmap
from PySide6.QtWidgets import (QApplication, QButtonGroup, QFileDialog, QFrame,
    QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit,
    QProgressBar, QPushButton, QScrollArea, QSizePolicy, QVBoxLayout, QWidget)

from backend import (Installer, MiiResourceInstaller, atomic_text, child_environment, configure_game,
                     game_executable, import_mii, installation, missing_mii, native_library_path, read_json)

from mii_data import database_path, ensure_database
from mii_editor import MiiManager

STYLE = """
QWidget { font-family: 'Inter', 'Segoe UI', sans-serif; font-size: 14px; color: #193e45; }
QMainWindow, #surface { background: #f3f6f3; }
#sidebar { background: #133e48; }
#brand { color: #f3faf6; font-size: 26px; font-weight: 800; }
#sideNote { color: #a8c5c8; font-size: 12px; }
#sidebar QPushButton { text-align: left; color: #d3e4e4; background: transparent;
  border: none; border-radius: 9px; padding: 13px; }
#sidebar QPushButton:hover { background: #24535d; }
#sidebar QPushButton:checked { color: white; background: #2c6269; }
#eyebrow { color: #598078; font-size: 11px; font-weight: 700; letter-spacing: 2px; }
#pageTitle { font-size: 28px; font-weight: 750; }
#muted { color: #647a7a; }
#heroTitle { color: #ffffff; font-size: 34px; font-weight: 800; }
#heroCopy { color: #e2f9ef; font-size: 14px; }
#heroEyebrow { color: #e2f9ef; font-size: 11px; font-weight: 700; letter-spacing: 2px; }
#card, #notice { background: #ffffff; border: 1px solid #dce6df; border-radius: 14px; }
#notice { background: #fff4dd; border-color: #efd6a6; }
#cardTitle { font-size: 17px; font-weight: 700; }
#edition { text-align: left; background: white; border: 2px solid #dce6df;
  border-radius: 14px; padding: 18px; min-height: 74px; font-size: 16px; }
#edition:hover { border-color: #82b6a7; background: #fafffa; }
#edition:checked { border-color: #187d70; background: #e5f3ea; color: #105c50; }
QPushButton { background: #e3ebe5; border: 1px solid transparent; padding: 11px 17px;
  border-radius: 8px; font-weight: 600; }
QPushButton:hover { background: #d5e5db; }
QPushButton:disabled { color: #91a49d; background: #e4e9e3; }
QPushButton#primary { color: white; background: #187d70; padding: 14px 26px; }
QPushButton#primary:hover { background: #12675d; }
QPushButton#primary:disabled { background: #bdcfc3; color: #f7faf5; }
QLineEdit { background: white; border: 1px solid #ccdcd3; border-radius: 8px; padding: 11px; }
QLineEdit:focus { border-color: #187d70; }
QPlainTextEdit { background: #163f48; color: #dbefe7; border: none; border-radius: 8px;
  font-family: monospace; font-size: 11px; padding: 10px; }
QProgressBar { border: none; background: #dce8dd; border-radius: 5px; min-height: 8px; max-height: 8px; }
QProgressBar::chunk { background: #187d70; border-radius: 5px; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 9px; }
QScrollBar::handle:vertical { background: #bdcec3; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
"""


def label(text: str, name: str = "", wrap: bool = False) -> QLabel:
    result = QLabel(text)
    result.setTextFormat(Qt.TextFormat.PlainText)
    result.setObjectName(name)
    result.setWordWrap(wrap)
    return result


def button(text, action, name=""):
    result = QPushButton(text)
    result.setObjectName(name)
    result.setCursor(Qt.CursorShape.PointingHandCursor)
    result.clicked.connect(action)
    return result


def card() -> tuple[QFrame, QVBoxLayout]:
    frame = QFrame()
    frame.setObjectName("card")
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(22, 20, 22, 20)
    layout.setSpacing(12)
    return frame, layout


class ResortBanner(QWidget):
    """Display the supplied logo at its original proportions."""
    def __init__(self):
        super().__init__()
        self.logo = QPixmap(str(Path(__file__).resolve().parent / "assets/branding/resortcompiled-logo.png"))
        policy = QSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        policy.setHeightForWidth(True); self.setSizePolicy(policy)
        self.setMinimumHeight(120)
        self.setAccessibleName("Wii Sports ResortCompiled logo")

    def sizeHint(self):
        return QSize(850, self.heightForWidth(850))

    def heightForWidth(self, width):
        return max(120, round(width * self.logo.height() / max(1, self.logo.width())))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        clip = QPainterPath(); clip.addRoundedRect(QRectF(self.rect()), 16, 16)
        painter.setClipPath(clip)
        painter.fillRect(self.rect(), QColor("white"))
        if not self.logo.isNull():
            size = self.logo.size().scaled(self.size(), Qt.AspectRatioMode.KeepAspectRatio)
            target = QRectF((self.width()-size.width())/2, (self.height()-size.height())/2,
                            size.width(), size.height())
            painter.drawPixmap(target, self.logo, QRectF(self.logo.rect()))


class Launcher(QMainWindow):
    def __init__(self, data: Path):
        super().__init__()
        self.data = data
        self.state_file = data / "launcher.json"
        self.worker = None
        self.game = None
        self.game_lock = None
        self.setup_lock = None
        self.close_after_cancel = False
        self.path = None
        self.last_log = None
        self.edition = "original"
        self.setWindowTitle("Riisorted · Resort Launcher")
        self.resize(1120, 830)
        self.setMinimumSize(960, 720)
        try:
            state = read_json(self.state_file)
            self.edition = state.get("edition", "original")
            if self.edition not in ("original", "riisorted"):
                self.edition = "original"
            candidate = Path(state.get("installation", ""))
            if state.get("installation"):
                installation(candidate)
                self.path = candidate
        except (OSError, ValueError):
            pass
        self.build_ui()
        self.refresh()

    def build_ui(self):
        root = QWidget(); root.setObjectName("surface")
        row = QHBoxLayout(root); row.setContentsMargins(0, 0, 0, 0); row.setSpacing(0)
        self.setCentralWidget(root)
        sidebar = QWidget(); sidebar.setObjectName("sidebar"); sidebar.setFixedWidth(200)
        nav = QVBoxLayout(sidebar); nav.setContentsMargins(20, 32, 20, 24); nav.setSpacing(8)
        nav.addWidget(label("Riisorted", "brand"))
        nav.addWidget(label("THE RESORT LAUNCHER", "sideNote")); nav.addSpacing(34)
        home = button("Your island", self.refresh); home.setCheckable(True); home.setChecked(True)
        nav.addWidget(home)
        self.locate = button("Locate installation", self.locate_install)
        nav.addWidget(self.locate)
        self.folder_button = button("Open game folder", self.open_folder)
        nav.addWidget(self.folder_button)
        self.logs_button = button("Open logs", self.open_logs)
        nav.addWidget(self.logs_button)
        nav.addStretch()
        nav.addWidget(label("Made for a little\nisland time.", "sideNote"))
        nav.addSpacing(14)
        nav.addWidget(label("PAL • RZTP01\nLauncher preview 0.1", "sideNote"))
        row.addWidget(sidebar)
        scroll = QScrollArea(); scroll.setWidgetResizable(True)
        body = QWidget(); body.setObjectName("surface")
        self.body = QVBoxLayout(body); self.body.setContentsMargins(32, 26, 32, 28); self.body.setSpacing(18)
        scroll.setWidget(body); row.addWidget(scroll, 1)
        self.body.addWidget(label("WELCOME TO THE RESORT", "eyebrow"))
        header = QHBoxLayout()
        header.addWidget(label("A place to play.", "pageTitle")); header.addStretch()
        self.status = label("Not installed", "muted"); header.addWidget(self.status)
        self.body.addLayout(header)
        self.body.addWidget(ResortBanner())

        self.setup, layout = card()
        layout.addWidget(label("Bring your copy. We’ll do the rest.", "cardTitle"))
        layout.addWidget(label("Choose your own Wii Sports Resort disc image. The launcher extracts it "
                               "and sets up the native game for you.", "muted", True))
        layout.addWidget(label("01   YOUR DISC IMAGE", "eyebrow"))
        self.image_path = QLineEdit(); self.image_path.setPlaceholderText("ISO, RVZ, WBFS, WIA, CISO or GCZ")
        self.image_path.setReadOnly(True)
        image_row = QHBoxLayout(); image_row.addWidget(self.image_path, 1)
        image_row.addWidget(button("Choose disc…", self.choose_image)); layout.addLayout(image_row)
        layout.addWidget(label("02   INSTALLATION FOLDER", "eyebrow"))
        self.library_path = QLineEdit(str(self.data / "Library")); self.library_path.setReadOnly(True)
        directory_row = QHBoxLayout(); directory_row.addWidget(self.library_path, 1)
        directory_row.addWidget(button("Browse…", self.choose_library)); layout.addLayout(directory_row)
        layout.addWidget(label("Uses about 6 GB of free space. This build supports PAL RZTP01 only. "
                               "Your disc image is never changed.", "muted", True))
        self.install_button = button("Install Wii Sports Resort", self.start_install, "primary")
        layout.addWidget(self.install_button)
        self.body.addWidget(self.setup)

        self.progress_card, layout = card()
        self.phase_label = label("Preparing…", "cardTitle"); layout.addWidget(self.phase_label)
        self.phase_copy = label("", "muted", True); layout.addWidget(self.phase_copy)
        self.progress = QProgressBar(); self.progress.setRange(0, 0); layout.addWidget(self.progress)
        self.details = QPlainTextEdit(); self.details.setReadOnly(True)
        self.details.setMaximumBlockCount(800); self.details.setFixedHeight(140)
        self.details.hide()
        actions = QHBoxLayout()
        actions.addWidget(button("Installation details", lambda: self.details.setVisible(not self.details.isVisible())))
        actions.addStretch(); self.cancel_button = button("Cancel", self.cancel_install)
        actions.addWidget(self.cancel_button); layout.addLayout(actions); layout.addWidget(self.details)
        self.body.addWidget(self.progress_card)

        self.library_widget = QWidget(); library = QVBoxLayout(self.library_widget)
        library.setContentsMargins(0, 0, 0, 0); library.setSpacing(14)
        library.addWidget(label("CHOOSE YOUR EDITION", "eyebrow"))
        choices = QHBoxLayout(); choices.setSpacing(14)
        self.group = QButtonGroup(self); self.group.setExclusive(True)
        self.editions = {}
        for key, title in [("original", "Wii Sports Resort\nThe regular game"),
                           ("riisorted", "Riisorted\nYour future enhanced edition")]:
            choice = button(title, lambda checked=False, k=key: self.select_edition(k), "edition")
            choice.setCheckable(True); choice.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            self.group.addButton(choice); self.editions[key] = choice; choices.addWidget(choice)
        library.addLayout(choices)
        self.edition_copy = label("", "muted", True); library.addWidget(self.edition_copy)
        self.mii_card, mii = card(); self.mii_card.setObjectName("notice")
        mii.addWidget(label("Make Mii at home!", "cardTitle"))
        self.mii_copy = label("Create your Mii's here! or import a preexisting Mii Save Data file! choice is yours.", "muted", True)
        mii.addWidget(self.mii_copy)
        self.artwork_notice = label("The face artwork still needs setup. Choose a complete disc dump containing its update partition.", "muted", True)
        mii.addWidget(self.artwork_notice)
        mii_actions = QHBoxLayout()
        self.manage_mii_button = button("Create / manage Miis…", self.manage_miis)
        self.import_mii_button = button("Import existing collection…", self.choose_mii)
        self.artwork_button = button("Set up face artwork…", self.setup_artwork)
        for control in (self.manage_mii_button, self.import_mii_button, self.artwork_button):
            mii_actions.addWidget(control)
        mii.addLayout(mii_actions)
        library.addWidget(self.mii_card)
        launch_row = QHBoxLayout()
        self.ready_copy = label("", "muted", True); launch_row.addWidget(self.ready_copy, 1)
        self.play_button = button("Play Wii Sports Resort", self.play, "primary")
        self.online_button = button("Online play · Experimental", self.online_play)
        launch_row.addWidget(self.online_button)
        launch_row.addWidget(self.play_button); library.addLayout(launch_row)
        self.body.addWidget(self.library_widget)
        self.message = label("", "muted", True); self.body.addWidget(self.message)
        self.body.addStretch()

    def save_state(self):
        try:
            atomic_text(self.state_file, json.dumps({"installation": str(self.path) if self.path else "",
                                                    "edition": self.edition}, indent=2))
        except OSError as exc:
            self.message.setText(f"Could not remember these settings: {exc}")

    def refresh(self):
        busy = self.worker is not None
        playing = self.game is not None
        installed = self.path is not None
        self.setup.setVisible(not installed and not busy)
        self.progress_card.setVisible(busy)
        self.library_widget.setVisible(installed and not busy)
        self.locate.setEnabled(not busy and not playing)
        self.folder_button.setEnabled(installed)
        self.install_button.setEnabled(bool(self.image_path.text()))
        self.status.setText("Installing…" if busy else "Game running" if playing else
                            "Installed · PAL" if installed else "Not installed")
        if installed:
            try:
                missing = missing_mii(self.path)
                self.mii_card.setVisible(True)
                self.mii_card.setEnabled(not busy and not playing)
                self.artwork_button.setVisible("RFL_Res.dat" in missing)
                self.artwork_notice.setVisible("RFL_Res.dat" in missing)
                self.play_button.setEnabled(not busy and not playing and not missing)
                self.ready_copy.setText("Finish Mii setup using the options above." if missing else
                                       "Enjoy your stay. Close the game to return here." if playing else
                                       "Ready when you are.\nF10 opens in-game settings.")
            except OSError as exc:
                self.play_button.setEnabled(False); self.ready_copy.setText(str(exc))
        for key, choice in self.editions.items():
            choice.setChecked(key == self.edition); choice.setEnabled(not playing and not busy)
        self.edition_copy.setText(
            "The regular Wii Sports Resort experience. Riisorted additions are disabled." if self.edition == "original"
            else "Riisorted includes experimental two-player online play. Online sessions use separate saves; "
                 "both editions share saves and settings when playing offline.")
        self.play_button.setText("Game running…" if playing else
                                 "Play Riisorted" if self.edition == "riisorted" else "Play Wii Sports Resort")
        self.online_button.setVisible(self.edition == "riisorted")
        self.online_button.setEnabled(self.play_button.isEnabled() and not busy and not playing)

    def select_edition(self, edition):
        self.edition = edition; self.save_state(); self.refresh()

    def choose_image(self):
        path, _ = QFileDialog.getOpenFileName(self, "Choose Wii Sports Resort", "",
                     "Wii disc images (*.iso *.rvz *.wbfs *.wia *.ciso *.gcz);;All files (*)")
        if path:
            self.image_path.setText(path); self.refresh()

    def choose_library(self):
        path = QFileDialog.getExistingDirectory(self, "Choose installation folder", self.library_path.text())
        if path:
            self.library_path.setText(path)

    def locate_install(self):
        value = QFileDialog.getExistingDirectory(self, "Locate your Resort installation")
        if not value:
            return
        path = Path(value)
        if not (path / "installation.json").exists():
            path = path / "Resort"
        try:
            installation(path)
            self.path = path; self.save_state(); self.message.setText(""); self.refresh()
        except (OSError, ValueError) as exc:
            self.error(str(exc))

    def start_install(self):
        self.message.setText(""); self.details.clear(); self.details.hide()
        self.last_log = self.data / "Logs" / f"install-{time.time_ns()}.log"
        self.worker = Installer(Path(self.image_path.text()), Path(self.library_path.text()), self.last_log)
        self.start_worker()

    def start_worker(self):
        self.worker.phase.connect(self.install_phase)
        self.worker.output.connect(lambda text: self.details.appendPlainText(text.rstrip()))
        self.worker.installed.connect(self.installed)
        self.worker.failed.connect(self.install_failed)
        self.worker.cancelled.connect(lambda: self.message.setText("Installation cancelled. You can try again whenever you’re ready."))
        self.worker.finished.connect(self.install_finished)
        self.cancel_button.setEnabled(True); self.cancel_button.setText("Cancel")
        self.phase_label.setText("Preparing your island"); self.phase_copy.setText("Checking your disc…")
        self.worker.start(); self.refresh()

    def install_phase(self, title, text):
        self.phase_label.setText(title); self.phase_copy.setText(text)

    def installed(self, value):
        self.path = Path(value); self.save_state()
        self.message.setText("Mii artwork is ready. Create your own Miis or optionally import a collection."
                             if isinstance(self.worker, MiiResourceInstaller) else
                             "Your game is installed. Create your own Miis or optionally import a collection.")

    def install_failed(self, text):
        operation = "Mii artwork setup" if isinstance(self.worker, MiiResourceInstaller) else "Installation"
        self.message.setText(f"{operation} didn’t finish: {text}\nUse Open logs for the full details.")

    def install_finished(self):
        self.worker.deleteLater(); self.worker = None
        if self.setup_lock:
            self.setup_lock.unlock(); self.setup_lock = None
        self.refresh()
        if self.close_after_cancel:
            self.close()

    def cancel_install(self):
        if self.worker:
            self.worker.cancel(); self.cancel_button.setEnabled(False)
            self.cancel_button.setText("Cancelling…")
            self.phase_copy.setText("Stopping extraction and cleaning up temporary files…")

    def data_lock(self):
        if self.game or self.worker or not self.path:
            return None
        lock = QLockFile(str(self.path / ".game.lock")); lock.setStaleLockTime(0)
        if not lock.tryLock(0):
            self.error("Close the game or other launcher before changing Miis."); return None
        return lock

    def manage_miis(self):
        lock = self.data_lock()
        if lock is None:
            return
        try:
            ensure_database(self.path)
            manager = MiiManager(database_path(self.path), self)
            try:
                manager.exec()
            finally:
                manager.deleteLater()
        except (OSError, ValueError) as exc:
            self.error(str(exc))
        finally:
            lock.unlock(); self.refresh()

    def choose_mii(self):
        lock = self.data_lock()
        if lock is None:
            return
        try:
            source = QFileDialog.getExistingDirectory(self, "Choose your FaceLib or Wii NAND folder")
            if source:
                count = import_mii(self.path, Path(source))
                self.message.setText(f"Imported {count} new Miis. Existing Miis were kept.")
        except (OSError, ValueError) as exc:
            self.error(str(exc))
        finally:
            lock.unlock(); self.refresh()

    def setup_artwork(self):
        lock = self.data_lock()
        if lock is None:
            return
        value, _ = QFileDialog.getOpenFileName(self, "Choose a complete Wii Sports Resort disc dump", "",
            "Wii disc images (*.iso *.rvz *.wbfs *.wia *.ciso *.gcz)")
        if not value:
            lock.unlock(); return
        self.setup_lock = lock
        self.message.setText(""); self.details.clear(); self.details.hide()
        self.last_log = self.data / "Logs" / f"mii-setup-{time.time_ns()}.log"
        self.worker = MiiResourceInstaller(Path(value), self.path, self.last_log)
        self.start_worker()

    def play(self):
        if self.game or not self.path:
            return
        lock = QLockFile(str(self.path / ".game.lock")); lock.setStaleLockTime(0)
        if not lock.tryLock(0):
            self.error("This installation is already running in another launcher."); return
        try:
            configure_game(self.path, self.edition)
            process = QProcess(self)
            environment = QProcessEnvironment()
            for key, value in child_environment().items():
                environment.insert(key, value)
            process.setProcessEnvironment(environment)
            process.setWorkingDirectory(str(self.path / "Runtime"))
            log_dir = self.path / "Runtime/UserData/Logs"; log_dir.mkdir(parents=True, exist_ok=True)
            self.last_log = log_dir / f"launcher-{time.time_ns()}.log"
            process.setStandardOutputFile(str(self.last_log))
            process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
            process.setProgram(str(self.path / "Runtime" / game_executable()))
            process.errorOccurred.connect(self.process_error)
            process.finished.connect(self.process_finished)
            self.game_lock = lock; self.game = process
            self.message.setText(""); self.refresh()
            with native_library_path():
                process.start()
                process.waitForStarted(3000)
        except Exception as exc:
            if self.game is not None:
                self.release_process()
            lock.unlock(); self.error(str(exc))

    def online_play(self):
        if self.game or self.worker or not self.path:
            return
        lock = QLockFile(str(self.path / ".game.lock")); lock.setStaleLockTime(0)
        if not lock.tryLock(0):
            self.error("Close the game or other launcher before starting online play."); return
        try:
            configure_game(self.path, "riisorted")
            from netplay_ui import OnlineDialog
            dialog = OnlineDialog(self.path / "Runtime", self.data / "Netplay", self)
            dialog.exec()
            dialog.deleteLater()
        except Exception as exc:
            self.error(str(exc))
        finally:
            lock.unlock()
            self.refresh()

    def process_error(self, error):
        if self.game and error == QProcess.ProcessError.FailedToStart:
            message = self.game.errorString()
            self.release_process()
            self.error(f"The game could not start: {message}")

    def process_finished(self, code, status):
        self.release_process()
        self.message.setText("Welcome back." if code == 0 and status == QProcess.ExitStatus.NormalExit else
                             "The game closed unexpectedly. Open logs to view this run’s diagnostic files.")

    def release_process(self):
        if self.game:
            self.game.deleteLater(); self.game = None
        if self.game_lock:
            self.game_lock.unlock(); self.game_lock = None
        self.refresh()

    def open_folder(self):
        if self.path:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.path)))

    def open_logs(self):
        folder = self.last_log.parent if self.last_log else (
            self.path / "Runtime/UserData/Logs" if self.path else self.data / "Logs")
        folder.mkdir(parents=True, exist_ok=True)
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(folder)))

    def error(self, text):
        QMessageBox.warning(self, "Something needs attention", text)

    def closeEvent(self, event):
        if self.worker:
            answer = QMessageBox.question(self, "Installation in progress", "Cancel installation and close the launcher?")
            event.ignore()
            if answer == QMessageBox.StandardButton.Yes:
                self.close_after_cancel = True; self.cancel_install()
        elif self.game:
            event.ignore()
            QMessageBox.information(self, "Game is running", "Close the game before closing the launcher.")
        else:
            event.accept()


def main():
    app = QApplication(sys.argv)
    app.setOrganizationName("Riisorted"); app.setApplicationName("Resort Launcher")
    app.setStyle("Fusion"); app.setStyleSheet(STYLE)
    data = Path(QStandardPaths.writableLocation(QStandardPaths.StandardLocation.AppLocalDataLocation))
    data.mkdir(parents=True, exist_ok=True)
    lock = QLockFile(str(data / ".launcher.lock")); lock.setStaleLockTime(0)
    if not lock.tryLock(0):
        QMessageBox.information(None, "Already open", "The Resort Launcher is already open.")
        return 0
    window = Launcher(data); window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
