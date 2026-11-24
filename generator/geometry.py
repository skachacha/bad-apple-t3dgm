from pathlib import Path
import json
import struct
import numpy as np
from scipy import ndimage
PICTURES = 8780
WIDTH, HEIGHT = (320, 240)
MASK_BYTES = WIDTH * HEIGHT // 8

def read_cache(path):
    data = Path(path).read_bytes()
    if len(data) < 8 or struct.unpack_from('<2I', data) != (PICTURES, 1):
        raise ValueError('Invalid geometry cache header')
    at, frames = (8, {})
    for expected in range(PICTURES):
        if at + 12 > len(data):
            raise ValueError('Short geometry cache')
        frame, vertices, triangles = struct.unpack_from('<3I', data, at)
        at += 12
        if frame != expected or vertices > 65536 or triangles > 1000000:
            raise ValueError('Invalid geometry cache record')
        end = at + vertices * 8 + triangles * 12
        if end > len(data):
            raise ValueError('Short geometry payload')
        points = np.frombuffer(data, dtype='<f4', count=vertices * 2, offset=at).reshape(-1, 2).copy()
        at += vertices * 8
        faces = np.frombuffer(data, dtype='<u4', count=triangles * 3, offset=at).reshape(-1, 3).copy()
        at = end
        if not np.isfinite(points).all() or (faces.size and faces.max() >= vertices):
            raise ValueError('Invalid mesh coordinates or indices')
        frames[frame] = (points, faces)
    if at != len(data):
        raise ValueError('Unexpected geometry cache tail')
    return frames

def write_cache(path, frames):
    with Path(path).open('wb') as stream:
        stream.write(struct.pack('<2I', PICTURES, 1))
        for frame in range(PICTURES):
            points, faces = frames[frame]
            stream.write(struct.pack('<3I', frame, len(points), len(faces)))
            stream.write(points.astype('<f4').tobytes())
            stream.write(faces.astype('<u4').tobytes())

def read_rasters(path):
    path = Path(path)
    if path.stat().st_size != 8 + PICTURES * (MASK_BYTES + 4):
        raise ValueError('Invalid raster cache size')
    with path.open('rb') as stream:
        if struct.unpack('<2I', stream.read(8)) != (PICTURES, 1):
            raise ValueError('Invalid raster cache header')
    records = np.memmap(path, dtype=np.dtype([('frame', '<u4'), ('mask', 'u1', (MASK_BYTES,))]), mode='r', offset=8, shape=(PICTURES,))
    if not np.array_equal(records['frame'], np.arange(PICTURES)):
        raise ValueError('Invalid raster picture sequence')
    return records

def unpack(packed):
    return np.unpackbits(packed, bitorder='little').reshape(HEIGHT, WIDTH).astype(bool)

def risks(source, base, candidate, relaxed):
    """Raster safeguards, not a semantic or perceptual equivalence guarantee."""
    lost = coverage = merges = splits = topology = 0
    for color in (False, True):
        labels, true_count = ndimage.label(source == color)
        bl, base_count = ndimage.label(base == color)
        nl, new_count = ndimage.label(candidate == color)
        sizes = np.bincount(labels.ravel())
        small = (sizes <= 128) & (sizes > 0)
        small[0] = False
        old = np.bincount(labels[base == color], minlength=len(sizes))
        new = np.bincount(labels[candidate == color], minlength=len(sizes))
        represented = small & (old > 0)
        lost += int(((new == 0) & represented).sum())
        allowance = np.maximum(2, 0.08 * old) if relaxed else 0
        coverage += int((represented & (old - new > allowance)).sum())
        topology += int(abs(new_count - true_count) > abs(base_count - true_count))
        focus = represented[labels]
        if focus.any():
            together = (base == color) & (candidate == color)
            pairs = np.unique(nl[together].astype(np.int64) * (base_count + 1) + bl[together])
            counts = np.bincount((pairs // (base_count + 1)).astype(int), minlength=new_count + 1)
            touched = np.unique(nl[focus & (candidate == color)])
            touched = touched[touched > 0]
            merges += int((counts[touched] > 1).sum())
            aa = np.unique(labels[focus & (base == color)].astype(np.int64) * (base_count + 1) + bl[focus & (base == color)])
            bb = np.unique(labels[focus & (candidate == color)].astype(np.int64) * (new_count + 1) + nl[focus & (candidate == color)])
            old_parts = np.bincount((aa // (base_count + 1)).astype(int), minlength=len(sizes))
            new_parts = np.bincount((bb // (new_count + 1)).astype(int), minlength=len(sizes))
            splits += int((new_parts[represented] > old_parts[represented]).sum())
    return {'lost_small': lost, 'coverage': coverage, 'merges': merges, 'splits': splits, 'component_error': topology}

def filter_cache(source_path, base_path, proposal_path, base_masks_path, proposal_masks_path, destination, report_path, relaxed=False):
    source = np.memmap(source_path, dtype='u1', mode='r', shape=(PICTURES, MASK_BYTES))
    baseline, proposal = (read_cache(base_path), read_cache(proposal_path))
    base_masks, proposal_masks = (read_rasters(base_masks_path), read_rasters(proposal_masks_path))
    kept, choices = ({}, [])
    for frame in range(PICTURES):
        risk = risks(unpack(source[frame]), unpack(base_masks[frame]['mask']), unpack(proposal_masks[frame]['mask']), relaxed)
        fallback = any(risk.values())
        if relaxed:
            fallback = fallback or len(proposal[frame][0]) > len(baseline[frame][0])
        kept[frame] = baseline[frame] if fallback else proposal[frame]
        choices.append(bool(fallback))
        if frame % 500 == 0:
            print('Detail checks', frame, '/', PICTURES, flush=True)
    write_cache(destination, kept)
    report = {'pictures': PICTURES, 'reference_fallbacks': sum(choices), 'vertices_before_deduplication': sum((len(v) for v, t in kept.values())), 'fallback_by_picture': choices}
    Path(report_path).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report
