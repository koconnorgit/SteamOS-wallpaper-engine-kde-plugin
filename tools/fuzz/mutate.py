"""Make damaged copies of a scene.pkg: truncations and byte flips aimed at the
header, the json/shader/model entries and the first bytes of each texture."""
import os, random, struct, sys
src, outdir, count, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
data = open(src, 'rb').read()
rng = random.Random(seed)
pos = 0
def u32():
    global pos
    v = struct.unpack_from('<I', data, pos)[0]; pos += 4; return v
n = u32(); pos += n
cnt = u32()
ents = []
for _ in range(cnt):
    n = u32(); name = data[pos:pos + n].decode('utf-8', 'replace'); pos += n
    off = u32(); ln = u32(); ents.append((name, off, ln))
header = pos
targets = [(0, header)]
for name, off, ln in ents:
    if ln == 0: continue
    targets.append((header + off, min(ln, 256) if name.endswith('.tex') else ln))
for i in range(count):
    d = os.path.join(outdir, f'{i:03d}'); os.makedirs(d, exist_ok=True)
    buf = bytearray(data)
    kind = i % 4
    if kind == 0:      # truncate
        buf = buf[:rng.choice([rng.randrange(1, header + 1), rng.randrange(header, len(buf))])]
    else:              # flips; more of them for the heavier kinds
        for _ in range({1: 3, 2: 30, 3: 300}[kind]):
            start, ln = rng.choice(targets)
            p = start + rng.randrange(ln)
            if kind == 3 and rng.random() < 0.3:      # plant extreme integers
                struct.pack_into('<I', buf, min(p, len(buf) - 4), rng.choice([0, 0xffffffff, 0x7fffffff, 0x80000000, 65536]))
            else:
                buf[p] = rng.randrange(256)
    open(os.path.join(d, 'scene.pkg'), 'wb').write(buf)
print(count, 'mutants of', src, 'entries', cnt)
