# SPDX-License-Identifier: GPL-3.0-only
"""Interactive OpenGL preview of the installed Wii Mii head assets."""
from __future__ import annotations

import ctypes
from struct import error as struct_error
from pathlib import Path

import numpy as np
from OpenGL import GL
from OpenGL.GL.shaders import compileProgram, compileShader
from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtGui import QMatrix4x4, QPainter, QSurfaceFormat
from PySide6.QtOpenGLWidgets import QOpenGLWidget
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QVBoxLayout, QWidget

from mii_scene import Artwork

VERTEX = '''#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
uniform mat4 mvp;
uniform mat4 model;
out vec3 n;
out vec2 uv;
void main() {
    gl_Position = mvp * vec4(position,1.0);
    n = mat3(model) * normal;
    uv = texcoord;
}
'''
FRAGMENT = '''#version 330 core
in vec3 n;
in vec2 uv;
uniform sampler2D image;
uniform vec3 color;
uniform int mode;
out vec4 result;
void main() {
    vec4 t = texture(image,uv);
    vec3 rgb = color;
    float alpha = 1.0;
    if (mode == 1) rgb = mix(color,t.rgb,t.a);
    if (mode == 2) { rgb = t.rgb; alpha = t.a; }
    if (mode == 3) rgb = color * (t.r+1.0)*0.5;
    if (mode == 4) { rgb = color*t.rgb; alpha = t.a; }
    if (alpha < 0.01) discard;
    float diffuse = max(dot(normalize(n),normalize(vec3(-0.35,0.55,1.0))),0.0);
    float light = (mode == 2 || mode == 4) ? 1.0 : (0.72 + 0.28*diffuse);
    result = vec4(rgb*light,alpha);
}
'''


class HeadView(QOpenGLWidget):
    error = Signal(str)
    ready = Signal()

    def __init__(self, resource: Path, parent=None):
        super().__init__(parent)
        fmt = QSurfaceFormat(); fmt.setVersion(3,3)
        fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CoreProfile)
        fmt.setDepthBufferSize(24); fmt.setSamples(4)
        self.setFormat(fmt)
        self.setMinimumSize(260,320)
        self.setCursor(Qt.CursorShape.OpenHandCursor)
        self.program = None; self.gpu = []; self.parts = []
        self.dirty = False; self.message = ''; self.gl_error = ''; self.last_mouse = None
        self.yaw, self.pitch, self.zoom = 0.0,0.0,1.0
        self.center = np.zeros(3); self.radius = 50.0
        self.pending = None; self.artwork = None
        try:
            self.artwork = Artwork(resource)
        except (OSError, ValueError) as exc:
            self.message = str(exc)
        self.timer = QTimer(self); self.timer.setSingleShot(True); self.timer.setInterval(45)
        self.timer.timeout.connect(self.rebuild)

    def set_record(self, record):
        self.pending = bytes(record)
        self.timer.start()

    def rebuild(self):
        if self.artwork is None:
            self.error.emit(self.message); self.update(); return
        try:
            self.parts = self.artwork.scene(self.pending)
            if not self.parts:
                raise ValueError('No model parts found for this Mii.')
            points = np.concatenate([part.vertices[:,:3] for part in self.parts])
            low,high = points.min(axis=0),points.max(axis=0)
            self.center = (low+high)/2
            self.radius = max(1,float(np.linalg.norm(high-low)/2))
            self.dirty = True
            self.message = self.gl_error
            if self.gl_error:
                self.error.emit(self.gl_error)
            else:
                self.ready.emit()
        except (OSError, ValueError, IndexError, struct_error) as exc:
            self.parts = []; self.dirty = True
            self.message = f'Could not build this Mii preview: {exc}'
            self.error.emit(self.message)
        self.update()

    def initializeGL(self):
        self.context().aboutToBeDestroyed.connect(self.cleanup)
        try:
            self.program = compileProgram(compileShader(VERTEX,GL.GL_VERTEX_SHADER),
                                          compileShader(FRAGMENT,GL.GL_FRAGMENT_SHADER),validate=False)
            GL.glEnable(GL.GL_DEPTH_TEST); GL.glDepthFunc(GL.GL_LEQUAL)
            GL.glEnable(GL.GL_CULL_FACE); GL.glCullFace(GL.GL_BACK)
            GL.glEnable(GL.GL_BLEND); GL.glBlendFunc(GL.GL_SRC_ALPHA,GL.GL_ONE_MINUS_SRC_ALPHA)
            self.dirty = True
        except Exception as exc:
            self.message = self.gl_error = f'3D preview could not start: {exc}'
            self.error.emit(self.message)

    def delete_meshes(self):
        for vao,vbo,texture,*_ in self.gpu:
            GL.glDeleteVertexArrays(1,[vao]); GL.glDeleteBuffers(1,[vbo]); GL.glDeleteTextures([texture])
        self.gpu = []

    def upload(self):
        self.delete_meshes()
        for part in self.parts:
            vao = int(GL.glGenVertexArrays(1)); vbo = int(GL.glGenBuffers(1)); texture = int(GL.glGenTextures(1))
            # Register immediately so partial uploads can also be cleaned up.
            self.gpu.append((vao,vbo,texture,len(part.vertices),part))
            GL.glBindVertexArray(vao); GL.glBindBuffer(GL.GL_ARRAY_BUFFER,vbo)
            vertices = np.ascontiguousarray(part.vertices,dtype=np.float32)
            GL.glBufferData(GL.GL_ARRAY_BUFFER,vertices.nbytes,vertices,GL.GL_STATIC_DRAW)
            for index,size,offset in ((0,3,0),(1,3,12),(2,2,24)):
                GL.glEnableVertexAttribArray(index)
                GL.glVertexAttribPointer(index,size,GL.GL_FLOAT,False,32,ctypes.c_void_p(offset))
            GL.glBindTexture(GL.GL_TEXTURE_2D,texture)
            pixels = part.texture if part.texture is not None else np.full((1,1,4),255,dtype=np.uint8)
            pixels = np.ascontiguousarray(pixels)
            height,width = pixels.shape[:2]
            GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT,1)
            GL.glTexImage2D(GL.GL_TEXTURE_2D,0,GL.GL_RGBA8,width,height,0,GL.GL_RGBA,GL.GL_UNSIGNED_BYTE,pixels)
            GL.glTexParameteri(GL.GL_TEXTURE_2D,GL.GL_TEXTURE_MIN_FILTER,GL.GL_LINEAR)
            GL.glTexParameteri(GL.GL_TEXTURE_2D,GL.GL_TEXTURE_MAG_FILTER,GL.GL_LINEAR)
            wrap = {0:GL.GL_CLAMP_TO_EDGE,1:GL.GL_REPEAT,2:GL.GL_MIRRORED_REPEAT}
            GL.glTexParameteri(GL.GL_TEXTURE_2D,GL.GL_TEXTURE_WRAP_S,wrap.get(part.wrap_s,GL.GL_CLAMP_TO_EDGE))
            GL.glTexParameteri(GL.GL_TEXTURE_2D,GL.GL_TEXTURE_WRAP_T,wrap.get(part.wrap_t,GL.GL_CLAMP_TO_EDGE))
        GL.glBindVertexArray(0)
        self.dirty = False

    def paintGL(self):
        try:
            GL.glEnable(GL.GL_DEPTH_TEST); GL.glDepthFunc(GL.GL_LEQUAL)
            GL.glEnable(GL.GL_BLEND); GL.glBlendFunc(GL.GL_SRC_ALPHA,GL.GL_ONE_MINUS_SRC_ALPHA)
            GL.glClearColor(.91,.96,.94,1)
            GL.glDepthMask(True); GL.glClear(GL.GL_COLOR_BUFFER_BIT|GL.GL_DEPTH_BUFFER_BIT)
            if not self.program:
                self.paint_message(); return
            if self.dirty:
                self.upload()
            if not self.gpu or self.message:
                self.paint_message(); return
            model = QMatrix4x4()
            model.rotate(self.yaw,0,1,0); model.rotate(self.pitch,1,0,0)
            model.translate(-float(self.center[0]),-float(self.center[1]),-float(self.center[2]))
            view = QMatrix4x4(); view.translate(0,0,-self.radius*4)
            projection = QMatrix4x4()
            aspect = max(.01,self.width()/max(1,self.height()))
            half = self.radius*1.05/max(.4,self.zoom)*max(1,1/aspect)
            projection.ortho(-half*aspect,half*aspect,-half,half,.1,self.radius*10)
            GL.glUseProgram(self.program)
            self.matrix('model',model); self.matrix('mvp',projection*view*model)
            GL.glActiveTexture(GL.GL_TEXTURE0)
            GL.glUniform1i(GL.glGetUniformLocation(self.program,'image'),0)
            for vao,_,texture,count,part in self.gpu:
                GL.glDepthMask(not part.transparent)
                GL.glFrontFace(GL.GL_CW if part.mirror else GL.GL_CCW)
                if part.transparent:
                    GL.glDisable(GL.GL_CULL_FACE)
                else:
                    GL.glEnable(GL.GL_CULL_FACE)
                GL.glUniform3f(GL.glGetUniformLocation(self.program,'color'),*(c/255 for c in part.color))
                GL.glUniform1i(GL.glGetUniformLocation(self.program,'mode'),part.mode)
                GL.glBindTexture(GL.GL_TEXTURE_2D,texture); GL.glBindVertexArray(vao)
                GL.glDrawArrays(GL.GL_TRIANGLES,0,count)
            GL.glDepthMask(True); GL.glBindVertexArray(0); GL.glUseProgram(0)
        except Exception as exc:
            self.message = self.gl_error = f'3D preview failed: {exc}'
            self.error.emit(self.message)
            # Prevent a broken GL upload from being retried every frame.
            self.parts = []; self.dirty = False
            self.paint_message()

    def matrix(self,name,value):
        # Qt exports column-major matrices; pass directly with transpose=False.
        array = np.array(value.data(),dtype=np.float32)
        GL.glUniformMatrix4fv(GL.glGetUniformLocation(self.program,name),1,False,array)

    def paint_message(self):
        painter = QPainter(self)
        painter.setPen(Qt.GlobalColor.darkGray)
        painter.drawText(self.rect().adjusted(16,16,-16,-16),Qt.AlignmentFlag.AlignCenter|Qt.TextFlag.TextWordWrap,
                         self.message or 'Preparing your Mii…')
        painter.end()

    def reset_camera(self):
        self.yaw,self.pitch,self.zoom = 0.0,0.0,1.0; self.update()

    def mousePressEvent(self,event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.last_mouse = event.position(); self.setCursor(Qt.CursorShape.ClosedHandCursor)
            event.accept()

    def mouseMoveEvent(self,event):
        if self.last_mouse is not None:
            delta = event.position()-self.last_mouse; self.last_mouse = event.position()
            self.yaw = (self.yaw+delta.x()*.6)%360
            self.pitch = max(-70,min(70,self.pitch+delta.y()*.6)); self.update(); event.accept()

    def mouseReleaseEvent(self,event):
        self.last_mouse = None; self.setCursor(Qt.CursorShape.OpenHandCursor); event.accept()

    def wheelEvent(self,event):
        self.zoom = max(.4,min(2.5,self.zoom*1.12**(event.angleDelta().y()/120)))
        self.update(); event.accept()

    def cleanup(self):
        self.timer.stop()
        if not self.context() or not self.isValid():
            self.gpu = []; self.program = None; self.dirty = True
            return
        self.makeCurrent()
        try:
            self.delete_meshes()
            if self.program:
                GL.glDeleteProgram(self.program); self.program = None
        except Exception:
            # A lost context has already reclaimed its GPU objects.
            self.gpu = []; self.program = None
        finally:
            self.doneCurrent()


class MiiPreview(QWidget):
    def __init__(self, resource: Path, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self); layout.setContentsMargins(0,0,0,0)
        self.view = HeadView(resource,self); layout.addWidget(self.view,1)
        self.status = QLabel('Drag to rotate · Scroll to zoom'); self.status.setWordWrap(True)
        self.status.setTextFormat(Qt.TextFormat.PlainText)
        layout.addWidget(self.status)
        self.view.error.connect(self.status.setText)
        self.view.ready.connect(lambda: self.status.setText('Drag to rotate · Scroll to zoom'))
        row = QHBoxLayout()
        reset = QPushButton('Front view'); reset.clicked.connect(self.view.reset_camera); row.addWidget(reset)
        left = QPushButton('↶'); left.setToolTip('Turn left'); left.clicked.connect(lambda:self.turn(-30)); row.addWidget(left)
        right = QPushButton('↷'); right.setToolTip('Turn right'); right.clicked.connect(lambda:self.turn(30)); row.addWidget(right)
        layout.addLayout(row)

    def turn(self,angle):
        self.view.yaw = (self.view.yaw+angle)%360; self.view.update()

    def set_record(self,record):
        self.view.set_record(record)
