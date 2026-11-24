from pathlib import Path
import hashlib
import math
import numpy as np
from geometry import read_cache
from binary_x import encode, decode
FOV = 1.0814433097839355
BODY_SCALE = 0.9
PLANE_Z = 6
DISTANCE = 27 + (PLANE_Z - 0.03) * BODY_SCALE
FULL_HEIGHT = 2 * DISTANCE * math.tan(FOV / 2) / BODY_SCALE * 1.004
UNIT = FULL_HEIGHT * 4 / 3 / 640
CENTER_Y = (15 * BODY_SCALE + 2) / BODY_SCALE
END = 17561

def fnum(value):
    text = format(float(value), '.9f').rstrip('0')
    return '0.' if text in ('0.', '-0.') else text

def vec(values):
    return ','.join((fnum(value) for value in values))

def matrix(scale=(1, 1, 1), pos=(0, 0, 0)):
    return 'FrameTransformMatrix { ' + vec((scale[0], 0, 0, 0, 0, scale[1], 0, 0, 0, 0, scale[2], 0, *pos, 1)) + ';; }\n'

def quad(name, material, width, height):
    positions = [(-width / 2, -height / 2, 0), (-width / 2, height / 2, 0), (width / 2, height / 2, 0), (width / 2, -height / 2, 0)] * 2
    normals = [(0, 0, -1)] * 4 + [(0, 0, 1)] * 4
    faces = ((0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6))
    rows = lambda values: ',\n'.join((';'.join((fnum(x) for x in row)) + ';' for row in values)) + ';'
    indices = ',\n'.join(('3;' + ','.join(map(str, face)) + ';' for face in faces)) + ';'
    return f'Mesh {name} {{8;' + rows(positions) + '4;' + indices + 'MeshNormals {8;' + rows(normals) + '4;' + indices + '}' + 'MeshTextureCoords {8;' + rows([(0, 1), (0, 0), (1, 0), (1, 1)] * 2) + '}MeshMaterialList {1;4;0,0,0,0;;{' + material + '}}}'

def channel(kind, values):
    return f'AnimationKey{{{kind};{len(values)};' + ''.join((f'{time};{len(value)};' + vec(value) + ';;' + (';' if index == len(values) - 1 else ',') for index, (time, value) in enumerate(values))) + '}\n'

def animation(name, scales, positions):
    return 'Animation{{' + name + '}\n' + channel(0, [(0, (1, 0, 0, 0))]) + channel(1, scales) + channel(2, positions) + '}\n'

def steps(values, end):
    values = np.asarray(values)
    changes = np.flatnonzero(np.any(values[1:] != values[:-1], axis=1)) + 1
    out = [(0, tuple(values[0]))]
    for picture in changes:
        time = 1 + 2 * int(picture)
        out.extend(((time, tuple(values[picture - 1])), (time, tuple(values[picture]))))
    if out[-1][0] != end:
        out.append((end, tuple(values[-1])))
    return out

def rawmesh(f, name, vs, tris):
    vs = np.asarray(vs, dtype=np.float32)
    tris = np.asarray(tris, np.uint32).copy()
    q = vs[tris]
    cross = np.cross(q[:, 1] - q[:, 0], q[:, 2] - q[:, 1])
    keep = cross[:, 2] != 0
    tris = tris[keep]
    cross = cross[keep]
    flip = cross[:, 2] > 0
    tris[flip] = tris[flip][:, [0, 2, 1]]
    used = np.unique(tris)
    assert len(used) == len(vs) and int(used.max()) < 65536 and np.isfinite(vs).all()
    assert np.all(np.abs(cross[:, :2]) == 0)
    f.write(f'Mesh {name}{{{len(vs)};' + ',\n'.join((';'.join((fnum(x) for x in row)) + ';' for row in vs)) + ';' + f'{len(tris)};' + ',\n'.join(('3;' + ','.join(map(str, t)) + ';' for t in tris)) + ';MeshMaterialList{1;1;0;;{White}}}\n')
    return (len(vs), len(tris))

def gated(values, end, hidden):
    keys = steps(values, end)
    out = [(0, hidden)]
    if tuple(values[0]) != tuple(hidden):
        out.extend(((1, hidden), (1, tuple(values[0]))))
    out.extend(keys[1:])
    if out[-1][1] != tuple(hidden):
        out.append((end, hidden))
    return out

def write_model(cache, text_path, decoded_path, destination):
    name = 'badapple'
    start = 0
    count = 8780
    bank_cap = 512
    allframes = read_cache(cache)
    unique = []
    seen = {}
    uids = []
    offsets = []
    minimum = 0
    before_v = 0
    for frame in range(start, start + count):
        points, tri = allframes[frame]
        before_v += len(points)
        if not len(points):
            uids.append(-1)
            offsets.append((0, 0))
            continue
        points = points.copy()
        a = points[tri]
        area = (a[:, 1, 0] - a[:, 0, 0]) * (a[:, 2, 1] - a[:, 0, 1]) - (a[:, 1, 1] - a[:, 0, 1]) * (a[:, 2, 0] - a[:, 0, 0])
        tri = tri[area != 0]
        used = np.unique(tri)
        mapping = np.full(len(points), -1, np.int32)
        mapping[used] = np.arange(len(used))
        points = points[used]
        tri = mapping[tri]
        origin = points.min(axis=0)
        points -= origin
        digest = hashlib.sha256(points.tobytes() + tri.tobytes()).digest()
        uid = seen.get(digest)
        if uid is None:
            uid = len(unique)
            seen[digest] = uid
            unique.append((points, tri))
        else:
            assert np.array_equal(points, unique[uid][0]) and np.array_equal(tri, unique[uid][1])
        uids.append(uid)
        offsets.append(tuple(origin))
    batches = []
    current = []
    nv = nt = 0
    where = {}
    for uid, (points, tri) in enumerate(unique):
        if current and (nv + len(points) > 30000 or nt + len(tri) > 45000 or len(current) >= bank_cap):
            batches.append(current)
            current = []
            nv = nt = 0
        tile = len(current)
        tx = tile % 16 * 1408
        ty = tile // 16 * 1088
        where[uid] = (len(batches), tx, ty)
        current.append((uid, tx, ty))
        nv += len(points)
        nt += len(tri)
    if current:
        batches.append(current)
    assert len(batches) + 3 <= 511
    locations = np.asarray([(-1, 0, 0) if uid < 0 else where[uid] for uid in uids], np.int32)
    offsets = np.asarray(offsets)
    active = (0, CENTER_Y, PLANE_Z)
    hidden = (0, -600, PLANE_Z)
    end = count * 2 + 1
    anim = animation
    tot_v = 8
    tot_tri = 4
    with Path(text_path).open('w', encoding='ascii', newline='\n') as f:
        f.write('xof 0303txt 0032\nHeader{1;0;1;}Material Black{0.;0.;0.;1.;;0.;0.;0.;0.;;0.;0.;0.;;}Material White{1.;1.;1.;1.;;0.;0.;0.;0.;;0.;0.;0.;;}Frame Root{' + matrix() + 'Frame Screen{' + matrix((UNIT, UNIT, 1), active) + 'Frame Backing{' + matrix(pos=(0, 0, 0.1)) + quad('BlackPanel', 'Black', 640 * 1.03, 480 * 1.03) + '}')
        for j, batch in enumerate(batches):
            vs = []
            ts = []
            at = 0
            for uid, tx, ty in batch:
                points, tri = unique[uid]
                vs.extend(np.c_[points[:, 0] * 2 - 320 + tx, 240 - points[:, 1] * 2 + ty, np.zeros(len(points))])
                ts.extend(tri + at)
                at += len(points)
            f.write(f'Frame B{j:03}{{' + matrix((1e-09, 1e-09, 1e-09), (0, 0, -100)))
            vv, tt = rawmesh(f, f'P{j:03}', vs, ts)
            tot_v += vv
            tot_tri += tt
            f.write('}')
        f.write('}}AnimationSet Playback{' + anim('Root', [(0, (1, 1, 1))], [(0, (0, 0, 0))]) + anim('Screen', [(0, (UNIT, UNIT, 1))], [(0, hidden), (1, hidden), (1, active), (end, active), (end, hidden)]) + anim('Backing', [(0, (1, 1, 1))], [(0, (0, 0, 0.1))]))
        for j in range(len(batches)):
            scales = np.full((count, 3), 1e-09)
            positions = np.tile((0.0, 0.0, -100.0), (count, 1))
            ix = np.flatnonzero(locations[:, 0] == j)
            scales[ix] = 1
            positions[ix, 0] = 2 * offsets[ix, 0] - locations[ix, 1]
            positions[ix, 1] = -2 * offsets[ix, 1] - locations[ix, 2]
            positions[ix, 2] = -0.03
            f.write(anim(f'B{j:03}', gated(scales, end, (1e-09, 1e-09, 1e-09)), gated(positions, end, (0.0, 0.0, -100.0))))
        f.write('}')
    encoded = encode(Path(text_path), Path(destination), True)
    checked = decode(Path(destination), Path(decoded_path))
    if encoded['semantic_sha256'] != checked['semantic_sha256'] or encoded['numeric_words'] != checked['numeric_words']:
        raise RuntimeError('Binary model semantic verification failed')
    return dict(name=name, pictures=count, width=320, height=240, fps=40, end=end, vertices=tot_v, triangles=tot_tri, mesh_parts=len(batches) + 1, hierarchy_nodes=len(batches) + 3, unique_pictures=len(unique), total_keys=checked['total_keys'], largest_channel=checked['max_channel_keys'], binary_bytes=Path(destination).stat().st_size, **encoded)
