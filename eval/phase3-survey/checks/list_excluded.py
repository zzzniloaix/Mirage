"""Compares this record with the survey directory it was made from (<out> in its files): every path
of the survey directory that is not here, with the rule that left it out; every file that is here
but changed, with the lines that differ (their numbers only); every file that is only here; and the
sizes of the files here. It names paths relative to the two directories and prints no file content.

usage: python3 -I list_excluded.py <survey directory> <this directory>
"""
import difflib
import os
import sys

MEDIA = ('.ts', '.mkv', '.mp4', '.m4s', '.mov', '.y4m', '.yuv', '.aac', '.ac3', '.eac3', '.mp3', '.m4a',
         '.m4v', '.wav', '.flac', '.h264', '.264', '.h265', '.265', '.hevc', '.webm', '.mpg', '.mpeg')
MACHO = (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\x7fELF')
RULES = [  # (reason, test on the relative path), the first that applies
    ('third-party SCTE-35 messages (owner question Q21)',
     lambda p: p in ('scte35_vectors.json', 'scte35_evidence/vectors_src.json')),
    ('a decode of a third-party SCTE-35 message only: nothing readable is left once it is redacted',
     lambda p: p.startswith('scte35_evidence/runs/decode-')),
    ('third-party source code (FFmpeg n8.1.1)', lambda p: p.startswith('testgen/ffmpeg-n8.1.1/')),
    ('third-party document (an RFC text)', lambda p: p.startswith('testgen/refs/')),
    ('fetched third-party pages, PDFs and code', lambda p: p.startswith('scte35_evidence/downloads/')),
    ('text extracted from the fetched third-party documents', lambda p: p.startswith('scte35_evidence/derived/')),
    ('generated media', lambda p: p.lower().endswith(MEDIA)),
    ('build product (Python bytecode)', lambda p: p.endswith('.pyc') or '/__pycache__/' in p),
]


def files(root):
    out = set()
    for r, _, fs in os.walk(root):
        for f in fs:
            out.add(os.path.relpath(os.path.join(r, f), root))
    return out


def main(src, here):
    a, b = files(src), files(here)
    groups = {}
    for p in sorted(a - b):
        full = os.path.join(src, p)
        reason = next((why for why, test in RULES if test(p)), None)
        if reason is None:
            with open(full, 'rb') as fh:
                if fh.read(4) in MACHO:
                    reason = 'executable (a build product)'
        groups.setdefault(reason or 'UNEXPECTED: no rule left it out', []).append(p)
    print(f'survey directory: {len(a)} files; this directory: {len(b)} files')
    print(f'left out: {len(a - b)} files')
    for reason, ps in groups.items():
        print(f'\n== {reason}: {len(ps)} files')
        for p in ps:
            print(f'   {p}')
    changed = []
    for p in sorted(a & b):
        x = open(os.path.join(src, p), 'rb').read()
        y = open(os.path.join(here, p), 'rb').read()
        if x != y:
            xl = x.decode('utf-8').split('\n')
            yl = y.decode('utf-8').split('\n')
            spans = []
            for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, xl, yl, autojunk=False).get_opcodes():
                if tag != 'equal':
                    spans.append(f'{i1 + 1}-{i2}' if i2 > i1 + 1 else f'{i1 + 1}' if i2 == i1 + 1 else f'(after {i1})')
            changed.append((p, len(xl), len(yl), spans))
    print(f'\n== changed in this copy: {len(changed)} files (line numbers of the survey directory\'s file)')
    for p, nx, ny, spans in changed:
        print(f'   {p}: lines {", ".join(spans)}' + ('' if nx == ny else f' ({nx} lines there, {ny} here)'))
    only = sorted(b - a)
    print(f'\n== only here: {len(only)} files')
    for p in only:
        print(f'   {p}')
    sizes = sorted((os.path.getsize(os.path.join(here, p)), p) for p in b)
    big = [p for s, p in sizes if s > 1000000]
    print(f'\n== sizes here: {sum(s for s, _ in sizes)} bytes in {len(b)} files; the largest, {sizes[-1][1]}, '
          f'{sizes[-1][0]} bytes; {len(big)} files over 1000000 bytes' + (': ' + ', '.join(big) if big else ''))
    return 1 if 'UNEXPECTED: no rule left it out' in groups else 0


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
