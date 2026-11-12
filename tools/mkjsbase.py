#!/usr/bin/env python3
"""Gera userland/libsagui/js_base.h a partir de js_base.js."""
import os
pasta = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "userland", "libsagui")
with open(os.path.join(pasta, "js_base.js"), encoding="ascii") as f:
    linhas = f.read().splitlines()
with open(os.path.join(pasta, "js_base.h"), "w") as f:
    f.write("/* Gerado por tools/mkjsbase.py a partir de js_base.js. */\nstatic const char JS_BASE[] =\n")
    for l in linhas:
        f.write('"' + l.replace("\\", "\\\\").replace('"', '\\"') + '\\n"\n')
    f.write(";\n")
