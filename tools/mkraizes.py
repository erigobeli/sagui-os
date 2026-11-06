#!/usr/bin/env python3
"""Gera rootfs/etc/raizes.bin, as autoridades em que o Cipó confia.

Uso: mkraizes.py ca-certificates.crt saida.bin   (lista de CAs em PEM, da Mozilla)

Registro: tipo (1 RSA, 2 P-256, 3 P-384), 16 bytes do SHA-256 do nome (DER),
tamanho (2), chave, expoente (4). Chaves de outros tipos ficam de fora.
"""
import hashlib
import re
import sys

from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa


def registro(pem):
    c = x509.load_pem_x509_certificate(pem)
    k = c.public_key()
    nome = hashlib.sha256(c.subject.public_bytes()).digest()[:16]
    if isinstance(k, rsa.RSAPublicKey):
        n = k.public_numbers()
        if not 2048 <= k.key_size <= 4096:
            return b""
        chave = n.n.to_bytes((k.key_size + 7) // 8, "big")
        return bytes([1]) + nome + len(chave).to_bytes(2, "big") + chave + n.e.to_bytes(4, "big")
    if isinstance(k, ec.EllipticCurvePublicKey) and k.curve.name in ("secp256r1", "secp384r1"):
        chave = k.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        tipo = 2 if k.curve.name == "secp256r1" else 3
        return bytes([tipo]) + nome + len(chave).to_bytes(2, "big") + chave + bytes(4)
    return b""


def main():
    pems = re.findall(rb"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----\n?", open(sys.argv[1], "rb").read(), re.S)
    regs = [registro(p) for p in pems]
    open(sys.argv[2], "wb").write(b"".join(regs))
    print(f"{sum(1 for r in regs if r)} de {len(pems)} autoridades, {sum(map(len, regs))} bytes")


if __name__ == "__main__":
    main()
