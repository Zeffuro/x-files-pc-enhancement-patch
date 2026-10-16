"""Opt-in native game regressions. Requires a private, edition-matched checkpoint."""

import argparse
import json
from pathlib import Path

from .driver import NativeGame
from .files import PreservedGame, digest, set_ini
from .scenarios import bomb_retry, cutters, has_target, saved_state, story_roundtrip, YardTarget


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--fixture-sha256', required=True)
    parser.add_argument('--story-fixture', type=Path)
    parser.add_argument('--story-fixture-sha256')
    parser.add_argument('--edition', choices=('CD', 'DVD'), required=True)
    parser.add_argument('--retries', type=int, default=2)
    parser.add_argument('--skip-clips', action='store_true', help='Use native skipping only when the active movie allows it.')
    parser.add_argument('--wine')
    parser.add_argument('--prefix', type=Path)
    args = parser.parse_args()
    if not 1 <= args.retries <= 10:
        parser.error('--retries must be between 1 and 10')
    if digest(args.fixture) != args.fixture_sha256.lower():
        parser.error('The fixture SHA-256 does not match.')
    if bool(args.story_fixture) != bool(args.story_fixture_sha256):
        parser.error('--story-fixture and --story-fixture-sha256 must be supplied together.')
    if args.story_fixture and digest(args.story_fixture) != args.story_fixture_sha256.lower():
        parser.error('The story fixture SHA-256 does not match.')
    actor = NativeGame(args.game.resolve(), args.tools.resolve(), args.output.resolve(),
                       args.wine, args.prefix, args.skip_clips)
    report = {'status': 'failed', 'edition': args.edition, 'fixture_sha256': args.fixture_sha256,
              'story_fixture_sha256': args.story_fixture_sha256 or args.fixture_sha256,
              'retries_requested': args.retries, 'skip_clips': args.skip_clips}
    preserved = None
    completed = False
    try:
        actor.preflight()
        baseline = saved_state(actor, args.game / 'XFILES.GAM', baseline=True)
        for path in {args.fixture, args.story_fixture} - {None}:
            fixture = saved_state(actor, path)
            if fixture['variables'].keys() != baseline['variables'].keys():
                parser.error('The fixture variable schema differs from this installation. Use an edition-matched save.')
            if args.edition == 'DVD' and fixture.get('current_database_error'):
                parser.error(fixture['current_database_error'])
            if (fixture['view'] != 6977 or fixture['variables'].get('1939') != 1 or
                    fixture['variables'].get('1992') != 0 or fixture['variables'].get('3969') != 0):
                parser.error('Expected the active-bomb office checkpoint before acquiring wire cutters.')
        preserved = PreservedGame(actor.game, actor.output)
        original = actor.output / 'starting-checkpoint.x'
        original.write_bytes(args.fixture.read_bytes())
        story_original = original
        if args.story_fixture:
            story_original = actor.output / 'story-checkpoint.x'
            story_original.write_bytes(args.story_fixture.read_bytes())
        set_ini(actor.game / 'patch.ini', {'Enhancements': {'ContinueLatest': 0, 'Autosaves': 0, 'QuickMenu': 0},
                                         'Input': {'ConfineMouseWindowed': 0}})
        set_ini(actor.game / 'preferences.ini', {'Game': {'Skip Intro': 1, 'Skip Clips': int(args.skip_clips)}})
        display = {'windowed': 'true', 'fullscreen': 'false', 'width': 640, 'height': 480}
        if args.wine:
            display['renderer'] = 'opengl'
        set_ini(actor.game / 'ddraw.ini', {'ddraw': display})
        quicksave = actor.game / 'saves/QUICKSAVE.x'
        quicksave.parent.mkdir(parents=True, exist_ok=True)
        quicksave.write_bytes(original.read_bytes())
        actor.start()
        menu = actor.wait('original main menu', lambda state: state.get('main_menu') is True)
        if menu['edition'] != args.edition:
            raise RuntimeError('The requested edition does not match the running original executable.')
        actor.key(120)
        actor.wait('checkpoint opens in the yard office', lambda state: has_target(state, YardTarget.FACE_WORKBENCH))
        bomb_retry(actor, args.retries)
        if args.story_fixture:
            actor.click(620, 20)
            actor.wait('original main menu before the story checkpoint',
                       lambda state: state.get('main_menu') is True)
            quicksave.write_bytes(story_original.read_bytes())
            actor.key(120)
            actor.wait('story checkpoint opens in the yard office',
                       lambda state: has_target(state, YardTarget.FACE_WORKBENCH))
            actor.event('story_checkpoint_loaded', sha256=args.story_fixture_sha256)
        cutters(actor)
        story_roundtrip(actor, story_original)
        report.update(retries_completed=args.retries, profile=menu['profile'])
        completed = True
    except BaseException as error:
        report['error'] = str(error) or type(error).__name__
        raise
    finally:
        try:
            try:
                if preserved is not None:
                    actor.close()
            finally:
                actor.finish_prefix()
            if preserved is not None:
                report['preservation'] = preserved.restore()
            if completed:
                report['status'] = 'passed'
        except Exception as error:
            report.update(status='failed', cleanup_error=str(error))
            raise
        finally:
            if preserved is not None:
                (actor.output / 'summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
