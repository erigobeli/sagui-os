#!/usr/bin/env python3
"""Converte o arquivo de pixels crus do teste_pagina (largura, altura, 0x00RRGGBB) em PNG."""
import struct
import sys
from PIL import Image

with open(sys.argv[1], "rb") as f:
    d = f.read()
l, a = struct.unpack("<II", d[:8])
im = Image.frombuffer("RGBX", (l, a), d[8:], "raw", "BGRX", 0, 1).convert("RGB")
im.save(sys.argv[2])
