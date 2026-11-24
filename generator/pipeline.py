import hashlib
import json
import subprocess
from pathlib import Path

def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()

def run_cpp(work, label, executable, arguments, inputs, outputs):
    """Reuse a completed stage only when command, inputs and outputs still match."""
    work = Path(work)
    command = [str(executable), *map(str, arguments)]
    signature = {'command': command, 'tool': sha256(executable), 'inputs': [sha256(path) for path in inputs]}
    key = hashlib.sha256(json.dumps(signature, sort_keys=True).encode()).hexdigest()
    marker = work / (label + '.stage.json')
    if marker.exists():
        old = json.loads(marker.read_text(encoding='utf-8'))
        if old.get('key') == key and all((Path(path).is_file() for path in outputs)) and (old.get('outputs') == [sha256(path) for path in outputs]):
            print('Reusing ' + label, flush=True)
            return
    print('Running ' + label, flush=True)
    with (work / (label + '.log')).open('wb') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(label + ' failed; see ' + str(work / (label + '.log')))
    record = {'key': key, 'outputs': [sha256(path) for path in outputs]}
    marker.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
