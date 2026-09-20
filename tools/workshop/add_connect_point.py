"""add_cpa.py <in.nif> <out.nif> [rootName]
Append a BSConnectPoint::Parents extra-data block ("CPA", one P-WS-Autoplace point at the origin, identity
rotation) to a Fallout 4 NIF and register it on the root NiNode, so the workshop treats the object as a
wall-mounted item exactly like the shipped workshop mirrors. The block is appended last so no existing block
reference changes; only the root node grows by one extra-data slot. Optionally renames the root node string."""
import struct, sys

src, dst = sys.argv[1], sys.argv[2]
root_name = sys.argv[3] if len(sys.argv) > 3 else None
b = open(src, 'rb').read()

# ---- header ----------------------------------------------------------------------------------------------------
off = b.index(b'\n') + 1
ver, = struct.unpack_from('<I', b, off); off += 4
endian = b[off]; off += 1
user_ver, = struct.unpack_from('<I', b, off); off += 4
num_blocks_off = off
num_blocks, = struct.unpack_from('<I', b, off); off += 4
bs_ver, = struct.unpack_from('<I', b, off); off += 4
export_off = off
for _ in range(4):                      # 3 export strings + max filepath (BS >= 130)
    n = b[off]; off += 1 + n
export_bytes = b[export_off:off]
num_types, = struct.unpack_from('<H', b, off); off += 2
types = []
for _ in range(num_types):
    n, = struct.unpack_from('<I', b, off); off += 4
    types.append(b[off:off + n]); off += n
type_idx = list(struct.unpack_from('<%dH' % num_blocks, b, off)); off += 2 * num_blocks
sizes = list(struct.unpack_from('<%dI' % num_blocks, b, off)); off += 4 * num_blocks
num_strings, = struct.unpack_from('<I', b, off); off += 4
max_len, = struct.unpack_from('<I', b, off); off += 4
strings = []
for _ in range(num_strings):
    n, = struct.unpack_from('<I', b, off); off += 4
    strings.append(b[off:off + n]); off += n
num_groups, = struct.unpack_from('<I', b, off); off += 4
groups = b[off:off + 4 * num_groups]; off += 4 * num_groups
blocks_start = off
blocks = []
for i in range(num_blocks):
    blocks.append(bytearray(b[off:off + sizes[i]])); off += sizes[i]
footer = b[off:]                        # NiFooter: root count + root block references (unchanged)
num_roots, = struct.unpack_from('<I', footer, 0)
assert len(footer) == 4 + 4 * num_roots, 'unexpected trailing data'

# ---- strings: CPA (+ optional root rename) --------------------------------------------------------------------------
if b'CPA' in strings:
    cpa_string = strings.index(b'CPA')
else:
    strings.append(b'CPA'); cpa_string = len(strings) - 1
root = blocks[0]
assert types[type_idx[0]] == b'NiNode', 'root is not a NiNode'
root_name_idx, root_extra = struct.unpack_from('<II', root, 0)
if root_name:
    strings[root_name_idx] = root_name.encode()

# ---- new block --------------------------------------------------------------------------------------------------------
if b'BSConnectPoint::Parents' in types:
    cpa_type = types.index(b'BSConnectPoint::Parents')
else:
    types.append(b'BSConnectPoint::Parents'); cpa_type = len(types) - 1
point_name = b'P-WS-Autoplace'
cpa = struct.pack('<II', cpa_string, 1)
cpa += struct.pack('<I', 0)                                     # parent: empty
cpa += struct.pack('<I', len(point_name)) + point_name          # name
cpa += struct.pack('<4f', 1.0, 0.0, 0.0, 0.0)                   # rotation quaternion (identity)
cpa += struct.pack('<3f', 0.0, 0.0, 0.0)                        # translation (origin = back plane)
cpa += struct.pack('<f', 1.0)                                   # scale
new_index = num_blocks
blocks.append(bytearray(cpa)); type_idx.append(cpa_type); sizes.append(len(cpa))

# ---- root node: append the extra-data reference --------------------------------------------------------------------
extras = list(struct.unpack_from('<%dI' % root_extra, root, 8))
assert new_index not in extras
new_root = bytearray(struct.pack('<II', root_name_idx, root_extra + 1) + struct.pack('<%dI' % (root_extra + 1), *(extras + [new_index])) + root[8 + 4 * root_extra:])
blocks[0] = new_root; sizes[0] = len(new_root)
num_blocks += 1

# ---- write ------------------------------------------------------------------------------------------------------------
out = bytearray(b[:num_blocks_off])
out += struct.pack('<I', num_blocks) + struct.pack('<I', bs_ver) + export_bytes
out += struct.pack('<H', len(types))
for t in types:
    out += struct.pack('<I', len(t)) + t
out += struct.pack('<%dH' % num_blocks, *type_idx)
out += struct.pack('<%dI' % num_blocks, *sizes)
out += struct.pack('<I', len(strings)) + struct.pack('<I', max(max_len, max(len(s) for s in strings)))
for s in strings:
    out += struct.pack('<I', len(s)) + s
out += struct.pack('<I', num_groups) + groups
for blk in blocks:
    out += blk
out += footer
open(dst, 'wb').write(out)
print(f'{dst}: blocks {num_blocks} (+CPA #{new_index}), root extras {extras + [new_index]}, {len(out)} bytes')
