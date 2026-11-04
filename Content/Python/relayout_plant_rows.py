# -*- coding: utf-8 -*-
"""Lay out BP_TomatoTree / BP_LettuceTree of Kitchen_New into a tidy 2 x N grid.

Farm plot (fenced): inner area x 3340..3880 / y 8552..9718, terrain ~z 690.
  row pitch  = 250 cm  (west row tomato, east row lettuce, 130 cm aisle between)
  cell pitch = 135 cm  (9 columns, span 1080 cm, ~40 cm clear of both fences)
  centre     = (3625, 9160)

Each slot is ground-snapped with a downward line trace against the landscape, so
the plants sit on the terrain instead of floating / sinking. Surplus plants and
the 4 stray clusters elsewhere in the map are deleted. Idempotent.

Modes (env PLANT_LAYOUT_MODE):
  probe - read-only: report ground height per slot
  dump  - read-only: report current placement
  apply - reposition, delete surplus, save the level
"""
import io
import os

import unreal

MAP = '/Game/_BLOCKOUT/Kitchen_New'
MODE = os.environ.get('PLANT_LAYOUT_MODE', 'apply').lower()
LOG_PATH = os.path.join(unreal.Paths.project_saved_dir(), 'plant_layout.log')

TOMATO = 'BP_TomatoTree_C'
LETTUCE = 'BP_LettuceTree_C'

CX, CY = 3625.0, 9130.0
ROW_PITCH = 250.0
COL_PITCH = 150.0
COLS = 6
ROW_X = (CX - ROW_PITCH * 0.5, CX + ROW_PITCH * 0.5)
ROW_CLASS = (TOMATO, LETTUCE)
MAIN_RADIUS = 600.0

EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
_LOG = io.open(LOG_PATH, 'w', encoding='utf-8')


def log(msg):
    _LOG.write(str(msg) + '\n')
    _LOG.flush()
    print('[PlantLayout] %s' % msg, flush=True)


def load_map():
    """Load the target map unless it is already the current level (in-editor run)."""
    try:
        cur = world_ctx().get_outermost().get_name()
    except Exception:
        cur = ''
    if cur == os.path.basename(MAP):
        log('already open: %s  mode=%s' % (cur, MODE))
        return
    unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    log('loaded %s  mode=%s' % (MAP, MODE))


def all_trees():
    out = {}
    for a in EAS.get_all_level_actors():
        cn = a.get_class().get_name()
        if cn in (TOMATO, LETTUCE):
            out.setdefault(cn, []).append(a)
    return out


def world_ctx():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def ground_z(x, y, ignore):
    """Landscape height under (x, y); None when the trace misses."""
    hit = unreal.SystemLibrary.line_trace_single(
        world_ctx(),
        unreal.Vector(x, y, 1400.0),
        unreal.Vector(x, y, 300.0),
        unreal.TraceTypeQuery.ECC_VISIBILITY,
        False,
        ignore,
        unreal.DrawDebugTrace.NONE,
    )
    if not hit or not hit.to_tuple()[0]:
        return None
    return hit.to_tuple()[4].z


def bottom_gap(a):
    """Actor origin -> bbox bottom offset. Skeletal bounds change with the pose, so this
    is only used to derive one nominal offset per plant class (see median_gap)."""
    o, e = a.get_actor_bounds(False)
    return a.get_actor_location().z - (o.z - e.z)


def median_gap(acts):
    """One representative origin->base offset per class: a few plants carry an animated
    pose whose bounds dip 15-20 cm lower, using it per actor would float those plants."""
    vals = sorted(bottom_gap(a) for a in acts) or [0.0]
    return vals[len(vals) // 2]


def slots():
    out = []
    for r, cls in enumerate(ROW_CLASS):
        for c in range(COLS):
            out.append((cls, ROW_X[r], CY + (c - (COLS - 1) * 0.5) * COL_PITCH))
    return out


def split_main(trees):
    main, stray = {}, {}
    for cls, acts in trees.items():
        for a in acts:
            l = a.get_actor_location()
            d = ((l.x - CX) ** 2 + (l.y - CY) ** 2) ** 0.5
            (main if d <= MAIN_RADIUS else stray).setdefault(cls, []).append(a)
    return main, stray


def dump(trees):
    for cls, acts in sorted(trees.items()):
        log('%s: %d' % (cls, len(acts)))
        for a in acts:
            l = a.get_actor_location()
            log('   %-30s (%8.1f,%8.1f,%7.1f)' % (a.get_actor_label(), l.x, l.y, l.z))


def trace_ignore():
    """Hit the terrain only: small props (rocks / sacks / crates) inside the plot must not
    lift the plants, otherwise a plant standing on a 30 cm rock looks like it floats."""
    return [a for a in EAS.get_all_level_actors()
            if a.get_class().get_name() != 'Landscape']


def probe():
    trees = all_trees()
    ignore = trace_ignore()
    for cls, x, y in slots():
        gz = ground_z(x, y, ignore)
        log('slot %-18s (%7.1f,%7.1f) ground=%s'
            % (cls, x, y, ('%.1f' % gz) if gz is not None else 'MISS'))


def apply():
    trees = all_trees()
    main, stray = split_main(trees)
    log('before: ' + ', '.join('%s=%d' % (k, len(v)) for k, v in sorted(trees.items())))

    keepers, doomed = {}, []
    n_stray = sum(len(v) for v in stray.values())
    for cls in (TOMATO, LETTUCE):
        pool = sorted(main.get(cls, []), key=lambda a: a.get_actor_location().y)
        keepers[cls] = pool[:COLS]
        doomed += pool[COLS:]
    for acts in stray.values():
        doomed += acts
    log('keep=%d (stray deleted=%d, surplus deleted=%d)'
        % (sum(len(v) for v in keepers.values()), n_stray, len(doomed) - n_stray))

    ignore = trace_ignore()
    gap_of = {cls: median_gap(acts) for cls, acts in keepers.items() if acts}
    misses = 0
    for cls, x, y in slots():
        pool = keepers.get(cls, [])
        if not pool:
            log('  !! no plant left for slot %s (%.0f,%.0f)' % (cls, x, y))
            continue
        a = pool.pop(0)
        gap = gap_of.get(cls, 0.0)
        gz = ground_z(x, y, ignore)
        if gz is None:
            misses += 1
            gz = a.get_actor_location().z - gap
            log('  !! no ground under (%.0f,%.0f), fallback z %.1f' % (x, y, gz))
        a.set_actor_location(unreal.Vector(x, y, gz + gap), False, True)
        a.set_actor_rotation(unreal.Rotator(0.0, 0.0, 0.0), False)
        log('  %-30s -> (%7.1f,%7.1f,%7.1f) ground=%7.1f' % (a.get_actor_label(), x, y, gz + gap, gz))

    for a in doomed:
        if a in ignore:
            ignore.remove(a)
        EAS.destroy_actor(a)

    left = all_trees()
    log('after: ' + ', '.join('%s=%d' % (k, len(v)) for k, v in sorted(left.items())))
    if misses:
        log('WARNING: %d slot(s) had no ground hit' % misses)
    if MODE == 'apply':
        ok = unreal.EditorLoadingAndSavingUtils.save_map(world_ctx(), MAP)
        log('save_map -> %s%s' % (ok, '' if ok else
            '  (关卡文件被占用：编辑器正打开 Kitchen_New，关掉编辑器后重跑即可)'))


load_map()
if MODE == 'dump':
    dump(all_trees())
elif MODE == 'probe':
    probe()
else:
    apply()
_LOG.close()
