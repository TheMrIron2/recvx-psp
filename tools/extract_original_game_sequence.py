"""Build the original gameplay loop and screen controllers without test-profile cuts."""
from pathlib import Path
import sys

from extract_original_boot_initializers import extract


def prepare(destination):
    game = Path('src/ps2/veronica/prog/game.c')
    source = game.read_text()
    headers = source[:source.index('// 100% matching!')]
    headers = headers.replace('../../../ps2/veronica/prog/', 'ps2/veronica/prog/')
    output = ['/* Generated from original sources; keep gameplay/render branches intact. */',
              '#if defined(RECVX_ENGINE_GAME_CALLBACK_TEST) || defined(RECVX_ENGINE_PASSIVE_RENDER)',
              '#error "Game boot requires the complete original gameplay/render sequence"',
              '#endif', headers]
    for path, names in [(game, ['bhMainSequence']),
                        (Path('src/ps2/veronica/prog/screen.c'),
                         ['bhControlScreenFade', 'bhControlCinesco'])]:
        for name in names:
            line, function = extract(path.read_text(), name)
            output += [f'#line {line} "{path.as_posix()}"', function, '']
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('\n'.join(output))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
