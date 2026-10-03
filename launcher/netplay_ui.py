# SPDX-License-Identifier: GPL-3.0-only
import time
import uuid
import tomllib
from pathlib import Path
from PySide6.QtCore import QThread, Signal, Qt
from PySide6.QtWidgets import (QApplication, QComboBox, QDialog, QFormLayout,
    QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem, QMessageBox,
    QPlainTextEdit, QPushButton, QSpinBox, QVBoxLayout)
from mii_data import Database, name_of
from netplay import OnlineSession, DB
from backend import child_environment


class SessionWorker(QThread):
    line = Signal(str)
    invitation = Signal(str)
    failure = Signal(str)

    def __init__(self, engine, role, address, port, code, parent):
        super().__init__(parent)
        self.engine, self.role, self.address, self.port, self.code = engine, role, address, port, code
        engine.log = self.line.emit
        engine.invitation = self.invitation.emit

    def run(self):
        try:
            self.engine.run_session(self.role, self.address, self.port, self.code)
        except Exception as error:
            self.failure.emit(str(error))


class OnlineDialog(QDialog):
    def __init__(self, runtime: Path, sessions: Path, parent=None):
        super().__init__(parent)
        self.runtime, self.sessions = runtime, sessions
        self.worker = None
        self.engine = None
        self.setWindowTitle('Riisorted · Online play'); self.resize(740, 690)
        layout = QVBoxLayout(self)
        title = QLabel('Play together, from anywhere.'); title.setObjectName('cardTitle')
        layout.addWidget(title)
        copy = QLabel('Experimental two-player direct connection. Both players need the same build and game content. '
                      'Host saves are used in a separate session. Progress stays there while synchronization is being developed.')
        copy.setWordWrap(True); layout.addWidget(copy)
        form = QFormLayout()
        self.role = QComboBox(); self.role.addItems(['Host a session', 'Join a session'])
        form.addRow('Session', self.role)
        self.address = QLineEdit(); self.address.setPlaceholderText('Host IP address (when joining)')
        form.addRow('Host address', self.address)
        self.port = QSpinBox(); self.port.setRange(1, 65535); self.port.setValue(42680)
        form.addRow('Host TCP port', self.port)
        self.code = QPlainTextEdit(); self.code.setPlaceholderText('Paste the host invitation when joining')
        self.code.setMaximumHeight(80); form.addRow('Invitation', self.code)
        layout.addLayout(form)
        self.mii_label = QLabel('Miis to bring (all selected by default):'); layout.addWidget(self.mii_label)
        self.miis = QListWidget(); self.miis.setMaximumHeight(100)
        user = runtime / 'UserData'
        paths = tomllib.loads((user / 'Config.toml').read_text(encoding='utf-8-sig')).get('paths', {})
        nand = Path(paths.get('nand_root') or 'NAND')
        if not nand.is_absolute(): nand = user / nand
        for slot, record in Database(nand / DB).records():
            item = QListWidgetItem(name_of(record)); item.setData(Qt.ItemDataRole.UserRole, slot)
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(Qt.CheckState.Checked); self.miis.addItem(item)
        layout.addWidget(self.miis)
        self.output = QPlainTextEdit(); self.output.setReadOnly(True); self.output.setMaximumBlockCount(500)
        layout.addWidget(self.output, 1)
        buttons = QHBoxLayout()
        self.start = QPushButton('Host session'); self.start.clicked.connect(self.begin)
        self.copy = QPushButton('Copy invitation'); self.copy.setEnabled(False)
        self.copy.clicked.connect(lambda: QApplication.clipboard().setText(self.code.toPlainText()))
        self.stop = QPushButton('Close'); self.stop.clicked.connect(self.finish_or_stop)
        self.keep = QPushButton('Keep guest Miis'); self.keep.setEnabled(False); self.keep.clicked.connect(self.keep_miis)
        for widget in (self.start, self.copy, self.keep, self.stop): buttons.addWidget(widget)
        layout.addLayout(buttons)
        self.role.currentIndexChanged.connect(self.role_changed); self.role_changed()

    def role_changed(self):
        joining = self.role.currentIndex() == 1
        self.start.setText('Join session' if joining else 'Host session')
        self.address.setEnabled(joining); self.port.setEnabled(not joining)
        self.code.setReadOnly(not joining)
        self.miis.setVisible(joining); self.mii_label.setVisible(joining)

    def begin(self):
        role = 'host' if self.role.currentIndex() == 0 else 'join'
        address = self.address.text().strip() if role == 'join' else '0.0.0.0'
        code = self.code.toPlainText().strip()
        if role == 'join' and (not address or not code):
            QMessageBox.information(self, 'Join session', 'Enter the host address and invitation.'); return
        folder = self.sessions / f'{role}-{time.time_ns()}-{uuid.uuid4().hex[:6]}'
        self.engine = OnlineSession(self.runtime, folder)
        self.engine.environment = child_environment()
        if role == 'join':
            self.engine.selected_slots = [self.miis.item(i).data(Qt.ItemDataRole.UserRole)
                for i in range(self.miis.count()) if self.miis.item(i).checkState() == Qt.CheckState.Checked]
        self.worker = SessionWorker(self.engine, role, address, self.port.value(), code, self)
        self.worker.line.connect(self.output.appendPlainText)
        self.worker.invitation.connect(self.show_invitation)
        self.worker.failure.connect(self.output.appendPlainText)
        self.worker.finished.connect(self.done_running)
        for widget in (self.start, self.role, self.address, self.port, self.code, self.miis): widget.setEnabled(False)
        self.keep.setEnabled(False); self.stop.setText('Leave session'); self.worker.start()

    def show_invitation(self, code):
        self.code.setPlainText(code); self.copy.setEnabled(True)
        self.output.appendPlainText('Share the invitation and your IP address with the other player.')

    def done_running(self):
        for widget in (self.start, self.role, self.code, self.miis): widget.setEnabled(True)
        self.role_changed(); self.stop.setText('Close')
        run = getattr(self.engine, 'run', None)
        self.keep.setEnabled(self.engine.role == 'host' and run is not None and (run / 'UserData/NAND' / DB).exists())

    def keep_miis(self):
        try:
            count = self.engine.keep_guest_miis()
            self.output.appendPlainText(f'Kept {count} guest Miis. Game progress was not imported.')
            self.keep.setEnabled(False)
        except Exception as error:
            QMessageBox.warning(self, 'Mii import', str(error))

    def finish_or_stop(self):
        if self.worker and self.worker.isRunning():
            self.engine.cancel(); self.stop.setText('Stopping…'); return
        self.accept()

    def reject(self):
        self.finish_or_stop()

    def closeEvent(self, event):
        if self.worker and self.worker.isRunning():
            self.finish_or_stop(); event.ignore()
        else:
            event.accept()
