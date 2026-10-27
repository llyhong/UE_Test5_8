# -*- coding: utf-8 -*-
import os
import unreal

OUT = 'C:/lly_UE_Proj/Ori5_8/Saved/GridShots/'
for name in ('RT_GridTop', 'RT_GridEye', 'RT_GridFarm'):
    rt = unreal.load_object(None, '/Game/_ReviewTemp/%s.%s' % (name, name))
    unreal.RenderingLibrary.export_render_target(
        None, rt, OUT, 'Grid_%s.png' % name.split('_')[-1])
for f in sorted(os.listdir(OUT)):
    print(f, os.path.getsize(os.path.join(OUT, f)))
