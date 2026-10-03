# SPDX-License-Identifier: GPL-3.0-only
"""Offline Qt Mii creator with Wheel Wizard feature picker artwork."""
from __future__ import annotations

from pathlib import Path
import binascii
from functools import lru_cache

from PySide6.QtCore import Qt, QSize
from PySide6.QtGui import QIcon, QPainter, QPixmap
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtWidgets import (QCheckBox, QComboBox, QDialog, QDialogButtonBox,
    QFileDialog, QFormLayout, QHBoxLayout, QLabel, QLineEdit,
    QListWidget, QListWidgetItem, QMessageBox, QPushButton, QScrollArea,
    QSpinBox, QTabWidget, QVBoxLayout, QWidget)

from mii_preview import MiiPreview

from mii_data import (Database, FIELDS, atomic_bytes, name_of, new_record,
                      read_field, set_name, validate_record, write_field)

ASSETS = Path(__file__).resolve().parent / 'assets/mii'
ICONS = {'face': 'MiiFace', 'hair': 'MiiHair', 'brow': 'MiiEyebrow',
         'eye': 'MiiEye', 'nose': 'MiiNose', 'mouth': 'MiiMouth',
         'glasses': 'MiiGlasses', 'mustache': 'MiiMustache', 'beard': 'MiiGoatee'}
HAIR_COLORS = ['Black', 'Brown', 'Red', 'Light red', 'Gray', 'Light brown', 'Blonde', 'Gold']
CHOICES = {
    'girl': ['Male', 'Female'],
    'color': ['Red', 'Orange', 'Yellow', 'Light green', 'Green', 'Blue', 'Light blue',
              'Pink', 'Purple', 'Brown', 'White', 'Black'],
    'skin': ['Light', 'Yellow', 'Red', 'Pink', 'Dark brown', 'Brown'],
    'feature': ['None', 'Cheeks', 'Cheeks and eyes', 'Freckles', 'Baggy eyes',
                'Strong features', 'Tired', 'Chin', 'Eye shadow', 'Stubble', 'Mouth corners', 'Wrinkles'],
    'hair_color': HAIR_COLORS, 'brow_color': HAIR_COLORS, 'beard_color': HAIR_COLORS,
    'eye_color': ['Black', 'Gray', 'Brown', 'Gold', 'Blue', 'Green'],
    'mouth_color': ['Skin', 'Red', 'Pink'],
    'glasses_color': ['Gray', 'Dark gold', 'Red', 'Blue', 'Gold', 'White'],
}


def plain(text):
    label = QLabel(text)
    label.setTextFormat(Qt.TextFormat.PlainText)
    label.setWordWrap(True)
    return label


def action(text, callback):
    result = QPushButton(text)
    result.clicked.connect(callback)
    return result


@lru_cache(maxsize=256)
def icon(key, value):
    renderer = QSvgRenderer(str(ASSETS / f'{ICONS[key]}{value:02d}.svg'))
    pixmap = QPixmap(104, 104); pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    renderer.render(painter); painter.end()
    return QIcon(pixmap)


class RecordEditor(QDialog):
    def __init__(self, record: bytes, resource: Path, parent=None):
        super().__init__(parent)
        self.setWindowTitle('Create your Mii' if name_of(record) == 'New Mii' else 'Edit your Mii')
        self.resize(1080, 740)
        self.original = bytes(record)
        self.record = bytearray(record)
        self.controls = {}
        self.result_record = None
        layout = QVBoxLayout(self)
        heading = plain('Make Mii at home!'); heading.setObjectName('pageTitle')
        layout.addWidget(heading)
        layout.addWidget(plain("Create your Mii's here! or import a preexisting Mii Save Data file! choice is yours."))
        content = QHBoxLayout(); layout.addLayout(content, 1)
        tabs = QTabWidget(); content.addWidget(tabs, 1)
        sections = {}
        for section in dict.fromkeys(field[0] for field in FIELDS.values()):
            area = QScrollArea(); area.setWidgetResizable(True)
            page = QWidget(); form = QFormLayout(page)
            form.setContentsMargins(16, 16, 16, 16); form.setSpacing(12)
            area.setWidget(page); tabs.addTab(area, section); sections[section] = form
        self.name = QLineEdit(name_of(record)); self.name.setMaxLength(10)
        self.creator = QLineEdit(name_of(record, 54)); self.creator.setMaxLength(10)
        sections['Profile'].addRow('Name', self.name)
        sections['Profile'].addRow('Creator (optional)', self.creator)
        for key, (section, label, _, _, _, _, low, high, _) in FIELDS.items():
            value = read_field(record, key)
            if key in ICONS:
                control = QListWidget()
                control.setViewMode(QListWidget.ViewMode.IconMode)
                control.setResizeMode(QListWidget.ResizeMode.Adjust)
                control.setMovement(QListWidget.Movement.Static)
                control.setIconSize(QSize(52, 52)); control.setGridSize(QSize(72, 80))
                control.setMinimumHeight(205)
                for n in range(low, high+1):
                    item = QListWidgetItem(icon(key, n), str(n+1))
                    item.setToolTip(f'{label} {n+1}')
                    control.addItem(item)
                control.setCurrentRow(value)
                control.currentRowChanged.connect(lambda n, k=key: self.change(k, n))
            elif key in CHOICES:
                control = QComboBox(); control.addItems(CHOICES[key]); control.setCurrentIndex(value)
                control.currentIndexChanged.connect(lambda n, k=key: self.change(k, n))
            elif high == 1:
                control = QCheckBox(); control.setChecked(bool(value))
                control.toggled.connect(lambda n, k=key: self.change(k, int(n)))
            else:
                control = QSpinBox(); control.setRange(low, high); control.setValue(value)
                control.valueChanged.connect(lambda n, k=key: self.change(k, n))
            self.controls[key] = control
            sections[section].addRow(label, control)
        sidebar = QWidget(); sidebar.setMinimumWidth(310); sidebar.setMaximumWidth(390)
        preview = QVBoxLayout(sidebar); content.addWidget(sidebar)
        self.title = plain(name_of(record)); self.title.setObjectName('cardTitle'); preview.addWidget(self.title)
        self.preview = MiiPreview(resource,self); preview.addWidget(self.preview,1)
        preview.addWidget(plain('Wii head preview · Updates as you edit'))
        preview.addWidget(plain('Mii format and feature pickers adapted from Team Wheel Wizard · GPL-3.0'))
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.save); buttons.rejected.connect(self.reject); layout.addWidget(buttons)
        self.name.textChanged.connect(lambda text: self.title.setText(text))
        self.refresh_preview()

    def change(self, key, value):
        if value < 0:
            return
        write_field(self.record, key, value)
        self.refresh_preview()

    def refresh_preview(self):
        if hasattr(self, 'preview'):
            self.preview.set_record(self.record)

    def save(self):
        try:
            set_name(self.record, self.name.text().strip())
            set_name(self.record, self.creator.text(), 54)
            validate_record(self.record)
        except ValueError as exc:
            QMessageBox.warning(self, 'Check your Mii', str(exc)); return
        self.result_record = bytes(self.record)
        self.accept()

    def reject(self):
        changed = (bytes(self.record) != self.original or self.name.text() != name_of(self.original)
                   or self.creator.text() != name_of(self.original, 54))
        if changed and QMessageBox.question(self, 'Discard changes?', 'Close without saving this Mii?',
                QMessageBox.StandardButton.Discard | QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Cancel) != QMessageBox.StandardButton.Discard:
            return
        super().reject()


class MiiManager(QDialog):
    def __init__(self, path: Path, parent=None):
        super().__init__(parent)
        self.database = Database(path)
        self.setWindowTitle('Your Miis · Riisorted'); self.resize(950, 630)
        layout = QVBoxLayout(self)
        heading = plain('Your Miis.'); heading.setObjectName('pageTitle'); layout.addWidget(heading)
        layout.addWidget(plain('Create a Mii here or bring one you already have. Changes are saved to both editions.'))
        self.resource = path.parent / 'RFL_Res.dat'
        collection = QHBoxLayout(); layout.addLayout(collection,1)
        self.list = QListWidget(); self.list.setIconSize(QSize(54, 54)); collection.addWidget(self.list,1)
        self.preview = MiiPreview(self.resource,self); collection.addWidget(self.preview,1)
        self.list.itemDoubleClicked.connect(lambda _: self.edit())
        create = QHBoxLayout()
        create.addWidget(action('Create male Mii', lambda: self.create(False)))
        create.addWidget(action('Create female Mii', lambda: self.create(True)))
        create.addWidget(action('Import Mii…', self.import_record)); layout.addLayout(create)
        edits = QHBoxLayout()
        self.edit_button = action('Edit', self.edit); edits.addWidget(self.edit_button)
        self.export_button = action('Export…', self.export); edits.addWidget(self.export_button)
        self.delete_button = action('Delete', self.delete); edits.addWidget(self.delete_button)
        layout.addLayout(edits)
        self.status = plain(''); layout.addWidget(self.status)
        layout.addWidget(plain('Existing databases are backed up before every save. Removing a Mii can affect game profiles linked to it.'))
        layout.addWidget(action('Done', self.accept))
        self.list.currentRowChanged.connect(self.selection_changed)
        self.refresh()

    def refresh(self):
        selected = self.list.currentRow()
        self.list.clear()
        for slot, record in self.database.records():
            item = QListWidgetItem(icon('hair', read_field(record, 'hair')), name_of(record) or f'Mii {slot+1}')
            item.setData(Qt.ItemDataRole.UserRole, slot)
            self.list.addItem(item)
        if self.list.count():
            self.list.setCurrentRow(max(0, min(selected, self.list.count()-1)))
        self.status.setText(f'{self.list.count()} / 100 Miis · Saved in your game installation')
        self.selection_changed()

    def selection_changed(self, *_):
        for control in (self.edit_button, self.export_button, self.delete_button):
            control.setEnabled(self.list.currentItem() is not None)
        if self.list.currentItem() is not None:
            self.preview.set_record(self.selected()[1])
        else:
            self.preview.view.parts = []; self.preview.view.dirty = True
            self.preview.view.message = 'Create a Mii to see it here.'
            self.preview.view.pending = None; self.preview.view.timer.stop(); self.preview.view.update()

    def selected(self):
        item = self.list.currentItem()
        if item is None:
            raise ValueError('Choose a Mii first.')
        slot = item.data(Qt.ItemDataRole.UserRole)
        return slot, bytes(self.database.data[4+slot*74:4+(slot+1)*74])

    def commit(self, record, slot=None):
        before = bytearray(self.database.data)
        try:
            self.database.put(record, slot)
            self.database.save()
        except (OSError, ValueError):
            self.database.data = before
            raise
        self.refresh()

    def create(self, girl):
        try:
            if len(self.database.records()) >= 100:
                raise ValueError('Your collection is full. Export or delete a Mii first.')
            editor = RecordEditor(new_record([r for _, r in self.database.records()], girl), self.resource, self)
            try:
                if editor.exec() == QDialog.DialogCode.Accepted:
                    self.commit(editor.result_record)
            finally:
                editor.deleteLater()
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, 'Could not save Mii', str(exc))

    def edit(self):
        try:
            slot, record = self.selected()
            validate_record(record)
            editor = RecordEditor(record, self.resource, self)
            try:
                if editor.exec() == QDialog.DialogCode.Accepted:
                    self.commit(editor.result_record, slot)
            finally:
                editor.deleteLater()
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, 'Could not edit Mii', str(exc))

    def import_record(self):
        path, _ = QFileDialog.getOpenFileName(self, 'Import a Wii Mii', '', 'Wii Miis (*.mii *.miigx *.rsd);;All files (*)')
        if not path:
            return
        try:
            if Path(path).stat().st_size not in (74, 76):
                raise ValueError('Choose a 74-byte Wii Mii or 76-byte RFL store record.')
            data = Path(path).read_bytes()
            if len(data) == 76 and binascii.crc_hqx(data, 0):
                raise ValueError('This Mii file has an invalid checksum.')
            self.commit(data[:74])
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, 'Could not import Mii', str(exc))

    def export(self):
        try:
            _, record = self.selected()
            path, _ = QFileDialog.getSaveFileName(self, 'Export Wii Mii', 'Mii.mii', 'Wii Mii (*.mii)')
            if path:
                atomic_bytes(Path(path), record)
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, 'Could not export Mii', str(exc))

    def delete(self):
        try:
            slot, record = self.selected()
            if QMessageBox.question(self, 'Delete Mii?', f'Delete {name_of(record)}? A database backup will be kept.',
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                    QMessageBox.StandardButton.No) != QMessageBox.StandardButton.Yes:
                return
            before = bytearray(self.database.data)
            try:
                self.database.data[4+slot*74:4+(slot+1)*74] = bytes(74)
                self.database.save()
            except (OSError, ValueError):
                self.database.data = before
                raise
            self.refresh()
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, 'Could not delete Mii', str(exc))
