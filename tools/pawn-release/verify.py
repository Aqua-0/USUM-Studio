import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def native_hash(name):
    result = 0
    for value in name.encode('ascii'):
        result = ((result * 131) ^ value) & 0xffffffff
    return result


def verify(data):
    if len(data) < 60:
        raise ValueError('Missing AMX header')
    size, magic, version, vm, flags, entry_size = struct.unpack_from('<IHBBHH', data)
    if (magic, version, vm, entry_size) != (0xf1e0, 10, 10, 8) or flags & 1:
        raise ValueError('Expected version-10, 32-bit AMX without overlays')
    code, values, heap, stack, entry = struct.unpack_from('<5I', data, 12)
    if not (60 <= code < values <= heap <= stack and code <= size == len(data)):
        raise ValueError('Invalid AMX segments')
    if (values - code) % 4 or (heap - values) % 4 or entry % 4 or entry >= values - code:
        raise ValueError('Invalid cell layout or entry point')
    tables = struct.unpack_from('<7I', data, 32)
    if any(t < 60 or t > code for t in tables) or tuple(sorted(tables)) != tables:
        raise ValueError('Invalid AMX tables')
    start, end = tables[1:3]
    if (end - start) % 8:
        raise ValueError('Invalid native records')
    natives = {struct.unpack_from('<I', data, offset + 4)[0] for offset in range(start, end, 8)}
    if natives != {native_hash(n) for n in ('FlagGet', 'WorkSet', '_Suspend')}:
        raise ValueError('Unexpected native references')
    if flags & 4:
        count = bits = value = 0
        for byte in data[code:size]:
            if bits == 0:
                value = -1 if byte & 64 else 0
            value = (value << 7) | (byte & 127)
            bits += 7
            if bits > 35:
                raise ValueError('Oversized compact cell')
            if not byte & 128:
                if not -2147483648 <= value <= 4294967295:
                    raise ValueError('Cell exceeds 32 bits')
                count += 1
                bits = 0
        if bits or count != (heap - code) // 4:
            raise ValueError('Incomplete compact AMX payload')
    elif heap != size:
        raise ValueError('Unexpected uncompressed payload size')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--include', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='studio-pawn-check-') as directory:
        output = Path(directory) / 'probe.amx'
        result = subprocess.run([str(args.compiler.resolve()), str(Path(__file__).with_name('probe.pwn').resolve()),
                                 '-d0', '-o' + str(output), '-i' + str(args.include.resolve())],
                                cwd=directory, capture_output=True, text=True, timeout=120)
        print(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Pawn compiler smoke test failed')
        verify(output.read_bytes())
    print('Pawn smoke test passed: version 10, 32-bit cells, expected natives and complete payload')


if __name__ == '__main__':
    main()
