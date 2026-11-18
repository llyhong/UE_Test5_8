"""Build a separate title-screen diorama. Run in a dedicated editor process."""
import math
import unreal

MAP='/Game/_Demo/Map/L_StartMenu'
ROOT='/Game/UI/StartMenu'
E=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
L=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
A=unreal.EditorAssetLibrary
if A.does_asset_exist(MAP):
    assert L.load_level(MAP)
    assert len(E.get_all_level_actors()) <= 2, 'Refusing to rebuild a populated start scene'
else:
    assert L.new_level(MAP,False)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode',unreal.load_class(None,ROOT+'/BP_StartMenuGameMode.BP_StartMenuGameMode_C'))

def vec(v): return unreal.Vector(*v)
def rot(v): return unreal.Rotator(pitch=v[0],yaw=v[1],roll=v[2])
def material(name,rgb):
    if A.does_asset_exist(ROOT+'/Materials/'+name):
        return unreal.load_asset(ROOT+'/Materials/'+name)
    m=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,ROOT+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    c=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionConstant3Vector,-300,0)
    c.set_editor_property('constant',unreal.LinearColor(*rgb))
    unreal.MaterialEditingLibrary.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
    rough=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionConstant,-300,140)
    rough.set_editor_property('r',.78)
    unreal.MaterialEditingLibrary.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(m)
    A.save_loaded_asset(m)
    return m

M={
    'cream':material('M_Title_Plaster',(.64,.54,.36)),
    'wood':material('M_Title_Oak',(.25,.105,.041)),
    'dark':material('M_Title_Walnut',(.055,.026,.014)),
    'sage':material('M_Title_Sage',(.105,.18,.09)),
    'tile':material('M_Title_Tile',(.38,.36,.25)),
    'soil':material('M_Title_Soil',(.055,.033,.012)),
    'ground':material('M_Title_Ground',(.065,.105,.052)),
    'brass':material('M_Title_Brass',(.55,.27,.065)),
}
CUBE=unreal.load_asset('/Engine/BasicShapes/Cube')
def cube(name,p,size,mat,angle=0):
    a=E.spawn_actor_from_class(unreal.StaticMeshActor,vec(p),rot((0,angle,0)))
    a.set_actor_label(name)
    a.set_folder_path('Title Set/Architecture')
    a.static_mesh_component.set_static_mesh(CUBE)
    a.static_mesh_component.set_material(0,M[mat])
    a.set_actor_scale3d(vec([s/100 for s in size]))
    return a

def prop(name,path,x,y,z=0,width=100,yaw=0):
    mesh=unreal.load_asset(path)
    assert isinstance(mesh,unreal.StaticMesh),(name,path)
    bounds=mesh.get_bounding_box()
    size=bounds.max-bounds.min
    s=width/max(size.x,size.y)
    a=E.spawn_actor_from_class(unreal.StaticMeshActor,vec((x,y,z-bounds.min.z*s)),rot((0,yaw,0)))
    a.set_actor_label(name)
    a.set_folder_path('Title Set/Props')
    a.static_mesh_component.set_static_mesh(mesh)
    a.set_actor_scale3d(vec((s,s,s)))
    return a

V='/Game/Fantastic_Village_Pack/meshes/props'
K='/Game/_LOCAL/KitchenAssets/Meshes'
P='/Game/_LOCAL/Props_Whitebox'

# A compact architectural set, rather than a duplicate of the gameplay world.
cube('Meadow', (0,0,-62),(200000,200000,40),'ground')
cube('Raised foundation',(0,35,-20),(1280,1010,40),'dark')
for i in range(17):
    cube('Oak floor %02d'%i,(-596+i*74,35,3),(70,1000,12),'wood')
for row in range(4):
    for col in range(10):
        cube('Kitchen stone %02d_%02d'%(row,col),(-470+col*105,195+row*75,13),(101,71,10),'tile' if (row+col)%2 else 'cream')
cube('Back plaster wall',(0,490,190),(1240,24,360),'cream')
cube('Left plaster wall',(-610,265,190),(24,450,360),'cream')
cube('Back wainscot',(0,472,75),(1240,16,125),'sage')
cube('Left wainscot',(-592,265,75),(16,450,125),'sage')
for x in (-610,-205,205,610):
    cube('Back timber post '+str(x),(x,461,197),(24,35,385),'dark')
cube('Back cornice',(0,465,385),(1270,45,28),'dark')
cube('Chair rail',(0,453,139),(1240,20,15),'wood')
cube('Left cornice',(-610,265,385),(40,470,28),'dark')
cube('Service step',(0,-500,-2),(430,90,22),'tile')

prop('Kitchen stove',K+'/SM_Stove',150,360,23,130,180)
prop('Kitchen sink',K+'/SM_Sink_001',320,360,110,135,180)
cube('Sink base cabinet',(320,360,64),(135,80,82),'sage')
cube('Sink counter rim',(320,360,108),(145,88,8),'cream')
prop('Kitchen prep',P+'/SM_PrepTable',-65,363,23,145,180)
prop('Sage refrigerator',P+'/SM_Fridge',-450,375,23,120,180)
prop('Produce cupboard',K+'/SM_Kitchen_Cabin',-270,365,23,135,180)
prop('Pot on range',K+'/SM_Pot',150,365,126,58)
prop('Knife board',K+'/SM_Cutting_Board',-65,360,125,60)
prop('Counter island',P+'/SM_BarCounter',175,110,15,320,180)
prop('Island chopping board',K+'/SM_Cutting_Board',135,100,125,64)
prop('Island pot',K+'/SM_Pot',250,110,125,58)

for n,x in enumerate((-255,260)):
    prop('Dining table '+str(n),P+'/SM_DiningTable',x,-240,12,180)
    for side in (-1,1):
        prop('Dining chair %d_%d'%(n,side),P+'/SM_Chair',x+side*125,-240,12,62,-90 if side<0 else 90)
    prop('Table plate '+str(n),K+'/SM_Plate',x,-248,116,35)
    prop('Table herb '+str(n),V+'/natural/SM_PROP_PLANT_pot_v1_02',x+40,-218,116,28)

# Shelves, pantry containers, produce and hanging plants give the set its identity.
for z in (220,300):
    cube('Pantry shelf '+str(z),(280,418,z),(455,65,12),'wood')
    for x in (90,450):
        cube('Shelf bracket %d_%d'%(x,z),(x,445,z-25),(14,20,50),'dark')
for i in range(5):
    prop('Shelf plate '+str(i),K+'/SM_Plate',120+i*19,421,228+i*2,38)
for i in range(3):
    prop('Shelf herb '+str(i),V+'/natural/SM_PROP_PLANT_pot_v1_02',190+i*105,420,307,42)
for i,x in enumerate((-405,-280,-155)):
    prop('Fresh produce basket '+str(i),K+'/SM_Ingredients_Box',x,70,13,100)
    for j in range(3):
        fruit=('tomato','pumpkin_01','salad')[i]
        prop('Harvest %d_%d'%(i,j),V+'/food/SM_PROP_food_'+fruit,x-27+j*25,65,64,27)
for x,y in [(-565,-370),(560,-355),(565,395)]:
    prop('Potted garden %d'%x+'_'+str(y),V+'/natural/SM_PROP_PLANT_pot_v3_01',x,y,12,110)

# An open pergola keeps the kitchen readable and casts a welcoming rhythm of shadows.
for x in (-555,560):
    cube('Pergola post '+str(x),(x,-395,200),(22,22,390),'dark')
    cube('Pergola side '+str(x),(x,-145,400),(24,535,25),'wood')
cube('Pergola front beam',(0,-395,400),(1160,26,26),'dark')
for i in range(7):
    cube('Pergola slat '+str(i),(-555+i*185,-140,421),(24,550,18),'wood')
for x in (-490,-305,435):
    prop('Pergola greenery '+str(x),V+'/natural/SM_PROP_PLANT_pot_v1_02',x,-270,426,92)

for i,x in enumerate((-450,-150,150,450)):
    cube('Lantern cord '+str(i),(x,-390,352),(3,3,76),'dark')
    cube('Lantern frame '+str(i),(x,-390,308),(35,35,44),'brass')
    light=E.spawn_actor_from_class(unreal.PointLight,vec((x,-370,300)))
    light.set_actor_label('Warm lantern '+str(i))
    light.set_folder_path('Title Set/Lighting')
    lc=light.point_light_component
    lc.set_mobility(unreal.ComponentMobility.MOVABLE)
    lc.set_intensity(350)
    lc.set_light_color(unreal.LinearColor(1,.65,.28))
    lc.set_attenuation_radius(280)

sun=E.spawn_actor_from_class(unreal.DirectionalLight,vec((0,0,800)),rot((-35,-45,0)))
sun.set_actor_label('Late afternoon sun')
suncomp=sun.get_component_by_class(unreal.DirectionalLightComponent)
suncomp.set_mobility(unreal.ComponentMobility.MOVABLE)
suncomp.set_intensity(4)
suncomp.set_light_color(unreal.LinearColor(1,.84,.61))
suncomp.set_editor_property('atmosphere_sun_light',True)
suncomp.set_editor_property('forward_shading_priority',1)
fill=E.spawn_actor_from_class(unreal.DirectionalLight,vec((0,0,700)),rot((-65,135,0)))
fill.set_actor_label('Soft sky fill')
fill.set_folder_path('Title Set/Lighting')
fc=fill.get_component_by_class(unreal.DirectionalLightComponent)
fc.set_mobility(unreal.ComponentMobility.MOVABLE)
fc.set_intensity(2.2)
fc.set_cast_shadows(False)
fc.set_light_color(unreal.LinearColor(.72,.84,1))
sky=E.spawn_actor_from_class(unreal.SkyLight,vec((0,0,500)))
skycomp=sky.get_component_by_class(unreal.SkyLightComponent)
skycomp.set_mobility(unreal.ComponentMobility.MOVABLE)
skycomp.set_editor_property('real_time_capture',True)
skycomp.set_intensity(1.1)
E.spawn_actor_from_class(unreal.SkyAtmosphere,vec((0,0,0)))
fog=E.spawn_actor_from_class(unreal.ExponentialHeightFog,vec((0,0,-60)))
fog.get_component_by_class(unreal.ExponentialHeightFogComponent).set_editor_property('fog_density',.008)

camera=E.spawn_actor_from_class(unreal.CameraActor,vec((2050,-1700,1530)))
camera.set_actor_label('Title Camera')
aim=unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),vec((175,360,170)))
camera.set_actor_rotation(aim,False)
camera.camera_component.set_field_of_view(48)
camera.camera_component.set_editor_property('constrain_aspect_ratio',False)
post=unreal.PostProcessSettings()
for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,'override_auto_exposure_bias':True,'auto_exposure_bias':1.2,'override_vignette_intensity':True,'vignette_intensity':.3,'override_bloom_intensity':True,'bloom_intensity':.2}.items():
    post.set_editor_property(key,value)
camera.camera_component.set_editor_property('post_process_settings',post)
post.set_editor_property('override_auto_exposure_apply_physical_camera_exposure',True)
post.set_editor_property('auto_exposure_apply_physical_camera_exposure',False)
post.set_editor_property('auto_exposure_bias',0.0)
camera.camera_component.set_editor_property('post_process_settings',post)
camera.camera_component.set_editor_property('post_process_blend_weight',1)
unreal.EditorLevelLibrary.set_level_viewport_camera_info(camera.get_actor_location(),camera.get_actor_rotation())
assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
print('START_SCENE_READY',MAP,'actors',len(E.get_all_level_actors()))
