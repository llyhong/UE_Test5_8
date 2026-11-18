"""Build the independent farmhouse title screen assets. Run in the UE editor."""
import json
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as B
from editor_toolset.toolsets.object import ObjectTools as O

ROOT = '/Game/UI/StartMenu'
MAP = '/Game/_Demo/Map/L_StartMenu'

def ref(obj):
    return {'refPath': obj.get_path_name()} if obj else None

def umg(name, **args):
    r = unreal.ToolsetRegistry.execute_tool('UMGToolSet.UMGToolSet', name, json.dumps(args))
    assert r.is_complete, 'Expected synchronous UMG operation'
    assert not r.error, r.error
    return json.loads(r.value)['returnValue']

def resolve(value):
    return unreal.find_object(None, value['refPath']) if isinstance(value, dict) else None

def color(r,g,b,a=1):
    return unreal.LinearColor(r,g,b,a)

CREAM = color(.94,.88,.73)
GOLD = color(.76,.47,.19)
GREEN = color(.023,.062,.047)
MUTED = color(.44,.56,.42)

bp = unreal.load_asset(ROOT+'/WBP_StartMenu')
assert bp, 'Create WBP_StartMenu first'
existing = umg('GetWidgets',widgetBlueprint=ref(bp))['widgets']
assert not existing, 'Refusing to rebuild an existing widget tree'

def widget(cls,name,parent=None):
    info=umg('AddWidget',widgetBlueprint=ref(bp),widgetClass=ref(cls.static_class()),widgetDisplayName=name,parentWidget=ref(parent),childIndex=-1)
    w=resolve(info['widget'])
    assert w, info
    O.list_properties(w)
    if w.slot:
        O.list_properties(w.slot)
    return w

def place(w,x,y,width,height):
    w.slot.set_position(unreal.Vector2D(x,y))
    w.slot.set_size(unreal.Vector2D(width,height))
    return w

def brush(c):
    b=unreal.SlateBrush()
    b.set_editor_property('draw_as',unreal.SlateBrushDrawType.ROUNDED_BOX)
    b.set_editor_property('tint_color',unreal.SlateColor(specified_color=c))
    outline=b.get_editor_property('outline_settings')
    outline.set_editor_property('rounding_type',unreal.SlateBrushRoundingType.FIXED_RADIUS)
    outline.set_editor_property('corner_radii',unreal.Vector4(8,8,8,8))
    b.set_editor_property('outline_settings',outline)
    return b

root=widget(unreal.CanvasPanel,'Root')
scale=widget(unreal.ScaleBox,'ResponsiveFrame',root)
scale.slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0,0),maximum=unreal.Vector2D(1,1)))
scale.slot.set_offsets(unreal.Margin(0,0,0,0))
scale.set_stretch(unreal.Stretch.SCALE_TO_FIT)
frame=widget(unreal.SizeBox,'DesignSize',scale)
frame.set_width_override(1920)
frame.set_height_override(1080)
canvas=widget(unreal.CanvasPanel,'Composition',frame)

def panel(name,x,y,w,h,c,parent=canvas):
    obj=place(widget(unreal.Border,name,parent),x,y,w,h)
    obj.set_brush_color(c)
    return obj

def text(name,value,x,y,w,h,size=24,c=CREAM,parent=canvas):
    obj=place(widget(unreal.TextBlock,name,parent),x,y,w,h)
    obj.set_text(value)
    fi=obj.get_editor_property('font')
    fi.set_editor_property('font_object',unreal.load_asset('/Engine/EngineFonts/Roboto'))
    fi.set_editor_property('typeface_font_name','Regular')
    fi.set_editor_property('size',size)
    obj.set_font(fi)
    obj.set_color_and_opacity(unreal.SlateColor(specified_color=c))
    obj.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    return obj

def button(name,label,x,y,w=390,h=72,primary=False,parent=canvas):
    obj=place(widget(unreal.Button,name,parent),x,y,w,h)
    style=obj.get_editor_property('widget_style')
    style.set_editor_property('normal',brush(GOLD if primary else color(.065,.12,.092)))
    style.set_editor_property('hovered',brush(color(.92,.65,.30) if primary else color(.13,.22,.15)))
    style.set_editor_property('pressed',brush(color(.55,.32,.10) if primary else color(.025,.065,.04)))
    obj.set_style(style)
    labelw=widget(unreal.TextBlock,name+'Label',obj)
    labelw.set_text(label)
    fi=labelw.get_editor_property('font')
    fi.set_editor_property('font_object',unreal.load_asset('/Engine/EngineFonts/Roboto'))
    fi.set_editor_property('size',27 if primary else 24)
    labelw.set_font(fi)
    labelw.set_color_and_opacity(unreal.SlateColor(specified_color=GREEN if primary else CREAM))
    obj.slot.set_z_order(2)
    umg('ToggleWidgetAsVariable',widgetBlueprint=ref(bp),widget=ref(obj),bIsVariable=True)
    return obj

panel('LeftBackdrop',0,0,610,1080,color(.018,.045,.032,.97))
panel('GoldRule',606,0,4,1080,color(.70,.44,.16,.7))
text('Eyebrow','F A R M   •   K I T C H E N',88,112,460,40,20,GOLD)
text('Title','田园食光',78,180,500,115,70)
text('Tagline','从一片菜园，到一桌热腾腾的好味道。',88,307,465,70,21,MUTED)
panel('TitleRule',88,399,66,3,GOLD)
text('Welcome','今天，也从一顿好饭开始。',88,432,450,40,23)
start=button('StartButton','开始游戏  →',88,522,420,80,True)
settings=button('SettingsButton','设置',88,628,420)
quitb=button('QuitButton','退出游戏',88,726,420)
text('SaveHint','有存档时自动继续，无存档时开启新旅程。',88,832,460,55,18,MUTED)
text('Footer','种植  /  烹饪  /  招待每一位来客',88,985,470,36,17,MUTED)
text('SceneCaption','一方小院 · 一间小店',1355,947,500,50,27,CREAM)
text('SceneCaptionSmall','把平凡的日子，煮成喜欢的味道。',1355,998,500,35,18,CREAM)

settings_layer=place(widget(unreal.CanvasPanel,'SettingsLayer',canvas),0,0,1920,1080)
settings_layer.slot.set_z_order(10)
umg('ToggleWidgetAsVariable',widgetBlueprint=ref(bp),widget=ref(settings_layer),bIsVariable=True)
panel('ModalScrim',0,0,1920,1080,color(.005,.015,.008,.82),settings_layer)
panel('SettingsCard',630,205,690,670,color(.035,.08,.055),settings_layer)
text('SettingsTitle','设置',685,250,570,65,40,parent=settings_layer)
text('QualityTitle','画面品质',685,348,570,36,23,parent=settings_layer)
text('QualityHint','选择后立即应用并保存',685,390,570,35,18,MUTED,settings_layer)
buttons={
    'StartButton':start,'SettingsButton':settings,'QuitButton':quitb,
    'QualityLow':button('QualityLow','流畅',685,452,170,65,parent=settings_layer),
    'QualityMedium':button('QualityMedium','均衡',880,452,170,65,parent=settings_layer),
    'QualityHigh':button('QualityHigh','精致',1075,452,170,65,parent=settings_layer),
    'WindowedButton':button('WindowedButton','窗口模式',685,568,267,65,parent=settings_layer),
    'FullscreenButton':button('FullscreenButton','无边框全屏',978,568,267,65,parent=settings_layer),
    'BackButton':button('BackButton','返回',685,730,560,72,True,settings_layer),
}
text('SettingsNote','画质与显示模式只影响画面表现。',685,666,570,35,18,MUTED,settings_layer)
settings_layer.set_visibility(unreal.SlateVisibility.COLLAPSED)
assert umg('CompileWidgetBlueprint',widgetBlueprint=ref(bp))
for name,obj in buttons.items():
    assert umg('BindToEventProperty',widgetBlueprint=ref(bp),eventName='OnClicked',propertyName=name,propertyClass=ref(unreal.Button.static_class()))
unreal.EditorAssetLibrary.save_loaded_asset(bp)
print('UI built',len(umg('GetWidgets',widgetBlueprint=ref(bp))['widgets']))
graph=B.get_graph(bp,'EventGraph')
print('EVENTS',[(n.get_name(),n.get_node_title()) for n in B.find_nodes(graph)])
print('GETTERS',{t:list(B.find_node_types(graph,t)) for t in ('GetStartButton','GetSettingsLayer','GetBackButton','GetOwningPlayer','RemovefromParent','SetIsEnabled')})
print('DIRTY_MAPS',[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
