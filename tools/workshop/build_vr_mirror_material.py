"""Remove the original bathroom panes' baked environment map in the VR package only.

The large pane receives the live overlay; small panes retain ordinary diffuse/
specular shading without a fake reflection. No meshes or sink/cabinet materials
are replaced. Input is the version-2 BGSM from the user's own game archive.
Layout reference: ousnius/Material-Editor, MaterialLib/BaseMaterialFile.cs.
"""
import argparse
from pathlib import Path
import struct


def neutral_pane(source: bytes) -> bytes:
    if len(source) < 64 or source[:4] != b'BGSM' or struct.unpack_from('<I', source, 4)[0] != 2:
        raise ValueError('Expected Fallout 4 version-2 BGSM')
    # Read all nine texture strings, keeping every other material field verbatim.
    cursor = 63
    textures = []
    for _ in range(9):
        size = struct.unpack_from('<I', source, cursor)[0]
        cursor += 4
        if size < 1 or size > len(source)-cursor or source[cursor+size-1] != 0:
            raise ValueError('Malformed BGSM texture string')
        textures.append(source[cursor:cursor+size])
        cursor += size
    if not textures[0].lower().startswith(b'setdressing/playerhouse/playerhouse_bathroommirror01_'):
        raise ValueError('Refusing to replace an unrelated material')
    header = bytearray(source[:63])
    header[45] = header[46] = header[57] = 0  # SSR, wet SSR, environment mapping
    struct.pack_into('<f', header, 58, 0.0)  # environment intensity
    textures[4] = b'\0'  # no baked environment map resource, even if a shader requests it
    return bytes(header) + b''.join(struct.pack('<I', len(s)) + s for s in textures) + source[cursor:]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    result = neutral_pane(args.source.read_bytes())
    args.destination.parent.mkdir(parents=True, exist_ok=True)
    args.destination.write_bytes(result)
    print(f'VR bathroom pane without baked reflection: {args.destination} ({len(result)} bytes)')
