# SPDX-License-Identifier: GPL-3.0-only
"""Read Wii RFL artwork and build an editable 3D head preview.

Independent resource reader and OpenGL scene builder. Binary layouts and Wii
part-placement conventions are documented in THIRD-PARTY.md. No game assets
are shipped: all geometry/textures are read from the installed RFL_Res.dat.
"""
from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
import struct

import numpy as np
from PySide6.QtCore import QRectF
from PySide6.QtGui import QImage, QPainter, QTransform

from mii_data import FIELDS, read_field
from mii_resources import MAX_RESOURCE, validate_resource

SKIN = [(240,216,196),(255,188,128),(216,136,80),(255,176,144),(152,80,48),(82,46,28)]
HAIR = [(30,26,24),(56,32,21),(85,38,23),(112,64,36),(114,114,120),(73,54,26),(122,89,40),(193,159,100)]
GLASSES = [(16,16,16),(96,56,16),(152,24,16),(32,48,96),(144,88,0),(96,88,80)]
FAVORITE = [(184,64,48),(240,120,40),(248,216,32),(128,200,40),(0,116,40),(32,72,152),
            (64,160,216),(232,96,120),(112,44,168),(72,56,24),(224,224,224),(24,24,20)]
EYES = [(0,0,0),(124,128,128),(112,80,64),(112,110,64),(88,104,184),(72,128,104)]
LIPS = [(190,78,38),(216,48,40),(207,68,71)]
LIP_LINES = [(113,42,4),(120,21,16),(126,37,40)]
EYE_ROT = [29,28,28,28,29,28,28,28,29,28,28,28,28,29,29,28,28,28,29,29,28,29,28,29,
           29,28,29,28,28,29,28,28,28,29,29,29,28,28,29,29,29,28,28,29,29,29,29,29]
BROW_ROT = [26,26,27,25,26,25,26,25,28,25,26,24,27,27,26,26,25,25,26,26,27,26,25,27]


class Reader:
    def __init__(self, data):
        self.data, self.cursor = data, 0

    def take(self, size):
        if size < 0 or self.cursor + size > len(self.data):
            raise ValueError('Incomplete Mii model data.')
        result = self.data[self.cursor:self.cursor+size]; self.cursor += size
        return result

    def byte(self):
        return self.take(1)[0]

    def short(self):
        return struct.unpack('>H', self.take(2))[0]

    def array(self, components, divisor):
        count = self.short()
        if count > 1024:
            raise ValueError('Mii model has too many vertices.')
        return np.frombuffer(self.take(count*components*2), dtype='>i2').astype(np.float32).reshape(-1, components) / divisor


@dataclass
class Shape:
    vertices: np.ndarray  # expanded triangles: xyz, normal xyz, uv
    attachments: np.ndarray | None = None


@dataclass
class Part:
    vertices: np.ndarray
    color: tuple
    texture: np.ndarray | None = None
    mode: int = 0  # 0 solid, 1 skin artwork, 2 direct RGBA, 3 cap tint, 4 color*texture
    transparent: bool = False
    mirror: bool = False
    wrap_s: int = 0
    wrap_t: int = 0


class Artwork:
    def __init__(self, path: Path):
        if not path.is_file():
            raise ValueError('Set up face artwork in the launcher to enable the 3D preview.')
        if path.stat().st_size > MAX_RESOURCE:
            raise ValueError('Mii artwork is too large.')
        self.data = path.read_bytes(); validate_resource(self.data)
        self.offsets = struct.unpack_from('>18I', self.data, 4)

    def entry(self, archive, index):
        start = self.offsets[archive]
        end = self.offsets[archive+1] if archive < 17 else len(self.data)
        if start + 4 > end:
            raise ValueError('Invalid Mii archive header.')
        count = struct.unpack_from('>H', self.data, start)[0]
        if count > 512 or not 0 <= index < count:
            raise ValueError('Mii artwork does not contain the selected part.')
        base = start + 8 + 4*count
        if base > end:
            raise ValueError('Invalid Mii archive table.')
        begin, finish = struct.unpack_from('>II', self.data, start+4+4*index)
        if begin > finish or base+finish > end:
            raise ValueError('Invalid Mii archive entry.')
        return self.data[base+begin:base+finish]

    @lru_cache(maxsize=160)
    def shape(self, archive, index):
        reader = Reader(self.entry(archive, index))
        tag = reader.take(4)
        expected = {0:b'berd',3:b'face',5:b'frhd',6:b'glas',8:b'hair',9:b'mask',13:b'nose',14:b'nsln',16:b'cap_'}
        if tag != expected[archive]:
            raise ValueError('Mii shape header does not match its archive.')
        attachments = None
        if archive == 3:
            attachments = np.array(struct.unpack('>9f', reader.take(36)), dtype=np.float32).reshape(3,3)
            if not np.isfinite(attachments).all():
                raise ValueError('Mii model contains invalid attachment coordinates.')
        pos = reader.array(3, 256)
        if not len(pos):
            return Shape(np.empty((0,8),dtype=np.float32), attachments)
        normals = reader.array(3, 16384)
        textured = archive not in (0,5,8,13)
        uv = reader.array(2, 8192) if textured else None
        triangles = []
        for _ in range(reader.byte()):
            count, kind = reader.byte(), reader.byte() & 0xf8
            stride = 3 if textured else 2
            indices = np.frombuffer(reader.take(count*stride), dtype=np.uint8).reshape(count,stride)
            if np.any(indices[:,0] >= len(pos)) or np.any(indices[:,1] >= len(normals)):
                raise ValueError('Mii shape references an invalid vertex.')
            if textured and np.any(indices[:,2] >= len(uv)):
                raise ValueError('Mii shape references an invalid texture coordinate.')
            if kind == 0x90:  # GX triangles
                faces = [(i,i+1,i+2) for i in range(0,count-2,3)]
            elif kind == 0x98:  # GX strip
                faces = [(i+(i%2), i+(1-i%2), i+2) for i in range(count-2)]
            elif kind == 0xa0:  # GX fan
                faces = [(0,i,i+1) for i in range(1,count-1)]
            elif kind == 0x80:  # GX quads
                faces = [face for i in range(0,count-3,4) for face in ((i,i+1,i+2),(i,i+2,i+3))]
            else:
                raise ValueError('Unsupported primitive in Mii shape.')
            for face in faces:
                refs = indices[list(face)]
                tex = uv[refs[:,2]] if textured else np.zeros((3,2),dtype=np.float32)
                triangles.append(np.concatenate((pos[refs[:,0]], normals[refs[:,1]], tex), axis=1))
        vertices = np.concatenate(triangles).astype(np.float32) if triangles else np.empty((0,8),dtype=np.float32)
        if len(vertices):
            # Standardize front winding using the resource's outward normals.
            tri = vertices.reshape(-1,3,8)
            facing = np.sum(np.cross(tri[:,1,:3]-tri[:,0,:3],tri[:,2,:3]-tri[:,0,:3])*tri[:,:,3:6].mean(axis=1),axis=1)
            nonzero = facing[np.abs(facing)>1e-6]
            if len(nonzero) and float(np.median(nonzero)) < 0:
                vertices = tri[:,[0,2,1],:].reshape(-1,8).copy()
        return Shape(vertices, attachments)

    @lru_cache(maxsize=200)
    def texture(self, archive, index):
        data = self.entry(archive,index)
        if len(data) < 32:
            raise ValueError('Incomplete Mii texture.')
        fmt, _, width, height, wrap_s, wrap_t = struct.unpack_from('>BBHHBB',data)
        offset = struct.unpack_from('>I',data,28)[0]
        if not 1 <= width <= 512 or not 1 <= height <= 512 or offset < 32:
            raise ValueError('Invalid Mii texture dimensions.')
        tiles = {0:(8,8,32),1:(8,4,32),2:(8,4,32),3:(4,4,32),4:(4,4,32),5:(4,4,32),6:(4,4,64)}
        if fmt not in tiles:
            raise ValueError(f'Unsupported Wii texture format {fmt}.')
        tw,th,size = tiles[fmt]
        padded = np.zeros((((height+th-1)//th)*th, ((width+tw-1)//tw)*tw,4),dtype=np.uint8)
        cursor = offset
        for y in range(0,padded.shape[0],th):
            for x in range(0,padded.shape[1],tw):
                block = data[cursor:cursor+size]; cursor += size
                if len(block) != size:
                    raise ValueError('Incomplete Wii texture pixels.')
                raw = np.frombuffer(block,dtype=np.uint8)
                rgba = np.full((tw*th,4),255,dtype=np.uint8)
                if fmt == 0:
                    intensity = np.stack((raw>>4,raw&15),axis=1).reshape(-1)*17
                    rgba[:] = intensity[:,None]
                elif fmt == 1:
                    rgba[:] = raw[:,None]
                elif fmt == 2:
                    rgba[:,:3] = ((raw&15)*17)[:,None]; rgba[:,3] = (raw>>4)*17
                elif fmt in (3,4,5):
                    values = np.frombuffer(block,dtype='>u2').astype(np.uint32)
                    if fmt == 3:
                        rgba[:,:3] = (values&255)[:,None]; rgba[:,3] = values>>8
                    elif fmt == 4:
                        rgba[:,:3] = np.stack(((values>>11)*255//31, ((values>>5)&63)*255//63,(values&31)*255//31),axis=1)
                    else:
                        opaque = values&0x8000 != 0
                        rgb5 = np.stack((((values>>10)&31)*255//31,((values>>5)&31)*255//31,(values&31)*255//31),axis=1)
                        rgb4 = np.stack((((values>>8)&15)*17,((values>>4)&15)*17,(values&15)*17),axis=1)
                        rgba[:,:3] = np.where(opaque[:,None],rgb5,rgb4)
                        rgba[:,3] = np.where(opaque,255,((values>>12)&7)*255//7)
                else:
                    ar,gb = raw[:32].reshape(-1,2),raw[32:].reshape(-1,2)
                    rgba[:,0],rgba[:,1],rgba[:,2],rgba[:,3] = ar[:,1],gb[:,0],gb[:,1],ar[:,0]
                padded[y:y+th,x:x+tw] = rgba.reshape(th,tw,4)
        return padded[:height,:width].copy(),wrap_s,wrap_t

    def feature(self, archive, index, color, second=None, eye=False):
        pixels = self.texture(archive,index)[0].copy()
        if second is None:
            pixels[:,:,:3] = color
        else:
            channels = pixels[:,:,:3].astype(np.float32)/255
            if eye:
                rgb = channels[:,:,0,None]*np.array(color) + channels[:,:,2,None]*np.array(second) + channels[:,:,1,None]*255
            else:
                rgb = channels[:,:,0,None]*np.array(color) + channels[:,:,1,None]*np.array(second) + channels[:,:,2,None]*255
            pixels[:,:,:3] = np.clip(rgb,0,255).astype(np.uint8)
        h,w = pixels.shape[:2]
        return QImage(pixels.data,w,h,w*4,QImage.Format.Format_RGBA8888).copy()

    def mask(self, f):
        canvas = QImage(256,256,QImage.Format.Format_ARGB32_Premultiplied); canvas.fill(0)
        painter = QPainter(canvas); painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        def place(image,x,y,width,height,rotation=0,origin='center'):
            angle = np.deg2rad(rotation); co,si = float(np.cos(angle)),float(np.sin(angle))
            transform = QTransform(4*.88961464*co*width,4*.9276675*si*width,
                                   -4*.88961464*si*height,4*.9276675*co*height,4*x,4*y)
            painter.setTransform(transform)
            ox = -1 if origin == 'right' else 0 if origin == 'left' else -.5
            if origin == 'left':
                image = image.mirrored(True,False)
            painter.drawImage(QRectF(ox,-.5,1,1),image)
        try:
            mustache = self.feature(12,f['mustache'],HAIR[f['beard_color']])
            ms = 1+.4*f['beard_size']; my = 31.763554+1.1600001*.9276675*f['beard_y']
            for side in ('right','left'):
                place(mustache,32,my,4.5*ms,9*ms,origin=side)
            mouth = self.feature(11,f['mouth'],LIPS[f['mouth_color']],LIP_LINES[f['mouth_color']])
            ms = 1+.4*f['mouth_size']
            place(mouth,32,29.25885+1.1600001*.9276675*f['mouth_y'],6.1875*ms,4.5*ms)
            for kind,arc,base_y,base_w,offset in [('brow',2,16.549807,5.0625,BROW_ROT),('eye',1,18.451525,5.34375,EYE_ROT)]:
                if kind == 'eye':
                    base = (255,130,0) if f['eye']==9 else (0,255,255) if f['eye']==20 else (0,0,0)
                    image = self.feature(arc,f[kind],base,EYES[f['eye_color']],eye=True)
                else:
                    image = self.feature(arc,f[kind],HAIR[f['brow_color']])
                scale = 1+.4*f[kind+'_size']; dx=.88961464*f[kind+'_spacing']
                y=base_y+1.1600001*.9276675*f[kind+'_y']
                rotation=((f[kind+'_rotation']+offset[f[kind]])%32)*11.25
                place(image,32-dx,y,base_w*scale,4.5*scale,rotation,'right')
                place(image,32+dx,y,base_w*scale,4.5*scale,-rotation,'left')
            mole = self.feature(10,f['mole'],(18,15,15))
            scale=1+.4*f['mole_size']
            place(mole,17.766165+2*.88961464*f['mole_x'],17.95986+1.1600001*.9276675*f['mole_y'],scale,scale)
        finally:
            painter.end()
        straight = canvas.convertToFormat(QImage.Format.Format_RGBA8888)
        return np.frombuffer(straight.bits(),dtype=np.uint8).reshape(256,256,4).copy()

    def scene(self, record):
        f = {key:read_field(record,key) for key in FIELDS}
        skin,hair = SKIN[f['skin']],HAIR[f['hair_color']]
        face = self.shape(3,f['face'])
        if face.attachments is None:
            raise ValueError('Mii face has no part attachments.')
        nose,beard,hair_origin = face.attachments
        parts = []
        def add(arc,index,color,translation=None,scale=1,mirror=False,texture=None,mode=0,transparent=False):
            vertices = self.shape(arc,index).vertices.copy()
            if not len(vertices):
                return
            if mirror:
                vertices[:,0] *= -1; vertices[:,3] *= -1
            vertices[:,:3] *= scale
            if translation is not None:
                vertices[:,:3] += translation
            pixels,ws,wt = (self.texture(*texture) if isinstance(texture,tuple) else (texture,0,0))
            parts.append(Part(vertices,color,pixels,mode,transparent,mirror,ws,wt))
        add(0,f['beard'],HAIR[f['beard_color']],beard)
        nt = nose + np.array((0,-1.5*(f['nose_y']-8),0))
        add(13,f['nose'],skin,nt,.4+.175*f['nose_size'])
        add(5,f['hair'],skin,hair_origin,mirror=bool(f['hair_flip']))
        add(8,f['hair'],hair,hair_origin,mirror=bool(f['hair_flip']))
        add(16,f['hair'],FAVORITE[f['color']],hair_origin,mirror=bool(f['hair_flip']),texture=(17,f['hair']),mode=3)
        add(3,f['face'],skin,texture=(4,f['feature']),mode=1)
        add(9,f['face'],(255,255,255),texture=self.mask(f),mode=2,transparent=True)
        add(14,f['nose'],(0,0,0),nt,.4+.175*f['nose_size'],texture=(15,f['nose']),mode=4,transparent=True)
        if f['glasses']:
            gt = nose + np.array((0,5-1.5*(f['glasses_y']-11),2))
            add(6,0,GLASSES[f['glasses_color']],gt,.4+.15*f['glasses_size'],texture=(7,f['glasses']),mode=4,transparent=True)
        return parts
