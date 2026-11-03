#!/usr/bin/env python3
"""
bmp2img.py — Converte BMPs (paleta 64 cores FabGL, gerados pelo mapper.html)
para o formato nativo .img do bootloader, sem perder um pixel.

Formato .img:
  bytes 0-3 : "FGIM"
  bytes 4-5 : width  (uint16 LE)
  bytes 6-7 : height (uint16 LE)
  bytes 8.. : width*height bytes, top->bottom, 1 byte/pixel = indice RGB222 (R | G<<2 | B<<4)

Uso:
  python3 bmp2img.py <pasta>            # converte todos os .bmp da pasta
  python3 bmp2img.py <pasta> -o <saida> # grava os .img noutra pasta
  python3 bmp2img.py arquivo.bmp        # converte um único arquivo
"""
import sys, os, struct, glob

# Níveis RGB222 do FabGL
LEVELS = [0, 85, 170, 255]

def nearest_level(v):
    # mapeia 0..255 para 0..3 (nível mais próximo)
    best, bd = 0, 999
    for i, L in enumerate(LEVELS):
        d = abs(v - L)
        if d < bd:
            bd, best = d, i
    return best

def rgb_to_idx(r, g, b):
    R = nearest_level(r)
    G = nearest_level(g)
    B = nearest_level(b)
    return R | (G << 2) | (B << 4)

def read_u16(b, o): return b[o] | (b[o+1] << 8)
def read_u32(b, o): return b[o] | (b[o+1] << 8) | (b[o+2] << 16) | (b[o+3] << 24)
def read_s32(b, o):
    v = read_u32(b, o)
    return v - (1 << 32) if v & 0x80000000 else v

def convert_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()

    if data[0:2] != b'BM':
        raise ValueError("não é um BMP (falta assinatura BM)")

    data_off   = read_u32(data, 10)
    dib_size   = read_u32(data, 14)
    w          = read_s32(data, 18)
    h          = read_s32(data, 22)
    bpp        = read_u16(data, 28)

    top_down = False
    if h < 0:
        h = -h
        top_down = True

    if bpp == 8:
        # paleta começa logo após o DIB header
        pal_off = 14 + dib_size
        # nº de cores: campo em offset 46; 0 => 256
        ncolors = read_u32(data, 46) or 256
        pal = []
        for i in range(ncolors):
            o = pal_off + i * 4
            b, g, r = data[o], data[o+1], data[o+2]   # BMP guarda B,G,R,0
            pal.append(rgb_to_idx(r, g, b))
        row_size = ((w + 3) // 4) * 4   # linhas alinhadas a 4 bytes
        out = bytearray(w * h)
        for y in range(h):
            src_row = y if top_down else (h - 1 - y)   # BMP é bottom-up
            base = data_off + src_row * row_size
            orow = y * w
            for x in range(w):
                out[orow + x] = pal[data[base + x]]
        return w, h, out

    elif bpp == 24:
        row_size = ((w * 3 + 3) // 4) * 4
        out = bytearray(w * h)
        for y in range(h):
            src_row = y if top_down else (h - 1 - y)
            base = data_off + src_row * row_size
            orow = y * w
            for x in range(w):
                o = base + x * 3
                b, g, r = data[o], data[o+1], data[o+2]
                out[orow + x] = rgb_to_idx(r, g, b)
        return w, h, out

    else:
        raise ValueError(f"bpp {bpp} não suportado (use 8 ou 24)")

def write_img(path, w, h, pixels):
    with open(path, 'wb') as f:
        f.write(b'FGIM')
        f.write(struct.pack('<HH', w, h))
        f.write(pixels)

def process(src, out_dir):
    w, h, pixels = convert_bmp(src)
    base = os.path.splitext(os.path.basename(src))[0]
    dst = os.path.join(out_dir, base + '.img')
    write_img(dst, w, h, pixels)
    print(f"  {os.path.basename(src)}  ->  {base}.img   ({w}x{h}, {len(pixels)} bytes)")

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    out_dir = None
    if '-o' in sys.argv:
        out_dir = sys.argv[sys.argv.index('-o') + 1]

    if not args:
        print(__doc__)
        sys.exit(1)

    target = args[0]

    if os.path.isfile(target):
        od = out_dir or os.path.dirname(target) or '.'
        os.makedirs(od, exist_ok=True)
        process(target, od)
        return

    if os.path.isdir(target):
        od = out_dir or target
        os.makedirs(od, exist_ok=True)
        bmps = sorted(glob.glob(os.path.join(target, '*.bmp')) +
                      glob.glob(os.path.join(target, '*.BMP')))
        if not bmps:
            print("Nenhum .bmp encontrado em", target)
            return
        print(f"Convertendo {len(bmps)} arquivo(s) de '{target}' para '{od}':")
        ok = 0
        for b in bmps:
            try:
                process(b, od)
                ok += 1
            except Exception as e:
                print(f"  FALHOU {os.path.basename(b)}: {e}")
        print(f"Pronto: {ok}/{len(bmps)} convertidos.")
        return

    print("Caminho não encontrado:", target)

if __name__ == '__main__':
    main()
