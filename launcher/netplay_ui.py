# SPDX-License-Identifier: GPL-3.0-only
import time
import uuid
import tomllib
from pathlib import Path
from PySide6.QtCore import QThread, Signal, Qt
from PySide6.QtNetwork import QAbstractSocket, QNetworkInterface
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
        self.network = QComboBox()
        self.refresh_network = QPushButton('Refresh')
        self.refresh_network.clicked.connect(self.load_networks)
        network_row = QHBoxLayout(); network_row.addWidget(self.network, 1)
        network_row.addWidget(self.refresh_network)
        self.network_label = QLabel('Host network')
        form.addRow(self.network_label, network_row)
        self.network_note = QLabel('Choose the ZeroTier device for internet play. Share its IP with your guest. '
                                  'The lobby stays open until you leave.')
        self.network_note.setWordWrap(True); form.addRow(self.network_note)
        self.load_networks()
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
        for widget in (self.network, self.refresh_network, self.network_label, self.network_note):
            widget.setVisible(not joining)

    def load_networks(self):
        previous = self.network.currentData()
        self.network.clear()
        entries = []
        for interface in QNetworkInterface.allInterfaces():
            if not interface.flags() & QNetworkInterface.InterfaceFlag.IsUp:
                continue
            for entry in interface.addressEntries():
                ip = entry.ip()
                if ip.protocol() != QAbstractSocket.NetworkLayerProtocol.IPv4Protocol:
                    continue
                name = interface.name()
                zerotier = name.startswith('zt') or 'zerotier' in interface.humanReadableName().lower()
                label = f'{"ZeroTier · " if zerotier else ""}{name} — {ip.toString()}'
                entries.append((not zerotier, ip.isLoopback(), label, ip.toString()))
        for _, _, label, ip in sorted(entries):
            self.network.addItem(label, ip)
        self.network.addItem('All networks — 0.0.0.0 (share your reachable IP)', '0.0.0.0')
        index = self.network.findData(previous)
        if index >= 0: self.network.setCurrentIndex(index)

    def begin(self):
        role = 'host' if self.role.currentIndex() == 0 else 'join'
        address = self.address.text().strip() if role == 'join' else self.network.currentData()
        self.host_address = address
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
        for widget in (self.start, self.role, self.address, self.port, self.code, self.miis,
                       self.network, self.refresh_network): widget.setEnabled(False)
        self.keep.setEnabled(False); self.stop.setText('Leave session'); self.worker.start()

    def show_invitation(self, code):
        self.code.setPlainText(code); self.copy.setEnabled(True)
        ip = self.host_address
        self.output.appendPlainText(f'Share this invitation and host IP {ip} with the other player.'
            if ip != '0.0.0.0' else 'Share this invitation and your ZeroTier or LAN IP (not 0.0.0.0).')

    def done_running(self):
        for widget in (self.start, self.role, self.code, self.miis, self.network,
                       self.refresh_network): widget.setEnabled(True)
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
