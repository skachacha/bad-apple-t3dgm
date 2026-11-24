import re, struct, hashlib, math
from functools import lru_cache
TOKEN = re.compile(b'(?P<skip>\\s+|//[^\\r\\n]*|/\\*.*?\\*/)|(?P<num>[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)|(?P<name>[A-Za-z_][A-Za-z0-9_]*)|(?P<brace>[{}])|(?P<punct>[;,])', re.S)
W = lambda n: struct.pack('<H', n)
D = lambda n: struct.pack('<I', n)

def encode(source, destination, raw=False):
    data = source.read_bytes()
    assert data[:16] == b'xof 0303txt 0032'
    at = 16
    buf = bytearray()
    kind = None
    groups = words = 0
    semantic = hashlib.sha256()
    with destination.open('wb') as f:
        f.write(b'xof 0303bin 0032')

        def flush():
            nonlocal buf, kind, groups, words
            if buf:
                f.write(W(kind) + D(len(buf) // 4) + buf)
                groups += 1
                words += len(buf) // 4
                buf = bytearray()
                kind = None
        while at < len(data):
            m = TOKEN.match(data, at)
            if not m:
                raise ValueError(('unsupported source token', at, data[at:at + 30]))
            at = m.end()
            typ = m.lastgroup
            value = m[0]
            if typ in ('skip', 'punct'):
                continue
            if typ == 'num':
                k = 7 if b'.' in value or b'e' in value.lower() else 6
                payload = struct.pack('<f', float(value)) if k == 7 else D(int(value) & 4294967295)
                if raw:
                    k = 6
                if kind is not None and kind != k:
                    flush()
                kind = k
                buf.extend(payload)
                semantic.update(b'N' + payload)
            else:
                flush()
                if typ == 'name':
                    f.write(W(1) + D(len(value)) + value)
                    semantic.update(b'S' + D(len(value)) + value)
                else:
                    f.write(W(10 if value == b'{' else 11))
                    semantic.update(value)
        flush()
    return dict(binary_numeric_records=groups, numeric_words=words, semantic_sha256=semantic.hexdigest())

class Stream:

    def __init__(self, data):
        assert data[:16] == b'xof 0303bin 0032'
        self.data = data
        self.at = 16
        self.look = None
        self.semantic = hashlib.sha256()
        self.words = 0

    def take(self):
        if self.look is not None:
            r = self.look
            self.look = None
            return r
        if self.at == len(self.data):
            return None
        assert self.at + 2 <= len(self.data)
        k = struct.unpack_from('<H', self.data, self.at)[0]
        self.at += 2
        if k in (1, 6, 7):
            assert self.at + 4 <= len(self.data)
            n = struct.unpack_from('<I', self.data, self.at)[0]
            self.at += 4
            size = n if k == 1 else n * 4
            assert self.at + size <= len(self.data)
            v = self.data[self.at:self.at + size]
            self.at += size
            if k == 1:
                self.semantic.update(b'S' + D(n) + v)
            else:
                for i in range(n):
                    self.semantic.update(b'N' + v[i * 4:i * 4 + 4])
                self.words += n
            return (k, v)
        assert k in (10, 11), ('unsupported binary token', k, self.at - 2)
        self.semantic.update(b'{' if k == 10 else b'}')
        return (k, None)

    def peek(self):
        if self.look is None:
            self.look = self.take()
        return self.look

    def need(self, k):
        item = self.take()
        assert item and item[0] == k, (k, item, self.at)
        return item[1]

    def node(self):
        kind = self.need(1).decode()
        name = ''
        if self.peek()[0] == 1:
            name = self.need(1).decode()
        self.need(10)
        items = []
        raw = bytearray()
        while self.peek()[0] != 11:
            k, v = self.peek()
            if k in (6, 7):
                self.take()
                raw.extend(v)
            elif k == 10:
                self.take()
                ref = self.need(1).decode()
                self.need(11)
                items.append(('ref', ref, b'', []))
            else:
                items.append(self.node())
        self.need(11)
        return (kind, name, bytes(raw), items)

@lru_cache(maxsize=None)
def fword(word):
    value = struct.unpack('<f', D(word))[0]
    assert math.isfinite(value)
    s = format(value, '.9g')
    if '.' not in s:
        if 'e' in s:
            s = s.replace('e', '.e')
        else:
            s += '.'
    assert struct.pack('<f', float(s)) == D(word)
    return s

def kinds(kind, raw):
    n = len(raw) // 4
    u = lambda i: struct.unpack_from('<I', raw, i * 4)[0]
    if kind == 'FrameTransformMatrix':
        assert n == 16
        return [True] * n
    if kind == 'Material':
        assert n == 11
        return [True] * n
    if kind == 'Header':
        assert n == 3
        return [False] * n
    if kind in ('Frame', 'Animation', 'AnimationSet'):
        assert n == 0
        return []
    if kind == 'MeshMaterialList':
        assert n == 2 + u(1)
        return [False] * n
    if kind in ('Mesh', 'MeshNormals'):
        count = u(0)
        out = [False] + [True] * (count * 3)
        at = 1 + count * 3
        faces = u(at)
        out.append(False)
        at += 1
        for _ in range(faces):
            length = u(at)
            out.extend([False] * (length + 1))
            at += length + 1
        assert at == n
        return out
    if kind == 'MeshTextureCoords':
        assert n == 1 + 2 * u(0)
        return [False] + [True] * (n - 1)
    if kind == 'AnimationKey':
        count = u(1)
        out = [False, False]
        at = 2
        for _ in range(count):
            width = u(at + 1)
            assert width == (4 if u(0) == 0 else 3)
            out.extend([False, False] + [True] * width)
            at += width + 2
        assert at == n
        return out
    raise ValueError(('unsupported schema', kind, n))

def decode(source, destination):
    s = Stream(source.read_bytes())
    blocks = 0
    payload = 0
    keycount = 0
    largest = 0
    buffer_digest = hashlib.sha256()
    with destination.open('w', encoding='ascii', newline='\n') as f:
        f.write('xof 0303txt 0032\n')

        def emit(node):
            nonlocal blocks, payload, keycount, largest
            kind, name, raw, children = node
            if kind == 'ref':
                f.write('{' + name + '}\n')
                return
            blocks += 1
            payload += len(raw)
            buffer_digest.update(kind.encode() + b'\x00' + name.encode() + b'\x00' + D(len(raw)) + raw)
            if kind == 'AnimationKey':
                count = struct.unpack_from('<I', raw, 4)[0]
                keycount += count
                largest = max(largest, count)
            f.write(kind + (' ' + name if name else '') + '{\n')
            schema = kinds(kind, raw)
            for i, floating in enumerate(schema):
                word = struct.unpack_from('<I', raw, i * 4)[0]
                f.write((fword(word) if floating else str(word)) + ';')
            f.write('\n')
            for c in children:
                emit(c)
            f.write('}\n')
        while s.peek() is not None:
            emit(s.node())
    assert s.at == len(s.data)
    return dict(semantic_sha256=s.semantic.hexdigest(), numeric_words=s.words, blocks=blocks, numeric_payload_bytes=payload, data_buffer_sha256=buffer_digest.hexdigest(), total_keys=keycount, max_channel_keys=largest)
