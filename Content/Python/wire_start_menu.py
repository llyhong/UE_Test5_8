"""Wire the title screen with editable Blueprint functions and button events."""
import json
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as B, _BlueprintCache

ROOT='/Game/UI/StartMenu'
bp=unreal.load_asset(ROOT+'/WBP_StartMenu')
g=B.get_graph(bp,'EventGraph')
_BlueprintCache.invalidate(bp)

def function(name,body):
    graph=B.add_function_graph(bp,name)
    B.write_graph_dsl(graph,'(fn '+name+' ()\n'+body+'\n)')
    B.compile_blueprint(bp)
    _BlueprintCache.invalidate(bp)
    return graph

def getter(name):
    matches=list(B.find_node_types(g,'Get'+name))
    assert len(matches)==1,(name,matches)
    return '('+matches[0]+')'

function('EnterKitchen', '''
  (Game|SetGamePaused :bPaused false)
  (bind pc (Widget|GetOwningPlayer))
  (Input|SetInputModeGameOnly :PlayerController pc :bFlushInput true)
  (Class|PlayerController|SetShowMouseCursor :self pc :bShowMouseCursor false)
  (Widget|RemovefromParent :self self)
  (Game|OpenLevel(byName) :LevelName "/Game/_BLOCKOUT/Kitchen_New" :bAbsolute true)
''')
enter=list(B.find_node_types(g,'EnterKitchen'))[0]
function('StartJourney','''
  (bind gi (Utilities|Casting|CastToBaseGameInstance :Object (Game|GetGameInstance))
    (:then
      (if (SaveGame|DoesSaveGameExist :SlotName "SaveSlot_01" :UserIndex 0)
        (Shoot|Gameplay|LoadGameData :self gi)
        (else (Shoot|Gameplay|NewGameData :self gi)))
      ('''+enter+'''))
    (:CastFailed))
''')
function('ShowSettings','''
  (Widget|SetVisibility :self '''+getter('SettingsLayer')+''' :InVisibility "Visible")
  (Widget|SetUserFocus :self '''+getter('BackButton')+''' :PlayerController (Widget|GetOwningPlayer))
''')
function('HideSettings','''
  (Widget|SetVisibility :self '''+getter('SettingsLayer')+''' :InVisibility "Collapsed")
  (Widget|SetUserFocus :self '''+getter('StartButton')+''' :PlayerController (Widget|GetOwningPlayer))
''')
function('ExitGame','(Game|QuitGame :SpecificPlayer (Widget|GetOwningPlayer) :QuitPreference "Quit" :bIgnorePlatformRestrictions false)')
for name,value in [('UseLowQuality',0),('UseMediumQuality',1),('UseHighQuality',3)]:
    function(name,f'''(bind s (Settings|GetGameUserSettings))
      (Settings|SetOverallScalabilityLevel :self s :Value {value})
      (Settings|ApplySettings :self s :bCheckForCommandLineOverrides false)''')
for name,mode in [('UseWindowed','Windowed'),('UseFullscreen','WindowedFullscreen')]:
    function(name,f'''(bind s (Settings|GetGameUserSettings))
      (Settings|SetFullscreenMode :self s :InFullscreenMode "{mode}")
      (Settings|ApplySettings :self s :bCheckForCommandLineOverrides false)''')

bindings={'StartButton':'StartJourney','SettingsButton':'ShowSettings','QuitButton':'ExitGame',
          'BackButton':'HideSettings','QualityLow':'UseLowQuality','QualityMedium':'UseMediumQuality',
          'QualityHigh':'UseHighQuality','WindowedButton':'UseWindowed','FullscreenButton':'UseFullscreen'}
for n in list(B.find_nodes(g)):
    if not isinstance(n,unreal.K2Node_Event):
        B.delete_node(n)
for name,handler in bindings.items():
    event=next(n for n in B.find_nodes(g) if n.get_node_title()=='On Clicked ('+name+')')
    event_info=B.get_node_infos([event])[0]
    type_id=next(t for t in B.find_node_types(g,handler) if t.split('|')[-1].lower()==handler.lower())
    node=B.create_node(g,type_id,unreal.IntPoint(420,event_info.position.y))
    call=B.get_node_infos([node])[0]
    B.connect_pins(next(p.pin_id for p in event_info.output_pins if p.type_id=='Exec'),next(p.pin_id for p in call.input_pins if p.type_id=='Exec'))
B.compile_blueprint(bp)
unreal.get_default_object(bp.generated_class()).set_editor_property('is_focusable',True)
unreal.EditorAssetLibrary.save_loaded_asset(bp)

pc=unreal.load_asset(ROOT+'/BP_StartMenuController')
pg=B.get_graph(pc,'EventGraph')
for n in list(B.find_nodes(pg)):
    B.delete_node(n)
B.write_graph_dsl(pg,'''(event EventBeginPlay
  (Variables|PlayerController|SetShouldPerformFullTickwhenPaused true)
  (bind camera (Actor|GetActorOfClass :ActorClass "/Script/Engine.CameraActor"))
  (Game|Player|SetViewTargetwithBlend :NewViewTarget camera :BlendTime 0.0)
  (bind menu (UserInterface|CreateWidget :Class "/Game/UI/StartMenu/WBP_StartMenu.WBP_StartMenu_C" :OwningPlayer self))
  (UserInterface|Viewport|AddtoViewport :self menu :ZOrder 10)
  (Variables|MouseInterface|SetShowMouseCursor true)
  (Input|SetInputModeUIOnly :PlayerController self :InWidgetToFocus menu :InMouseLockMode "DoNotLock" :bFlushInput true)
  (Game|SetGamePaused :bPaused true)
)''')
B.compile_blueprint(pc)
unreal.get_default_object(pc.generated_class()).set_editor_property('should_perform_full_tick_when_paused',True)
unreal.EditorAssetLibrary.save_loaded_asset(pc)
gm_path=ROOT+'/BP_StartMenuGameMode'
gm=unreal.load_asset(gm_path)
if not gm:
    gm=B.create(ROOT,'BP_StartMenuGameMode',unreal.GameModeBase.static_class())
B.compile_blueprint(gm)
cdo=unreal.get_default_object(gm.generated_class())
cdo.set_editor_property('player_controller_class',pc.generated_class())
cdo.set_editor_property('default_pawn_class',None)
cdo.set_editor_property('hud_class',None)
B.compile_blueprint(gm)
unreal.EditorAssetLibrary.save_loaded_asset(gm)
print('Compiled menu widget, controller, and game mode.')
