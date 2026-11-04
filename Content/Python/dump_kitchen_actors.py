# -*- coding: utf-8 -*-
"""Dump every actor of Kitchen_New to Saved/kitchen_actors.json (analysis helper)."""
import json
import os

import unreal

MAP = '/Game/_BLOCKOUT/Kitchen_New'
OUT = os.path.join(unreal.Paths.project_saved_dir(), 'kitchen_actors.json')

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

rows = []
for a in EAS.get_all_level_actors():
    try:
        l = a.get_actor_location()
        r = a.get_actor_rotation()
        s = a.get_actor_scale3d()
        o, e = a.get_actor_bounds(False)
        rows.append({
            'label': a.get_actor_label(),
            'cls': a.get_class().get_name(),
            'x': round(l.x, 1), 'y': round(l.y, 1), 'z': round(l.z, 1),
            'yaw': round(r.yaw, 1),
            'sx': round(s.x, 3), 'sy': round(s.y, 3), 'sz': round(s.z, 3),
            'bx': round(o.x, 1), 'by': round(o.y, 1), 'bz': round(o.z, 1),
            'ex': round(e.x, 1), 'ey': round(e.y, 1), 'ez': round(e.z, 1),
        })
    except Exception as exc:  # noqa
        rows.append({'label': a.get_actor_label(), 'cls': 'ERR', 'err': str(exc)[:80]})

with open(OUT, 'w', encoding='utf-8') as f:
    json.dump(rows, f, ensure_ascii=False)
print('[Dump] %d actors -> %s' % (len(rows), OUT), flush=True)
