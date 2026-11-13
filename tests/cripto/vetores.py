"""vetores.py - vetores de teste oficiais da criptografia (H50).

Só dados: cada bloco diz de que documento saiu. Os das RFCs foram extraídos
do texto das próprias RFCs por um script (para não haver erro de digitação);
os da FIPS 197, da SP 800-38A e do GCM foram digitados e conferidos. Tudo em
hexadecimal; cadeia vazia quer dizer "nenhum byte".
"""

# Mensagens dos exemplos da FIPS 180-4 ("abc", a vazia e as de 448 e 896 bits).
SHA = [
    {
        "tipo": "sha256",
        "dados": "616263",
        "resumo": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    },
    {
        "tipo": "sha256",
        "dados": "",
        "resumo": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    },
    {
        "tipo": "sha256",
        "dados": (
            "6162636462636465636465666465666765666768666768696768696a68696a6b"
            "696a6b6c6a6b6c6d6b6c6d6e6c6d6e6f6d6e6f706e6f7071"
        ),
        "resumo": "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
    },
    {
        "tipo": "sha384",
        "dados": "616263",
        "resumo": (
            "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
            "8086072ba1e7cc2358baeca134c825a7"
        ),
    },
    {
        "tipo": "sha384",
        "dados": "",
        "resumo": (
            "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da"
            "274edebfe76f65fbd51ad2f14898b95b"
        ),
    },
    {
        "tipo": "sha384",
        "dados": (
            "61626364656667686263646566676869636465666768696a6465666768696a6b"
            "65666768696a6b6c666768696a6b6c6d6768696a6b6c6d6e68696a6b6c6d6e6f"
            "696a6b6c6d6e6f706a6b6c6d6e6f70716b6c6d6e6f7071726c6d6e6f70717273"
            "6d6e6f70717273746e6f707172737475"
        ),
        "resumo": (
            "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712"
            "fcc7c71a557e2db966c3e9fa91746039"
        ),
    },
    {
        "tipo": "sha512",
        "dados": "616263",
        "resumo": (
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
        ),
    },
    {
        "tipo": "sha512",
        "dados": "",
        "resumo": (
            "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
            "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"
        ),
    },
    {
        "tipo": "sha512",
        "dados": (
            "61626364656667686263646566676869636465666768696a6465666768696a6b"
            "65666768696a6b6c666768696a6b6c6d6768696a6b6c6d6e68696a6b6c6d6e6f"
            "696a6b6c6d6e6f706a6b6c6d6e6f70716b6c6d6e6f7071726c6d6e6f70717273"
            "6d6e6f70717273746e6f707172737475"
        ),
        "resumo": (
            "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
            "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"
        ),
    },
]

# Um milhão de letras "a" (exemplo longo da FIPS 180-4).
SHA_MILHAO = {
    "sha256": "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
    "sha384": (
        "9d0e1809716474cb086e834e310a4a1ced149e9c00f248527972cec5704c2a5b"
        "07b8b3dc38ecc4ebae97ddd87f3d8985"
    ),
    "sha512": (
        "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
        "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b"
    ),
}

# RFC 4231, casos de teste 1 a 7 (o caso 5 traz a saída cortada em 128 bits).
RFC4231 = [
    {
        "chave": "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
        "dados": "4869205468657265",
        "sha256": "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
        "sha384": (
            "afd03944d84895626b0825f4ab46907f15f9dadbe4101ec682aa034c7cebc59c"
            "faea9ea9076ede7f4af152e8b2fa9cb6"
        ),
    },
    {
        "chave": "4a656665",
        "dados": "7768617420646f2079612077616e7420666f72206e6f7468696e673f",
        "sha256": "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
        "sha384": (
            "af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e"
            "8e2240ca5e69e2c78b3239ecfab21649"
        ),
    },
    {
        "chave": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "dados": (
            "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
            "dddddddddddddddddddddddddddddddddddd"
        ),
        "sha256": "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
        "sha384": (
            "88062608d3e6ad8a0aa2ace014c8a86f0aa635d947ac9febe83ef4e55966144b"
            "2a5ab39dc13814b94e3ab6e101a34f27"
        ),
    },
    {
        "chave": "0102030405060708090a0b0c0d0e0f10111213141516171819",
        "dados": (
            "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
            "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        ),
        "sha256": "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
        "sha384": (
            "3e8a69b7783c25851933ab6290af6ca77a9981480850009cc5577c6e1f573b4e"
            "6801dd23c4a7d679ccf8a386c674cffb"
        ),
    },
    {
        "chave": "0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c",
        "dados": "546573742057697468205472756e636174696f6e",
        "sha256": "a3b6167473100ee06e0c796c2955552b",
        "sha384": "3abf34c3503b2a23a46efc619baef897",
    },
    {
        "chave": (
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaa"
        ),
        "dados": (
            "54657374205573696e67204c6172676572205468616e20426c6f636b2d53697a"
            "65204b6579202d2048617368204b6579204669727374"
        ),
        "sha256": "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
        "sha384": (
            "4ece084485813e9088d2c63a041bc5b44f9ef1012a2b588f3cd11f05033ac4c6"
            "0c2ef6ab4030fe8296248df163f44952"
        ),
    },
    {
        "chave": (
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            "aaaaaa"
        ),
        "dados": (
            "5468697320697320612074657374207573696e672061206c6172676572207468"
            "616e20626c6f636b2d73697a65206b657920616e642061206c61726765722074"
            "68616e20626c6f636b2d73697a6520646174612e20546865206b6579206e6565"
            "647320746f20626520686173686564206265666f7265206265696e6720757365"
            "642062792074686520484d414320616c676f726974686d2e"
        ),
        "sha256": "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
        "sha384": (
            "6617178e941f020d351e2f254e8fd32c602420feb0b8fb9adccebb82461e99c5"
            "a678cc31e799176d3860e6110c46523e"
        ),
    },
]

# RFC 5869, apêndice A, casos 1 a 3 (os de SHA-256).
RFC5869 = [
    {
        "ikm": "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
        "sal": "000102030405060708090a0b0c",
        "info": "f0f1f2f3f4f5f6f7f8f9",
        "tam": 42,
        "prk": "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5",
        "okm": (
            "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
            "34007208d5b887185865"
        ),
    },
    {
        "ikm": (
            "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
            "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
            "404142434445464748494a4b4c4d4e4f"
        ),
        "sal": (
            "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f"
            "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
            "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
        ),
        "info": (
            "b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
            "d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeef"
            "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"
        ),
        "tam": 82,
        "prk": "06a6b88c5853361a06104c9ceb35b45cef760014904671014a193f40c15fc244",
        "okm": (
            "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c"
            "59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71"
            "cc30c58179ec3e87c14c01d5c1f3434f1d87"
        ),
    },
    {
        "ikm": "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
        "sal": "",
        "info": "",
        "tam": 42,
        "prk": "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04",
        "okm": (
            "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
            "9d201395faa4b61a96c8"
        ),
    },
]

# FIPS 197: apêndices C.1 (AES-128), C.3 (AES-256) e B (o exemplo passo a passo).
AES = [
    {
        "chave": "000102030405060708090a0b0c0d0e0f",
        "claro": "00112233445566778899aabbccddeeff",
        "cifrado": "69c4e0d86a7b0430d8cdb78070b4c55a",
    },
    {
        "chave": "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
        "claro": "00112233445566778899aabbccddeeff",
        "cifrado": "8ea2b7ca516745bfeafc49904b496089",
    },
    {
        "chave": "2b7e151628aed2a6abf7158809cf4f3c",
        "claro": "3243f6a8885a308d313198a2e0370734",
        "cifrado": "3925841d02dc09fbdc118597196a0b32",
    },
]

# NIST SP 800-38A, F.5.1 (CTR-AES128) e F.5.5 (CTR-AES256).
AES_CTR = [
    {
        "chave": "2b7e151628aed2a6abf7158809cf4f3c",
        "contador": "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
        "claro": (
            "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710"
        ),
        "cifrado": (
            "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff"
            "5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee"
        ),
    },
    {
        "chave": "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
        "contador": "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff",
        "claro": (
            "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710"
        ),
        "cifrado": (
            "601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5"
            "2b0930daa23de94ce87017ba2d84988ddfc9c58db67aada613c2dd08457941a6"
        ),
    },
]

# Casos de teste do apêndice B da especificação do GCM (McGrew e Viega), a que a
# SP 800-38D remete: "n" é o número do caso. O caso 5 usa um nonce de 8 bytes.
GCM = [
    {
        "n": 1,
        "chave": "00000000000000000000000000000000",
        "nonce": "000000000000000000000000",
        "extra": "",
        "claro": "",
        "cifrado": "",
        "marca": "58e2fccefa7e3061367f1d57a4e7455a",
    },
    {
        "n": 2,
        "chave": "00000000000000000000000000000000",
        "nonce": "000000000000000000000000",
        "extra": "",
        "claro": "00000000000000000000000000000000",
        "cifrado": "0388dace60b6a392f328c2b971b2fe78",
        "marca": "ab6e47d42cec13bdf53a67b21257bddf",
    },
    {
        "n": 3,
        "chave": "feffe9928665731c6d6a8f9467308308",
        "nonce": "cafebabefacedbaddecaf888",
        "extra": "",
        "claro": (
            "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
            "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255"
        ),
        "cifrado": (
            "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
            "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985"
        ),
        "marca": "4d5c2af327cd64a62cf35abd2ba6fab4",
    },
    {
        "n": 4,
        "chave": "feffe9928665731c6d6a8f9467308308",
        "nonce": "cafebabefacedbaddecaf888",
        "extra": "feedfacedeadbeeffeedfacedeadbeefabaddad2",
        "claro": (
            "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
            "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39"
        ),
        "cifrado": (
            "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
            "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091"
        ),
        "marca": "5bc94fbc3221a5db94fae95ae7121a47",
    },
    {
        "n": 5,
        "chave": "feffe9928665731c6d6a8f9467308308",
        "nonce": "cafebabefacedbad",
        "extra": "feedfacedeadbeeffeedfacedeadbeefabaddad2",
        "claro": (
            "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
            "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39"
        ),
        "cifrado": (
            "61353b4c2806934a777ff51fa22a4755699b2a714fcdc6f83766e5f97b6c7423"
            "73806900e49f24b22b097544d4896b424989b5e1ebac0f07c23f4598"
        ),
        "marca": "3612d2e79e3b0785561be14aaca2fccb",
    },
    {
        "n": 13,
        "chave": "0000000000000000000000000000000000000000000000000000000000000000",
        "nonce": "000000000000000000000000",
        "extra": "",
        "claro": "",
        "cifrado": "",
        "marca": "530f8afbc74536b9a963b4f1c4cb738b",
    },
    {
        "n": 14,
        "chave": "0000000000000000000000000000000000000000000000000000000000000000",
        "nonce": "000000000000000000000000",
        "extra": "",
        "claro": "00000000000000000000000000000000",
        "cifrado": "cea7403d4d606b6e074ec5d3baf39d18",
        "marca": "d0d1c8a799996bf0265b98b5d48ab919",
    },
    {
        "n": 15,
        "chave": "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
        "nonce": "cafebabefacedbaddecaf888",
        "extra": "",
        "claro": (
            "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
            "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255"
        ),
        "cifrado": (
            "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
            "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad"
        ),
        "marca": "b094dac5d93471bdec1a502270e3cc6c",
    },
    {
        "n": 16,
        "chave": "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
        "nonce": "cafebabefacedbaddecaf888",
        "extra": "feedfacedeadbeeffeedfacedeadbeefabaddad2",
        "claro": (
            "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
            "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39"
        ),
        "cifrado": (
            "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
            "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662"
        ),
        "marca": "76fc6ece0f4e1768cddf8853bb2d551b",
    },
]

# RFC 8439, seção 2.3.2.
CHACHA_BLOCO = {
    "chave": "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
    "nonce": "000000090000004a00000000",
    "contador": 1,
    "saida": (
        "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e"
        "d2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e"
    ),
}

# RFC 8439, seção 2.4.2.
CHACHA_CIFRA = {
    "chave": "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
    "nonce": "000000000000004a00000000",
    "contador": 1,
    "claro": (
        "4c616469657320616e642047656e746c656d656e206f662074686520636c6173"
        "73206f66202739393a204966204920636f756c64206f6666657220796f75206f"
        "6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73"
        "637265656e20776f756c642062652069742e"
    ),
    "cifrado": (
        "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
        "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8"
        "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
        "5af90bbf74a35be6b40b8eedf2785e42874d"
    ),
}

# RFC 8439: seção 2.5.2 e apêndice A.3 (vetores 1 a 11; os de 5 a 11 são os casos
# de borda da redução módulo 2^130 - 5).
POLY = [
    {
        "chave": "85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b",
        "dados": (
            "43727970746f6772617068696320466f72756d2052657365617263682047726f"
            "7570"
        ),
        "marca": "a8061dc1305136c6c22b8baf0c0127a9",
    },
    {
        "chave": "0000000000000000000000000000000000000000000000000000000000000000",
        "dados": (
            "0000000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000000000"
        ),
        "marca": "00000000000000000000000000000000",
    },
    {
        "chave": "0000000000000000000000000000000036e5f6b5c5e06070f0efca96227a863e",
        "dados": (
            "416e79207375626d697373696f6e20746f20746865204945544620696e74656e"
            "6465642062792074686520436f6e7472696275746f7220666f72207075626c69"
            "636174696f6e20617320616c6c206f722070617274206f6620616e2049455446"
            "20496e7465726e65742d4472616674206f722052464320616e6420616e792073"
            "746174656d656e74206d6164652077697468696e2074686520636f6e74657874"
            "206f6620616e204945544620616374697669747920697320636f6e7369646572"
            "656420616e20224945544620436f6e747269627574696f6e222e205375636820"
            "73746174656d656e747320696e636c756465206f72616c2073746174656d656e"
            "747320696e20494554462073657373696f6e732c2061732077656c6c20617320"
            "7772697474656e20616e6420656c656374726f6e696320636f6d6d756e696361"
            "74696f6e73206d61646520617420616e792074696d65206f7220706c6163652c"
            "207768696368206172652061646472657373656420746f"
        ),
        "marca": "36e5f6b5c5e06070f0efca96227a863e",
    },
    {
        "chave": "36e5f6b5c5e06070f0efca96227a863e00000000000000000000000000000000",
        "dados": (
            "416e79207375626d697373696f6e20746f20746865204945544620696e74656e"
            "6465642062792074686520436f6e7472696275746f7220666f72207075626c69"
            "636174696f6e20617320616c6c206f722070617274206f6620616e2049455446"
            "20496e7465726e65742d4472616674206f722052464320616e6420616e792073"
            "746174656d656e74206d6164652077697468696e2074686520636f6e74657874"
            "206f6620616e204945544620616374697669747920697320636f6e7369646572"
            "656420616e20224945544620436f6e747269627574696f6e222e205375636820"
            "73746174656d656e747320696e636c756465206f72616c2073746174656d656e"
            "747320696e20494554462073657373696f6e732c2061732077656c6c20617320"
            "7772697474656e20616e6420656c656374726f6e696320636f6d6d756e696361"
            "74696f6e73206d61646520617420616e792074696d65206f7220706c6163652c"
            "207768696368206172652061646472657373656420746f"
        ),
        "marca": "f3477e7cd95417af89a6b8794c310cf0",
    },
    {
        "chave": "1c9240a5eb55d38af333888604f6b5f0473917c1402b80099dca5cbc207075c0",
        "dados": (
            "2754776173206272696c6c69672c20616e642074686520736c6974687920746f"
            "7665730a446964206779726520616e642067696d626c6520696e207468652077"
            "6162653a0a416c6c206d696d737920776572652074686520626f726f676f7665"
            "732c0a416e6420746865206d6f6d65207261746873206f757467726162652e"
        ),
        "marca": "4541669a7eaaee61e708dc7cbcc5eb62",
    },
    {
        "chave": "0200000000000000000000000000000000000000000000000000000000000000",
        "dados": "ffffffffffffffffffffffffffffffff",
        "marca": "03000000000000000000000000000000",
    },
    {
        "chave": "02000000000000000000000000000000ffffffffffffffffffffffffffffffff",
        "dados": "02000000000000000000000000000000",
        "marca": "03000000000000000000000000000000",
    },
    {
        "chave": "0100000000000000000000000000000000000000000000000000000000000000",
        "dados": (
            "fffffffffffffffffffffffffffffffff0ffffffffffffffffffffffffffffff"
            "11000000000000000000000000000000"
        ),
        "marca": "05000000000000000000000000000000",
    },
    {
        "chave": "0100000000000000000000000000000000000000000000000000000000000000",
        "dados": (
            "fffffffffffffffffffffffffffffffffbfefefefefefefefefefefefefefefe"
            "01010101010101010101010101010101"
        ),
        "marca": "00000000000000000000000000000000",
    },
    {
        "chave": "0200000000000000000000000000000000000000000000000000000000000000",
        "dados": "fdffffffffffffffffffffffffffffff",
        "marca": "faffffffffffffffffffffffffffffff",
    },
    {
        "chave": "0100000000000000040000000000000000000000000000000000000000000000",
        "dados": (
            "e33594d7505e43b900000000000000003394d7505e4379cd0100000000000000"
            "0000000000000000000000000000000001000000000000000000000000000000"
        ),
        "marca": "14000000000000005500000000000000",
    },
    {
        "chave": "0100000000000000040000000000000000000000000000000000000000000000",
        "dados": (
            "e33594d7505e43b900000000000000003394d7505e4379cd0100000000000000"
            "00000000000000000000000000000000"
        ),
        "marca": "13000000000000000000000000000000",
    },
]

# RFC 8439: seção 2.8.2 (cifrar) e apêndice A.5 (decifrar).
AEAD = [
    {
        "chave": "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f",
        "nonce": "070000004041424344454647",
        "extra": "50515253c0c1c2c3c4c5c6c7",
        "claro": (
            "4c616469657320616e642047656e746c656d656e206f662074686520636c6173"
            "73206f66202739393a204966204920636f756c64206f6666657220796f75206f"
            "6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73"
            "637265656e20776f756c642062652069742e"
        ),
        "cifrado": (
            "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
            "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
            "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
            "3ff4def08e4b7a9de576d26586cec64b6116"
        ),
        "marca": "1ae10b594f09e26a7e902ecbd0600691",
    },
    {
        "chave": "1c9240a5eb55d38af333888604f6b5f0473917c1402b80099dca5cbc207075c0",
        "nonce": "000000000102030405060708",
        "extra": "f33388860000000000004e91",
        "claro": (
            "496e7465726e65742d4472616674732061726520647261667420646f63756d65"
            "6e74732076616c696420666f722061206d6178696d756d206f6620736978206d"
            "6f6e74687320616e64206d617920626520757064617465642c207265706c6163"
            "65642c206f72206f62736f6c65746564206279206f7468657220646f63756d65"
            "6e747320617420616e792074696d652e20497420697320696e617070726f7072"
            "6961746520746f2075736520496e7465726e65742d4472616674732061732072"
            "65666572656e6365206d6174657269616c206f7220746f206369746520746865"
            "6d206f74686572207468616e206173202fe2809c776f726b20696e2070726f67"
            "726573732e2fe2809d"
        ),
        "cifrado": (
            "64a0861575861af460f062c79be643bd5e805cfd345cf389f108670ac76c8cb2"
            "4c6cfc18755d43eea09ee94e382d26b0bdb7b73c321b0100d4f03b7f355894cf"
            "332f830e710b97ce98c8a84abd0b948114ad176e008d33bd60f982b1ff37c855"
            "9797a06ef4f0ef61c186324e2b3506383606907b6a7c02b0f9f6157b53c867e4"
            "b9166c767b804d46a59b5216cde7a4e99040c5a40433225ee282a1b0a06c523e"
            "af4534d7f83fa1155b0047718cbc546a0d072b04b3564eea1b422273f548271a"
            "0bb2316053fa76991955ebd63159434ecebb4e466dae5a1073a6727627097a10"
            "49e617d91d361094fa68f0ff77987130305beaba2eda04df997b714d6c6f2c29"
            "a6ad5cb4022b02709b"
        ),
        "marca": "eead9d67890cbb22392336fea1851f38",
    },
]

# RFC 7748, seção 5.2.
X25519 = [
    {
        "escalar": "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
        "u": "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
        "saida": "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552",
    },
    {
        "escalar": "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
        "u": "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493",
        "saida": "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957",
    },
]

# RFC 7748, seção 5.2: k = u = 9 e repetir (k, u) = (X25519(k, u), k).
X25519_LACO = {
    1: "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079",
    1000: "684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51",
    1000000: "7c3911e0ab2586fd864497297e575e6f3bc601c0883c30df5f4dd2d24f665424",
}

# RFC 7748, seção 6.1.
X25519_DH = {
    "a": "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
    "A": "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a",
    "b": "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",
    "B": "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f",
    "K": "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742",
}

# RFC 8032, seção 7.1: TEST 1, 2, 3, 1024 e SHA(abc).
RFC8032 = [
    {
        "nome": "TEST 1",
        "semente": "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
        "publica": "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        "mensagem": "",
        "assinatura": (
            "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
            "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
        ),
    },
    {
        "nome": "TEST 2",
        "semente": "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
        "publica": "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
        "mensagem": "72",
        "assinatura": (
            "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
            "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"
        ),
    },
    {
        "nome": "TEST 3",
        "semente": "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
        "publica": "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
        "mensagem": "af82",
        "assinatura": (
            "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
            "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"
        ),
    },
    {
        "nome": "TEST 1024",
        "semente": "f5e5767cf153319517630f226876b86c8160cc583bc013744c6bf255f5cc0ee5",
        "publica": "278117fc144c72340f67d0f2316e8386ceffbf2b2428c9c51fef7c597f1d426e",
        "mensagem": (
            "08b8b2b733424243760fe426a4b54908632110a66c2f6591eabd3345e3e4eb98"
            "fa6e264bf09efe12ee50f8f54e9f77b1e355f6c50544e23fb1433ddf73be84d8"
            "79de7c0046dc4996d9e773f4bc9efe5738829adb26c81b37c93a1b270b20329d"
            "658675fc6ea534e0810a4432826bf58c941efb65d57a338bbd2e26640f89ffbc"
            "1a858efcb8550ee3a5e1998bd177e93a7363c344fe6b199ee5d02e82d522c4fe"
            "ba15452f80288a821a579116ec6dad2b3b310da903401aa62100ab5d1a36553e"
            "06203b33890cc9b832f79ef80560ccb9a39ce767967ed628c6ad573cb116dbef"
            "efd75499da96bd68a8a97b928a8bbc103b6621fcde2beca1231d206be6cd9ec7"
            "aff6f6c94fcd7204ed3455c68c83f4a41da4af2b74ef5c53f1d8ac70bdcb7ed1"
            "85ce81bd84359d44254d95629e9855a94a7c1958d1f8ada5d0532ed8a5aa3fb2"
            "d17ba70eb6248e594e1a2297acbbb39d502f1a8c6eb6f1ce22b3de1a1f40cc24"
            "554119a831a9aad6079cad88425de6bde1a9187ebb6092cf67bf2b13fd65f270"
            "88d78b7e883c8759d2c4f5c65adb7553878ad575f9fad878e80a0c9ba63bcbcc"
            "2732e69485bbc9c90bfbd62481d9089beccf80cfe2df16a2cf65bd92dd597b07"
            "07e0917af48bbb75fed413d238f5555a7a569d80c3414a8d0859dc65a46128ba"
            "b27af87a71314f318c782b23ebfe808b82b0ce26401d2e22f04d83d1255dc51a"
            "ddd3b75a2b1ae0784504df543af8969be3ea7082ff7fc9888c144da2af58429e"
            "c96031dbcad3dad9af0dcbaaaf268cb8fcffead94f3c7ca495e056a9b47acdb7"
            "51fb73e666c6c655ade8297297d07ad1ba5e43f1bca32301651339e22904cc8c"
            "42f58c30c04aafdb038dda0847dd988dcda6f3bfd15c4b4c4525004aa06eeff8"
            "ca61783aacec57fb3d1f92b0fe2fd1a85f6724517b65e614ad6808d6f6ee34df"
            "f7310fdc82aebfd904b01e1dc54b2927094b2db68d6f903b68401adebf5a7e08"
            "d78ff4ef5d63653a65040cf9bfd4aca7984a74d37145986780fc0b16ac451649"
            "de6188a7dbdf191f64b5fc5e2ab47b57f7f7276cd419c17a3ca8e1b939ae49e4"
            "88acba6b965610b5480109c8b17b80e1b7b750dfc7598d5d5011fd2dcc5600a3"
            "2ef5b52a1ecc820e308aa342721aac0943bf6686b64b2579376504ccc493d97e"
            "6aed3fb0f9cd71a43dd497f01f17c0e2cb3797aa2a2f256656168e6c496afc5f"
            "b93246f6b1116398a346f1a641f3b041e989f7914f90cc2c7fff357876e506b5"
            "0d334ba77c225bc307ba537152f3f1610e4eafe595f6d9d90d11faa933a15ef1"
            "369546868a7f3a45a96768d40fd9d03412c091c6315cf4fde7cb68606937380d"
            "b2eaaa707b4c4185c32eddcdd306705e4dc1ffc872eeee475a64dfac86aba41c"
            "0618983f8741c5ef68d3a101e8a3b8cac60c905c15fc910840b94c00a0b9d0"
        ),
        "assinatura": (
            "0aab4c900501b3e24d7cdf4663326a3a87df5e4843b2cbdb67cbf6e460fec350"
            "aa5371b1508f9f4528ecea23c436d94b5e8fcd4f681e30a6ac00a9704a188a03"
        ),
    },
    {
        "nome": "TEST SHA(abc)",
        "semente": "833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
        "publica": "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
        "mensagem": (
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
        ),
        "assinatura": (
            "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b589"
            "09351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704"
        ),
    },
]
