"""Cross-platform Qt frontend. No game assets or ROM contents are embedded."""
from pathlib import Path
import threading
from PySide6.QtCore import QObject, Signal, QTimer, Qt, QUrl
from PySide6.QtWidgets import (QApplication,QMainWindow,QWidget,QVBoxLayout,QHBoxLayout,QLabel,
    QFrame,QLineEdit,QPushButton,QFileDialog,QSpinBox,QCheckBox,QPlainTextEdit,QProgressBar,QGroupBox)
from install import Installer, launch_game

def local_drop_path(urls):
    if len(urls)!=1 or not urls[0].isLocalFile():return None
    return urls[0].toLocalFile()

class DropArea(QFrame):
    selected=Signal(str)
    browse=Signal()
    def __init__(self):
        super().__init__();self.setAcceptDrops(True);self.setObjectName('dropArea')
        layout=QVBoxLayout(self)
        label=QLabel('Drop your Mario Party 2 USA ROM here\n.z64, .v64 or .n64 — or click to browse')
        label.setAlignment(Qt.AlignmentFlag.AlignCenter);layout.addWidget(label);self.setMinimumHeight(110)
    def mousePressEvent(self,event):
        if event.button()==Qt.MouseButton.LeftButton:self.browse.emit()
    def dragEnterEvent(self,event):
        if local_drop_path(event.mimeData().urls()):event.acceptProposedAction()
    def dropEvent(self,event):
        path=local_drop_path(event.mimeData().urls())
        if path:self.selected.emit(path);event.acceptProposedAction()

class Bridge(QObject):
    log=Signal(str)
    status=Signal(str)
    finished=Signal(object,object)

class Window(QMainWindow):
    def __init__(self,args):
        super().__init__();self.args=args;self.installer=None;self.running=False;self.close_when_done=False
        self.setWindowTitle('Install Mario Party 2 Recompiled');self.resize(680,680)
        body=QWidget();self.setCentralWidget(body);layout=QVBoxLayout(body);layout.setContentsMargins(26,22,26,22);layout.setSpacing(12)
        title=QLabel('Mario Party 2 Recompiled');title.setObjectName('heading');layout.addWidget(title)
        layout.addWidget(QLabel('Bring your ROM. We’ll take care of the build tools.'))
        self.drop=DropArea();self.drop.selected.connect(lambda p:self.rom.setText(p));self.drop.browse.connect(self.choose_rom);layout.addWidget(self.drop)
        self.rom=QLineEdit(str(args.rom or ''));self.rom.setPlaceholderText('ROM file');layout.addWidget(self.rom)
        row=QHBoxLayout();row.addWidget(QLabel('Install to'));self.prefix=QLineEdit(str(args.prefix));row.addWidget(self.prefix)
        folder=QPushButton('Browse');folder.clicked.connect(self.choose_folder);row.addWidget(folder);layout.addLayout(row)
        info=QLabel('The installer prepares build tools and builds from your ROM locally.\nYour ROM stays on this computer. Saves are preserved when reinstalling.');info.setWordWrap(True);layout.addWidget(info)
        advanced=QGroupBox('Build options');advanced.setCheckable(True);advanced.setChecked(False);layout.addWidget(advanced)
        options=QVBoxLayout(advanced)
        self.cache=QLineEdit(str(args.build_dir or ''));self.cache.setPlaceholderText('Build directory (automatic cache by default)');options.addWidget(self.cache)
        self.pack=QLineEdit(str(args.dependency_pack or ''));self.pack.setPlaceholderText('Offline dependency pack (optional .tar.gz / .zip)');options.addWidget(self.pack)
        jobs=QHBoxLayout();jobs.addWidget(QLabel('Parallel build jobs'));self.jobs=QSpinBox();self.jobs.setRange(1,256);self.jobs.setValue(args.jobs);jobs.addWidget(self.jobs)
        self.desktop=QCheckBox('Create application shortcut');self.desktop.setChecked(not args.no_desktop);jobs.addWidget(self.desktop);options.addLayout(jobs)
        self.launch=QCheckBox('Launch when finished');self.launch.setChecked(True);layout.addWidget(self.launch)
        self.status=QLabel('Ready. Drop your USA ROM above.');self.status.setWordWrap(True);self.status.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse);layout.addWidget(self.status)
        self.progress=QProgressBar();self.progress.setRange(0,1);self.progress.setValue(0);layout.addWidget(self.progress)
        self.log=QPlainTextEdit();self.log.setReadOnly(True);self.log.setMaximumBlockCount(2000);self.log.setPlaceholderText('Installation progress will appear here.');self.log.setMinimumHeight(120);layout.addWidget(self.log,1)
        actions=QHBoxLayout();self.cancel=QPushButton('Cancel');self.cancel.setEnabled(False);self.cancel.clicked.connect(self.cancel_install);actions.addWidget(self.cancel);actions.addStretch()
        self.go=QPushButton('Go');self.go.setObjectName('go');self.go.setMinimumWidth(120);self.go.clicked.connect(self.start);actions.addWidget(self.go);layout.addLayout(actions)
        self.controls=[self.drop,self.rom,self.prefix,folder,advanced,self.launch]
        self.bridge=Bridge();self.bridge.log.connect(self.log.appendPlainText);self.bridge.status.connect(self.status.setText);self.bridge.finished.connect(self.finished)
        self.setStyleSheet('QMainWindow{background:#f5f7fb} QLabel#heading{font-size:25px;font-weight:700} QFrame#dropArea{background:#e8effb;border:2px dashed #6d8ebd;border-radius:12px} QPushButton{padding:7px 12px} QPushButton#go{background:#285bb5;color:white;font-weight:700;border-radius:6px} QLineEdit{padding:6px} QPlainTextEdit{background:#fff;border:1px solid #d4dce8}')
    def choose_rom(self):
        path,_=QFileDialog.getOpenFileName(self,'Choose your USA ROM','','Nintendo 64 ROM (*.z64 *.n64 *.v64);;All files (*)')
        if path:self.rom.setText(path)
    def choose_folder(self):
        path=QFileDialog.getExistingDirectory(self,'Choose install directory',self.prefix.text())
        if path:self.prefix.setText(path)
    def cancel_install(self):
        if self.installer:self.status.setText('Cancelling…');self.cancel.setEnabled(False);self.installer.cancel()
    def closeEvent(self,event):
        if self.running:self.close_when_done=True;self.cancel_install();event.ignore()
        else:event.accept()
    def start(self):
        if self.running:return
        rom=self.rom.text().strip();prefix=self.prefix.text().strip();cache=self.cache.text().strip();pack=self.pack.text().strip()
        if not rom or not prefix:self.status.setText('Select a ROM and install directory.');return
        jobs=self.jobs.value();desktop=self.desktop.isChecked()
        self.running=True;self.go.setEnabled(False);self.cancel.setEnabled(True);self.progress.setRange(0,0)
        for control in self.controls:control.setEnabled(False)
        self.installer=Installer(status=self.bridge.status.emit,log=self.bridge.log.emit)
        def work():
            launcher=None;error=None
            try:launcher=self.installer.install(rom,prefix,cache or None,jobs,desktop,dependency_pack=pack or None,sdk_dir=self.args.sdk_dir,source_build=self.args.source_build)
            except Exception as exc:error=str(exc)
            self.bridge.finished.emit(launcher,error)
        threading.Thread(target=work,daemon=True).start()
    def finished(self,launcher,error):
        self.running=False;self.cancel.setEnabled(False);self.go.setEnabled(True);self.progress.setRange(0,1);self.progress.setValue(0 if error else 1)
        for control in self.controls:control.setEnabled(True)
        if error:self.status.setText(error)
        else:
            self.status.setText('Installed. Ready to play.')
            if self.launch.isChecked() and not self.close_when_done:
                try:launch_game(launcher)
                except OSError as exc:self.status.setText('Installed, but could not launch: '+str(exc))
        if self.close_when_done:self.close()

def run_gui(args):
    app=QApplication.instance() or QApplication([]);window=Window(args);window.show()
    if getattr(args,'gui_smoke_test',False):QTimer.singleShot(800,window.close)
    return app.exec()
