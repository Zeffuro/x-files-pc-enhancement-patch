"""Original-engine scenarios using physical input and checked observations."""

from enum import IntEnum
import json
from pathlib import Path
import subprocess
import time


class StoryVariable(IntEnum):
    BOMB_ACTIVE = 1939
    YARD_FINISHED = 1992
    WIRE_CUTTERS = 3969


class YardTarget(IntEnum):
    FACE_WORKBENCH = 6980
    WORKBENCH = 40199
    TOOLBOX = 41021
    PICK_UP_CUTTERS = 61956
    RETURN_TO_OFFICE = 6978


WIRE_CUTTER_GRAPHICS = {19470, 19472}


def saved_state(actor, path, baseline=False):
    arguments = (['--baseline'] if baseline else []) + [actor.windows_path(path)]
    result = actor.command(actor.tools / 'xfiles-native-save.exe', *arguments)
    return json.loads(result.stdout)


def file_generation(path):
    value = path.stat()
    return value.st_mtime_ns, value.st_ctime_ns, value.st_size


def wait_for_save(actor, path, previous, predicate, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if file_generation(path) != previous:
                state = saved_state(actor, path)
                if predicate(state):
                    return state
        except (OSError, ValueError, subprocess.SubprocessError):
            pass
        time.sleep(.1)
    raise TimeoutError('The original game did not write the expected save state.')


def has_target(state, identity):
    return (bool(state.get('scene_active')) and not state.get('modal') and not state.get('movie_active') and
            not state.get('pending_load') and not state.get('main_menu') and
            state.get('command_queue_count') == 0 and state.get('input_address') == 0 and
            any(item['id'] == identity for item in state.get('world_targets', [])))


def has_cutters(state):
    return any(item['id'] in WIRE_CUTTER_GRAPHICS for item in state.get('inventory', []))


def office_inventory(actor, state, acquired):
    return (has_target(state, YardTarget.FACE_WORKBENCH) and
            actor.variable(state, StoryVariable.WIRE_CUTTERS) == int(acquired) and
            has_cutters(state) == acquired)


def cutters(actor):
    for target in YardTarget:
        actor.target(int(target))
    actor.wait('wire cutters acquired',
               lambda state: office_inventory(actor, state, True))


def bomb_retry(actor, repetitions):
    for index in range(repetitions):
        state = actor.wait('native bomb death and retry prompt',
                           lambda state: (state.get('modal') or {}).get('text') == 'Try again?',
                           timeout=300)
        if actor.variable(state, StoryVariable.YARD_FINISHED) != 0:
            raise RuntimeError('The bomb death unexpectedly completed the yard story.')
        buttons = [item for item in state['modal']['items'] if 'text' not in item]
        if len(buttons) != 2:
            raise RuntimeError('Expected the original two-button retry prompt.')
        # The English prompt places Yes to the left of No.
        yes = min(buttons, key=lambda item: item['position'][0])
        actor.click(yes['position'][0] + 20, yes['position'][1] + 12)
        actor.wait('retry restores the playable yard office',
                   lambda state: has_target(state, YardTarget.FACE_WORKBENCH) and
                   actor.variable(state, StoryVariable.BOMB_ACTIVE) == 1 and
                   actor.variable(state, StoryVariable.WIRE_CUTTERS) == 0)
        actor.event('bomb_retry_completed', repetition=index + 1)


def story_roundtrip(actor, original_fixture):
    quicksave = actor.game / 'saves/QUICKSAVE.x'
    previous = file_generation(quicksave)
    actor.key(116)
    acquired = wait_for_save(actor, quicksave, previous,
                            lambda state: state['variables'].get(str(int(StoryVariable.WIRE_CUTTERS))) == 1)
    checkpoint = actor.output / 'acquired-cutters.x'
    checkpoint.write_bytes(quicksave.read_bytes())
    # Reload the untouched checkpoint to prove the later change can be rolled back.
    quicksave.write_bytes(Path(original_fixture).read_bytes())
    actor.key(120)
    actor.wait('load rolls back the acquired inventory and story flag',
               lambda state: office_inventory(actor, state, False))
    quicksave.write_bytes(checkpoint.read_bytes())
    actor.key(120)
    actor.wait('saved story flag survives loading',
               lambda state: office_inventory(actor, state, True))
    previous = file_generation(quicksave)
    actor.key(116)
    loaded = wait_for_save(actor, quicksave, previous,
                          lambda state: state['view'] == acquired['view'] and
                          state['variables'].get(str(int(StoryVariable.WIRE_CUTTERS))) == 1)
    if loaded['variable_count'] != acquired['variable_count']:
        raise RuntimeError('The native round trip changed the edition variable schema.')
    actor.event('story_roundtrip_completed', view=loaded['view'])
