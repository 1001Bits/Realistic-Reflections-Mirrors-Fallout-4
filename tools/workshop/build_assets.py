"""Author standalone workshop mirrors from xEdit JSON exports of the installed vanilla meshes.

Run with --reference pointing to the six source exports and --output to an isolated staging
directory. Convert the resulting JSON with ConvertWorkshopNifs.pas before packaging.
Only model fragments and vanilla material paths are reused; no fixture collision is retained.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import struct


UNIT_SCALE = 69.99125
PLUGIN = 'Realistic Reflections - Mirrors.esm'


def vec(value):
    return tuple(map(float, value.split()))


def fmt(value):
    return ' '.join(f'{x:.9f}' for x in value)


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def normalized(a):
    length = math.sqrt(dot(a, a))
    assert length > 1e-9
    return tuple(x/length for x in a)


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def bounds(vertices):
    points = [vec(v['Vertex']) for v in vertices]
    return tuple(min(p[i] for p in points) for i in range(3)), tuple(max(p[i] for p in points) for i in range(3))


def components(shape):
    adjacent = [set() for _ in shape['Vertex Data']]
    for triangle in shape['Triangles']:
        indices = list(map(int, triangle.split()))
        for i in indices:
            adjacent[i].update(indices)
    seen = set()
    result = []
    for start in range(len(adjacent)):
        if start in seen:
            continue
        group, todo = set(), [start]
        while todo:
            i = todo.pop()
            if i not in group:
                group.add(i)
                todo.extend(adjacent[i]-group)
        seen.update(group)
        result.append(group)
    return result


def fragment(shape, indices, axes, center):
    shape = copy.deepcopy(shape)
    index_map = {old: new for new, old in enumerate(sorted(indices))}
    shape['Vertex Data'] = [shape['Vertex Data'][i] for i in sorted(indices)]
    triangles = [tuple(map(int, t.split())) for t in shape['Triangles']]
    assert all(not any(i in indices for i in t) or all(i in indices for i in t) for t in triangles), 'cut triangle'
    shape['Triangles'] = [' '.join(str(index_map[i]) for i in t) for t in triangles if all(i in indices for i in t)]
    for vertex in shape['Vertex Data']:
        position = sub(vec(vertex['Vertex']), center)
        vertex['Vertex'] = fmt(tuple(dot(axis, position) for axis in axes))
        for name in ('Normal', 'Tangent'):
            direction = vec(vertex[name])
            vertex[name] = fmt(normalized(tuple(dot(axis, direction) for axis in axes)))
        bitangent = tuple(float(vertex['Bitangent '+axis]) for axis in 'XYZ')
        rotated = normalized(tuple(dot(axis, bitangent) for axis in axes))
        for axis, value in zip('XYZ', rotated):
            vertex['Bitangent '+axis] = f'{value:.9f}'
        vertex.pop('Bone Weights', None)
        vertex.pop('Bone Indices', None)
    shape['Skin'] = 'None'
    shape['Controller'] = 'None'
    shape['Extra Data List'] = []
    shape['Alpha Property'] = 'None'
    return shape


def packed_vector(values):
    # hkPackedVector3: signed 16-bit xyz in the high half of int32, multiplied
    # by a float32 power-of-two whose high half is stored in the fourth word.
    maximum = max(map(abs, values))
    quantum = 2.0**math.ceil(math.log2(maximum/32766)) if maximum else 2.0**-24
    exponent = struct.unpack('<I', struct.pack('<f', quantum/65536))[0]
    assert exponent & 0xFFFF == 0
    ints = [round(v/quantum) for v in values]
    assert all(-32767 <= i <= 32767 for i in ints)
    return struct.pack('<3hH', *ints, exponent >> 16)


def box_physics(donor, dimensions):
    """Resize a verified static, centered, eight-vertex Havok polytope.

    Preserve topology, local/global/virtual relocations, material and static-body flags.
    Recompute convex radius, planes, center of mass, inertia, mass and volume. Each
    collider is attached to its own translated NiNode, so BodyCInfo stays at identity.
    """
    data = bytearray(donor)
    assert len(data) == 1376 and data[:8] == bytes.fromhex('57e0e05710c0c010')
    assert data[0x28:0x35] == b'hk_2014.1.0-r'
    assert struct.unpack_from('<I', data, 0xD4)[0] == 0x1C0
    assert struct.unpack_from('<4H', data, 0x340) == (8, 0x90, 6, 0x10C)
    assert struct.unpack_from('<I', data, 0x1E8)[0] == 0  # no dynamic motions
    assert struct.unpack_from('<I', data, 0x1F8)[0] == 0  # no dynamic inertias
    half = tuple(d/(2*UNIT_SCALE) for d in dimensions)
    radius = min(0.25/UNIT_SCALE, min(half)*0.2)
    core = tuple(h-radius for h in half)
    struct.pack_into('<f', data, 0x314, radius)
    original_points = [struct.unpack_from('<3f', donor, 0x350+16*i) for i in range(8)]
    original_center = tuple((min(p[a] for p in original_points)+max(p[a] for p in original_points))/2 for a in range(3))
    for i in range(8):
        original = sub(original_points[i], original_center)
        struct.pack_into('<3f', data, 0x350+16*i, *(math.copysign(h, p) for h, p in zip(core, original)))
        assert struct.unpack_from('<I', data, 0x35C+16*i)[0] == 0x3F000000+i
    for i in range(6):
        normal = struct.unpack_from('<3f', data, 0x3D0+16*i)
        axis = max(range(3), key=lambda a: abs(normal[a]))
        struct.pack_into('<f', data, 0x3DC+16*i, -core[axis])
    struct.pack_into('<4f', data, 0x2C0, 0, 0, 0, 0)
    struct.pack_into('<4f', data, 0x2D0, 0, 0, 0, 1)
    volume = 8*half[0]*half[1]*half[2]
    mass = volume  # density one, matching the donor's static shape metadata
    inertia = tuple(mass/3*sum(half[j]**2 for j in range(3) if j != i) for i in range(3))
    data[0x4C0:0x4C8] = packed_vector((0, 0, 0))
    data[0x4C8:0x4D0] = packed_vector(inertia)
    assert data[0x4D0:0x4D8] == bytes.fromhex('00800080008030f5')  # identity principal axes
    struct.pack_into('<2f', data, 0x4D8, mass, volume)
    assert data[0x4E0:] == donor[0x4E0:], 'relocations changed'
    return bytes(data)


def translate_box_physics(physics, offset):
    """Bake a box into its root's local space, matching vanilla wall-decoration collision.

    For an identity body rotation, BodyCInfo.position is COM metadata, not a second
    vertex translation. Move the vertices, plane distances and both COM records once.
    """
    data = bytearray(physics)
    delta = tuple(v/UNIT_SCALE for v in offset)
    for i in range(8):
        point = struct.unpack_from('<3f', data, 0x350+16*i)
        struct.pack_into('<3f', data, 0x350+16*i, *(p+d for p, d in zip(point, delta)))
    for i in range(6):
        plane = struct.unpack_from('<4f', data, 0x3D0+16*i)
        struct.pack_into('<f', data, 0x3DC+16*i, plane[3]-dot(plane[:3], delta))
    struct.pack_into('<4f', data, 0x2C0, *delta, 0.)
    data[0x4C0:0x4C8] = packed_vector(delta)
    return bytes(data)


class Model:
    def __init__(self, donor, name, root_collision=False):
        self.donor = donor
        self.name = name
        self.blocks = []
        self.parts = []
        self.root_collision = root_collision
        self.root = self.add('NiNode', self.node(name, (0, 0, 0)))
        flags = self.add('BSXFlags', {'Name': 'BSX', 'Flags': 'Havok | Articulated'})
        self.blocks[0][1]['Extra Data List'] = [flags]
        if root_collision:
            # BasketballHoop01NoPole and PictureFrame01 both expose this root
            # attachment point. Root collision alone leaves workshop placement
            # using its floor/drop path, with half of our centered pane buried.
            attachment = self.add('BSConnectPoint::Parents', {
                'Name': 'CPA', 'Connect Points': [{
                    'Parent': '', 'Name': 'P-WS-Autoplace',
                    'Rotation': '0 0 0', 'Translation': '0 0 0', 'Scale': '1'}]})
            self.blocks[0][1]['Extra Data List'].append(attachment)

    @staticmethod
    def node(name, translation):
        return {'Name': name, 'Extra Data List': [], 'Controller': 'None', 'Flags': '14',
                'Transform': {'Translation': fmt(translation), 'Rotation': '0 0 0', 'Scale': '1'},
                'Collision Object': 'None', 'Children': []}

    def add(self, kind, block):
        index = len(self.blocks)
        self.blocks.append((kind, block))
        return f'{index} {kind}'

    def part(self, name, shapes, source, shader_indices, translation):
        """Input vertices are in root coordinates, before translation; center them in a child node."""
        all_vertices = [v for shape in shapes for v in shape['Vertex Data']]
        lo, hi = bounds(all_vertices)
        center = tuple((a+b)/2 for a, b in zip(lo, hi))
        offset = tuple(a+b for a, b in zip(center, translation))
        node = self.node(name, offset)
        node_ref = self.add('NiNode', node)
        self.blocks[0][1]['Children'].append(node_ref)
        physics = box_physics(bytes.fromhex(self.donor['3 bhkPhysicsSystem']['Binary Data']), sub(hi, lo))
        collision = {'Target': node_ref, 'Flags': '128', 'Data': 'None', 'Body ID': '0'}
        collision_ref = self.add('bhkNPCollisionObject', collision)
        if self.root_collision and not self.parts:
            # Match the vanilla basketball hoop and picture frame: the placed root
            # owns the support collision. Child-only boxes leave that root without
            # a collision object for workshop placement to inspect.
            physics = translate_box_physics(physics, offset)
            collision['Target'] = self.root
            self.blocks[0][1]['Collision Object'] = collision_ref
        else:
            node['Collision Object'] = collision_ref
        collision['Data'] = self.add('bhkPhysicsSystem', {'Binary Data': physics.hex().upper()})
        for shape, shader_index in zip(shapes, shader_indices):
            shape['Name'] = name+(' Glass' if not node['Children'] and name.endswith(' Head') else ' Frame')
            shape['Transform'] = self.node('', (0, 0, 0))['Transform']
            shape['Collision Object'] = 'None'
            shape['VertexDesc'] = copy.deepcopy(self.donor['4 BSTriShape']['VertexDesc'])
            for vertex in shape['Vertex Data']:
                vertex['Vertex'] = fmt(sub(vec(vertex['Vertex']), center))
            slo, shi = bounds(shape['Vertex Data'])
            sphere_center = tuple((a+b)/2 for a, b in zip(slo, shi))
            radius = max(math.dist(vec(v['Vertex']), sphere_center) for v in shape['Vertex Data'])+0.05
            shape['Bounding Sphere'] = {'Center': fmt(sphere_center), 'Radius': f'{radius:.9f}'}
            shape['Num Vertices'] = str(len(shape['Vertex Data']))
            shape['Num Triangles'] = str(len(shape['Triangles']))
            shape['Data Size'] = str(20*len(shape['Vertex Data'])+6*len(shape['Triangles']))
            shape['Alpha Property'] = 'None'
            node['Children'].append(self.add('BSTriShape', shape))
            shader = copy.deepcopy(source[f'{shader_index} BSLightingShaderProperty'])
            shader['Controller'] = 'None'
            shader['Shader Flags 1'] = ' | '.join(x for x in shader['Shader Flags 1'].split(' | ') if x != 'Skinned')
            texture_index = int(shader['Texture Set'].split()[0])
            shape['Shader Property'] = self.add('BSLightingShaderProperty', shader)
            shader['Texture Set'] = self.add('BSShaderTextureSet', copy.deepcopy(source[f'{texture_index} BSShaderTextureSet']))
        self.parts.append({'name': name, 'collision_node': self.name if self.root_collision and not self.parts else name,
                           'center': offset, 'half': tuple((b-a)/2 for a, b in zip(lo, hi)),
                           'triangles': sum(len(s['Triangles']) for s in shapes)})

    def document(self):
        header = copy.deepcopy(self.donor['NiHeader'])
        header['Export Info'] = {'Author': 'Mirrors of Fallout', 'Process Script': '', 'Export Script': 'Workshop mirror generator'}
        header['Max Filepath'] = ''
        header['Num Blocks'] = str(len(self.blocks))
        header['Block Types'] = list(dict.fromkeys(k for k, _ in self.blocks))
        header['Block Type Index'] = [k for k, _ in self.blocks]
        header['Block Size'] = ['0']*len(self.blocks)
        header['Strings'] = []
        header['Num Strings'] = '0'
        header['Max String Length'] = '0'
        return {'NiHeader': header, **{f'{i} {k}': b for i, (k, b) in enumerate(self.blocks)},
                'NiFooter': {'Roots': [self.root]}}


def box_shape(donor, half):
    shape = copy.deepcopy(donor['4 BSTriShape'])
    shape['Vertex Data'], shape['Triangles'] = [], []
    shape['Skin'] = 'None'
    for axis in range(3):
        for sign in (-1, 1):
            # Build consistently wound quads; source normals and triangles agree.
            u, v = (axis+1) % 3, (axis+2) % 3
            first = len(shape['Vertex Data'])
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                normal = [0., 0., 0.]; normal[axis] = sign
                tangent = [0., 0., 0.]; tangent[u] = 1
                bitangent = [0., 0., 0.]; bitangent[v] = sign
                p = [0., 0., 0.]; p[axis] = sign*half[axis]; p[u] = a*half[u]; p[v] = b*half[v]*sign
                vertex = {'Vertex': fmt(p), 'Normal': fmt(normal), 'Tangent': fmt(tangent),
                          'UV': f'{0.12+(a+1)*0.015:.6f} {0.06+(b+1)*0.015:.6f}'}
                vertex.update({'Bitangent '+x: str(value) for x, value in zip('XYZ', bitangent)})
                shape['Vertex Data'].append(vertex)
            shape['Triangles'].extend([f'{first} {first+1} {first+2}', f'{first} {first+2} {first+3}'])
    return shape


def make_head(source, design, complete_cabinet=False):
    if design == 'Cabinet':
        axes = ((1., 0., 0.), (0., 1., 0.), (0., 0., 1.))
        center = (-0.62109375, -9.0078125, 0.0078125)
        glass = source['22 BSTriShape']; frame = source['27 BSTriShape']
        indices = (set(range(len(frame['Vertex Data']))) if complete_cabinet else
                   {i for i, v in enumerate(frame['Vertex Data']) if v['Bone Indices'][0] == '1'})
        half = (15.73828125, 21.1484375); shader_indices = (25, 30)
    elif design == 'Round':
        n = normalized((0.74246, -0.50792, 0.43677))
        t = normalized((-0.17785, -0.77806, -0.60248))
        # Re-orthogonalize the measured basis before baking normals/tangents.
        t = normalized(tuple(x-dot(t, n)*y for x, y in zip(t, n)))
        b = (n[1]*t[2]-n[2]*t[1], n[2]*t[0]-n[0]*t[2], n[0]*t[1]-n[1]*t[0])
        axes = (t, tuple(-x for x in n), b)
        center = (32.0788, -29.3184, 136.3333)
        glass = source['4 BSTriShape']; frame = source['7 BSTriShape']
        indices = next(group for group in components(frame) if min(group) == 3479)
        assert len(indices) == 100
        half = (8.60, 8.60); shader_indices = (5, 8)
    else:
        n = normalized((0., -0.985398, 0.170265))
        axes = ((1., 0., 0.), tuple(-x for x in n), (0., n[2], -n[1]))
        center = (-0.29296875, -4.969721686, 18.777342907)
        glass = source['7 BSTriShape']; frame = source['4 BSTriShape']
        indices = set.union(*(group for group in components(frame) if min(group) in (436, 443, 475, 500, 518)))
        assert len(indices) == 100
        half = (10.92578125, 6.853985731); shader_indices = (8, 5)
    shapes = [fragment(glass, set(range(len(glass['Vertex Data']))), axes, center), fragment(frame, indices, axes, center)]
    return shapes, half, shader_indices


def scale_shape(shape, factors):
    for vertex in shape['Vertex Data']:
        vertex['Vertex'] = fmt(tuple(p*s for p, s in zip(vec(vertex['Vertex']), factors)))
        vertex['Normal'] = fmt(normalized(tuple(p/s for p, s in zip(vec(vertex['Normal']), factors))))
        vertex['Tangent'] = fmt(normalized(tuple(p*s for p, s in zip(vec(vertex['Tangent']), factors))))
        bitangent = normalized(tuple(float(vertex['Bitangent '+axis])*s for axis, s in zip('XYZ', factors)))
        for axis, value in zip('XYZ', bitangent):
            vertex['Bitangent '+axis] = f'{value:.9f}'


def make_round_assembly(source):
    # The sink's disconnected components 2421..3976 are the mirror's articulated
    # arm, head rim, pivots and wall bracket. Preserve their authored orientation;
    # discard the basin, taps and plumbing. fragment() rejects any cut triangle.
    glass = source['4 BSTriShape']
    frame = source['7 BSTriShape']
    indices = set.union(*(part for part in components(frame) if 2421 <= min(part) < 3977))
    lo, hi = bounds([frame['Vertex Data'][i] for i in indices])
    origin = ((lo[0]+hi[0])/2, 0., (lo[2]+hi[2])/2)
    axes = ((1., 0., 0.), (0., 1., 0.), (0., 0., 1.))
    shapes = [fragment(glass, set(range(len(glass['Vertex Data']))), axes, origin),
              fragment(frame, indices, axes, origin)]
    normal = normalized((0.74246, -0.50792, 0.43677))
    tangent = normalized((-0.17785, -0.77806, -0.60248))
    tangent = normalized(tuple(x-dot(tangent, normal)*y for x, y in zip(tangent, normal)))
    bitangent = (normal[1]*tangent[2]-normal[2]*tangent[1],
                 normal[2]*tangent[0]-normal[0]*tangent[2],
                 normal[0]*tangent[1]-normal[1]*tangent[0])
    return shapes, {'center': sub((32.0788, -29.3184, 136.3333), origin),
                    'normal': normal, 'tangent': tangent, 'bitangent': bitangent,
                    'half': (8.6, 8.6), 'ellipse': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    donor = json.loads((args.reference/'mirror02.json').read_text(encoding='utf-8-sig'))
    manifest = {'plugin': PLUGIN, 'models': [], 'source_sha256': {}}
    sources = [('Cabinet', 'playerhouse_bathroommirror02'), ('Round', 'playerhouse_bathroomsink01'), ('Vanity', 'playerhouse_bathroomshavedrawer01')]
    # Keep the six original forms in their original order. Existing wall models
    # become Small; append the new sizes so placed objects and recipes keep IDs.
    variants = [(design, stem, placement, '', 1.)
                for design, stem in sources for placement in ('Wall', 'Standing')]
    variants += [(design, stem, 'Wall', size, scale)
                 for design, stem in sources if design in ('Cabinet', 'Round')
                 for size, scale in (('Medium', 1.5), ('Large', 2.))]
    variants += [('CabinetUnit', 'playerhouse_bathroommirror02', 'Wall', '', 1.)]
    variants += [('RoundMetal', 'playerhouse_bathroomsink01', 'Wall', '', 1.)]
    # Append after the complete fixtures: none of the twelve existing FormIDs move.
    variants += [(design, stem, 'Wall', size, scale)
                 for design, stem in sources if design in ('Cabinet', 'Round')
                 for size, scale in (('Very Large', 4.), ('Gigantic', 8.))]
    for design, stem, placement, size, scale in variants:
        path = args.reference/(stem+'.json')
        manifest['source_sha256'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        source = json.loads(path.read_text(encoding='utf-8-sig'))
        name = design+placement+size.replace(' ', '')
        assembly_pane = None
        if design == 'RoundMetal':
            shapes, assembly_pane = make_round_assembly(source)
            half, shader_indices = assembly_pane['half'], (5, 8)
        else:
            shapes, half, shader_indices = make_head(source, 'Cabinet' if design == 'CabinetUnit' else design,
                                                    complete_cabinet=design == 'CabinetUnit')
        if placement == 'Wall' and design == 'Cabinet':
            # Bare reflective glass: remove every cabinet/frame fragment, retain width,
            # and double only its height. Existing standing objects keep their old mesh.
            shapes, shader_indices = shapes[:1], shader_indices[:1]
            scale_shape(shapes[0], (1., 1., 2.))
            half = (half[0], half[1]*2)
        elif placement == 'Wall' and design == 'Round':
            for shape in shapes:
                scale_shape(shape, (2., 2., 2.))
            half = (half[0]*2, half[1]*2)
        if scale != 1.:
            for shape in shapes:
                scale_shape(shape, (scale, scale, scale))
            half = tuple(v*scale for v in half)
        if design == 'Cabinet' and size == 'Gigantic':
            # Bake a quarter turn in the wall plane, including the tangent basis.
            # Keep the root/attachment orientation identical to the basketball hoop.
            shapes = [fragment(shape, set(range(len(shape['Vertex Data']))),
                               ((0., 0., 1.), (0., 1., 0.), (-1., 0., 0.)), (0., 0., 0.))
                      for shape in shapes]
            half = (half[1], half[0])
        lo, hi = bounds([v for s in shapes for v in s['Vertex Data']])
        pane_y = -hi[1] if placement == 'Wall' else 0.
        pane_z = 0. if placement == 'Wall' else 118.
        model = Model(donor, name, root_collision=placement == 'Wall')
        model.part(name+' Head', shapes, source, shader_indices, (0., pane_y, pane_z))
        if placement == 'Standing':
            bottom = lo[2]+pane_z
            post_top = bottom+5.
            # Supports are authored independently, not clipped remnants of the sink/drawer.
            model.part(name+' Post', [box_shape(donor, (1.4, 1.4, (post_top-3.)/2))],
                       donor, (5,), (0., hi[1]+1.4, (post_top+3.)/2))
            model.part(name+' Foot', [box_shape(donor, (max(half[0]*0.65, 7.), 10., 1.5))],
                       donor, (5,), (0., hi[1]+1.4, 1.5))
        document = model.document()
        (args.output/(name+'.json')).write_text(json.dumps(document, indent=2), encoding='utf-8', newline='\n')
        overall_lo = tuple(min(p['center'][i]-p['half'][i] for p in model.parts) for i in range(3))
        overall_hi = tuple(max(p['center'][i]+p['half'][i] for p in model.parts) for i in range(3))
        manifest['models'].append({'name': name, 'design': design, 'placement': placement,
            'buildable': placement == 'Wall',
            'model': 'MirrorsOfFallout\\Workshop\\'+name+'.nif', 'form_id': 0x800+len(manifest['models']),
            'pane': {'center': (0., pane_y, pane_z), 'normal': (0., -1., 0.), 'tangent': (1., 0., 0.),
                     'bitangent': (0., 0., 1.), 'half': half, 'ellipse': design == 'Round'},
            'bounds': [overall_lo, overall_hi], 'parts': model.parts})
        if placement == 'Wall' and design in ('Cabinet', 'Round'):
            manifest['models'][-1].update(size=size or 'Small', scale=scale)
        if assembly_pane:
            assembly_pane['center'] = tuple(x+y for x, y in zip(assembly_pane['center'], (0., pane_y, pane_z)))
            manifest['models'][-1]['pane'] = assembly_pane
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8', newline='\n')
    print('Authored thirteen buildable wall mirrors and three legacy standing meshes.')


if __name__ == '__main__':
    main()
