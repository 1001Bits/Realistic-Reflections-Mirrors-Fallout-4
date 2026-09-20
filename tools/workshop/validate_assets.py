"""Independently verify xEdit-reopened workshop meshes against the authored manifest."""
import argparse
import json
import math
from pathlib import Path
import struct


def vector(text):
    return tuple(map(float, text.split()))


def verify(model, path):
    doc = json.loads(path.read_text(encoding='utf-8-sig'))
    blocks = {int(k.split()[0]): (k.split(' ', 1)[1], v) for k, v in doc.items() if k[0].isdigit()}
    assert len(blocks) == int(doc['NiHeader']['Num Blocks'])
    assert doc['NiHeader']['User Version 2'] == '130'
    if model['placement'] == 'Wall':
        assert blocks[0][1]['Collision Object'] != 'None', 'wall decoration root lacks placement collision'
        root_extras = [blocks[int(link.split()[0])] for link in blocks[0][1]['Extra Data List']]
        attachments = [block for kind, block in root_extras if kind == 'BSConnectPoint::Parents']
        assert len(attachments) == 1, 'wall decoration lacks its root attachment marker'
        points = attachments[0]['Connect Points']
        assert attachments[0]['Name'] == 'CPA' and len(points) == 1
        assert points[0]['Name'] == 'P-WS-Autoplace' and points[0]['Parent'] == ''
        assert vector(points[0]['Translation']) == (0., 0., 0.), 'attachment must sit on the mounting plane'
        assert vector(points[0]['Rotation']) == (0., 0., 0.) and float(points[0]['Scale']) == 1.
    shapes, physics, visited = [], [], set()

    def walk(index, origin=(0, 0, 0)):
        assert index not in visited, 'cyclic or multiply parented node'
        visited.add(index)
        kind, block = blocks[index]
        if kind == 'NiNode':
            assert all(abs(x) < 1e-7 for x in vector(block['Transform']['Rotation']))
            position = tuple(x+y for x, y in zip(origin, vector(block['Transform']['Translation'])))
            for child in block['Children']:
                walk(int(child.split()[0]), position)
            if block['Collision Object'] != 'None':
                collision = blocks[int(block['Collision Object'].split()[0])][1]
                assert int(collision['Target'].split()[0]) == index
                data = bytes.fromhex(blocks[int(collision['Data'].split()[0])][1]['Binary Data'])
                points = [struct.unpack_from('<3f', data, 0x350+16*j) for j in range(8)]
                radius = struct.unpack_from('<f', data, 0x314)[0]
                for j in range(6):
                    plane = struct.unpack_from('<4f', data, 0x3D0+16*j)
                    assert all(sum(p[k]*plane[k] for k in range(3))+plane[3] < 1e-6 for p in points)
                    assert sum(abs(sum(p[k]*plane[k] for k in range(3))+plane[3]) < 1e-6 for p in points) == 4
                minimum = tuple(min(p[k] for p in points)*69.99125+position[k]-radius*69.99125 for k in range(3))
                maximum = tuple(max(p[k] for p in points)*69.99125+position[k]+radius*69.99125 for k in range(3))
                expected = next(p for p in model['parts'] if p.get('collision_node', p['name']) == block['Name'])
                for k in range(3):
                    assert abs(minimum[k]-(expected['center'][k]-expected['half'][k])) < 0.001, (model['name'], k, minimum, expected)
                    assert abs(maximum[k]-(expected['center'][k]+expected['half'][k])) < 0.001, (model['name'], k, maximum, expected)
                body_center = struct.unpack_from('<3f', data, 0x2C0)
                assert all(abs(body_center[k]*69.99125+position[k]-expected['center'][k]) < 0.001 for k in range(3))
                assert struct.unpack_from('<4f', data, 0x2D0) == (0, 0, 0, 1)
                assert struct.unpack_from('<I', data, 0x1E8)[0] == 0
                assert struct.unpack_from('<I', data, 0x1F8)[0] == 0
                physics.append({'node': block['Name'], 'minimum': minimum, 'maximum': maximum})
        elif kind == 'BSTriShape':
            assert block['Skin'] == 'None' and block['Controller'] == 'None'
            vertices = [vector(v['Vertex']) for v in block['Vertex Data']]
            triangles = [tuple(map(int, t.split())) for t in block['Triangles']]
            assert len(vertices) == int(block['Num Vertices']) and len(triangles) == int(block['Num Triangles'])
            assert all(all(0 <= i < len(vertices) for i in t) for t in triangles)
            sphere = block['Bounding Sphere']
            center, radius = vector(sphere['Center']), float(sphere['Radius'])
            assert all(math.dist(v, center) <= radius+0.002 for v in vertices)
            for vertex in block['Vertex Data']:
                assert 'Bone Indices' not in vertex and 'Bone Weights' not in vertex
                assert all(math.isfinite(n) for n in vector(vertex['Vertex']))
            shader = blocks[int(block['Shader Property'].split()[0])][1]
            assert 'Skinned' not in shader['Shader Flags 1'] and shader['Name']
            world = [tuple(x+y for x, y in zip(v, origin)) for v in vertices]
            shapes.append({'name': block['Name'], 'vertices': world, 'triangles': triangles})
        else:
            raise AssertionError(kind)

    walk(0)
    assert len(physics) == (1 if model['placement'] == 'Wall' else 3)
    if model['placement'] == 'Wall':
        assert all(p['maximum'][1] <= 0.001 for p in physics), 'collision projects behind the mounting plane'
        assert abs(physics[0]['maximum'][1]) < 0.001, 'collision is not flush with the mounting plane'
    glass = next(s for s in shapes if 'Glass' in s['name'])
    distances = [abs(sum((v[k]-model['pane']['center'][k])*model['pane']['normal'][k] for k in range(3)))
                 for v in glass['vertices']]
    near = sum(x < 0.08 for x in distances)
    assert near >= 4, (model['name'], near, sorted(distances))
    vertices = [v for s in shapes for v in s['vertices']]
    for k in range(3):
        assert min(v[k] for v in vertices) >= model['bounds'][0][k]-.08
        assert max(v[k] for v in vertices) <= model['bounds'][1][k]+.08
    return {'name': model['name'], 'valid': True, 'optical_plane_vertices': near,
            'collision': physics, 'shapes': shapes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--converted', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    reports = [verify(model, args.converted/(model['name']+'.json')) for model in manifest['models']]
    args.report.write_text(json.dumps(reports, indent=2), encoding='utf-8', newline='\n')
    print(f'PASS: {len(reports)} meshes, {sum(len(r["collision"]) for r in reports)} static colliders, optical planes, indices, bounds and unskinned materials.')


if __name__ == '__main__':
    main()
