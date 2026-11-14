#!/usr/bin/env python3
"""Gera o js_base.h (uma string em C) a partir do js_base.js do Cipó.

Veio do pull request #6 (Robson de Lima Barbosa); na 2.1 roda no build.
Uso: mkjsbase.py userland/navegador/js/js_base.js build/gerado/js_base.h"""
import sys

with open(sys.argv[1], encoding="ascii") as f:
    linhas = f.read().splitlines()
with open(sys.argv[2], "w") as f:
    f.write("/* Gerado por tools/mkjsbase.py a partir de js_base.js. */\nstatic const char JS_BASE[] =\n")
    for l in linhas:
        f.write('"' + l.replace("\\", "\\\\").replace('"', '\\"') + '\\n"\n')
    f.write(";\n")
