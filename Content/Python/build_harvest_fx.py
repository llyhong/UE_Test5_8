import unreal as ue

fx = ue.FXConverterUtilitiesLibrary
category = ue.ScriptExecutionCategory
input_type = ue.NiagaraScriptInputType
asset_path = "/Game/VFX/Pick/NS_Harvest"


def script_args(path, version=None):
    asset = fx.create_asset_data(path)
    assert asset.is_valid(), path
    return ue.CreateScriptContextArgs(asset, version) if version else ue.CreateScriptContextArgs(asset)


def set_input(context, name, value):
    assert context.set_parameter(name, value), name


def attraction_started_input():
    context = fx.create_script_context(script_args("/Niagara/DynamicInputs/Bool/SetBoolByFloatComparison.SetBoolByFloatComparison"))
    set_input(context, "A", fx.create_script_input_linked_parameter("Particles.Age", input_type.FLOAT))
    set_input(context, "B", fx.create_script_input_float(0.34))
    set_input(context, "ComparisonType", fx.create_script_input_enum("/Niagara/Enums/ENiagaraBooleanLogicOps.ENiagaraBooleanLogicOps", "Greater Than Or Equal To"))
    return fx.create_script_input_dynamic(context, input_type.BOOL)


def curve_input(points):
    keys = []
    for time, value in points:
        key = ue.RichCurveKeyBP()
        key.set_editor_property("time", time)
        key.set_editor_property("value", value)
        key.set_editor_property("interp_mode", ue.RichCurveInterpMode.RCIM_LINEAR)
        keys.append(key)
    curve = fx.create_float_curve_di(keys)
    context = fx.create_script_context(script_args("/Niagara/DynamicInputs/ValueFromCurve/FloatFromCurve.FloatFromCurve", [1, 0]))
    set_input(context, "FloatCurve", fx.create_script_input_di(curve))
    set_input(context, "CurveIndex", fx.create_script_input_linked_parameter("Particles.Age", input_type.FLOAT))
    return fx.create_script_input_dynamic(context, input_type.FLOAT)


def target_vector_input():
    context = fx.create_script_context(script_args("/Niagara/DynamicInputs/Transforms/ConvertPositionToVector.ConvertPositionToVector"))
    set_input(context, "Input Position", fx.create_script_input_linked_parameter("User.TargetPosition", input_type.POSITION))
    return fx.create_script_input_dynamic(context, input_type.VEC3)


def build():
    assert not ue.EditorAssetLibrary.does_asset_exist(asset_path), asset_path
    system = ue.AssetToolsHelpers.get_asset_tools().create_asset("NS_Harvest", "/Game/VFX/Pick", ue.NiagaraSystem, ue.NiagaraSystemFactoryNew())
    assert system
    context = fx.create_system_conversion_context(system)
    try:
        emitter = context.add_empty_emitter("HarvestMotes")
        emitter.set_local_space(False)
        emitter.set_sim_target(ue.NiagaraSimTarget.CPU_SIM)

        def module(name, path, stage, version=None):
            return emitter.find_or_add_module_script(name, script_args(path, version), stage)

        state = module("EmitterState", "/Niagara/Modules/Emitter/EmitterState.EmitterState", category.EMITTER_UPDATE, [1, 0])
        set_input(state, "Life Cycle Mode", fx.create_script_input_enum("/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode", "Self"))
        set_input(state, "Loop Behavior", fx.create_script_input_enum("/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions", "Once"))
        set_input(state, "Loop Duration", fx.create_script_input_float(1.1))
        set_input(state, "Inactive Response", fx.create_script_input_enum("/Niagara/Enums/ENiagaraInactiveMode.ENiagaraInactiveMode", "Complete (Let Particles Finish then Kill Emitter)"))

        burst = module("HarvestBurst", "/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous", category.EMITTER_UPDATE, [1, 1])
        set_input(burst, "Spawn Count", fx.create_script_input_linked_parameter("User.ParticleCount", input_type.INT))
        set_input(burst, "Spawn Time", fx.create_script_input_float(0.0))
        initialize = module("InitializeParticle", "/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle.InitializeParticle", category.PARTICLE_SPAWN)
        set_input(initialize, "Lifetime", fx.create_script_input_float(1.1))
        set_input(initialize, "Color Mode", fx.create_script_input_enum("/Niagara/Enums/ENiagara_ColorInitializationMode.ENiagara_ColorInitializationMode", "Direct Set"))
        set_input(initialize, "Color", fx.create_script_input_linear_color(ue.LinearColor(1.0, 0.65, 0.12, 1.0)))
        set_input(initialize, "Sprite Size Mode", fx.create_script_input_enum("/Niagara/Enums/ENiagara_SizeScaleMode.ENiagara_SizeScaleMode", "Uniform"))
        set_input(initialize, "Uniform Sprite Size", fx.create_script_input_float(14.0))

        random_velocity = fx.create_script_context(script_args("/Niagara/DynamicInputs/UniformRange/V2/RandomRangeVector.RandomRangeVector"))
        set_input(random_velocity, "Minimum", fx.create_script_input_vector(ue.Vector(-140.0, -140.0, 430.0)))
        set_input(random_velocity, "Maximum", fx.create_script_input_vector(ue.Vector(140.0, 140.0, 500.0)))
        velocity = module("UpwardLaunch", "/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity", category.PARTICLE_SPAWN, [1, 2])
        set_input(velocity, "Velocity", fx.create_script_input_dynamic(random_velocity, input_type.VEC3))
        set_input(velocity, "Rotation Coordinate Space", fx.create_script_input_enum("/Niagara/Enums/ENiagaraCoordinateSpace.ENiagaraCoordinateSpace", "World"))

        module("ParticleState", "/Niagara/Modules/Update/Lifetime/ParticleState.ParticleState", category.PARTICLE_UPDATE, [1, 1])
        gravity = module("SnappyGravity", "/Niagara/Modules/Update/Forces/GravityForce.GravityForce", category.PARTICLE_UPDATE)
        set_input(gravity, "Gravity", fx.create_script_input_vector(ue.Vector(0.0, 0.0, -3500.0)))
        attraction = module("SocketAttraction", "/Niagara/Modules/Update/Forces/PointAttractionForce.PointAttractionForce", category.PARTICLE_UPDATE)
        set_input(attraction, "AttractorPosition", target_vector_input())
        set_input(attraction, "AttractionStrength", curve_input([(0.0, 0.0), (0.32, 0.0), (0.34, 18000.0), (1.1, 18000.0)]))
        set_input(attraction, "Attraction Radius", fx.create_script_input_float(1500.0))
        set_input(attraction, "Falloff Exponent", fx.create_script_input_float(0.1))
        set_input(attraction, "Kill Within Radius", fx.create_script_input_bool(False))
        drag = module("AttractionDamping", "/Niagara/Modules/Update/Forces/Drag.Drag", category.PARTICLE_UPDATE)
        set_input(drag, "Drag", curve_input([(0.0, 0.0), (0.32, 0.0), (0.34, 8.5), (1.1, 8.5)]))
        module("SolveForcesAndVelocity", "/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity", category.PARTICLE_UPDATE)
        arrival = module("SocketArrival", "/Niagara/Modules/Update/Lifetime/KillParticlesInVolume.KillParticlesInVolume", category.PARTICLE_UPDATE)
        set_input(arrival, "Kill Shape", fx.create_script_input_enum("/Niagara/Enums/ENiagaraKillVolumeOptions.ENiagaraKillVolumeOptions", "Sphere"))
        set_input(arrival, "Volume Origin", target_vector_input())
        set_input(arrival, "Sphere Radius", fx.create_script_input_float(24.0))
        set_input(arrival, "Kill Volume Enabled", attraction_started_input())
        renderer = ue.NiagaraSpriteRendererProperties()
        renderer.set_editor_property("material", ue.load_asset("/Niagara/DefaultAssets/DefaultSpriteMaterial"))
        emitter.add_renderer("HarvestSprite", renderer)
        system.set_editor_property("require_current_frame_data", True)
        context.finalize()
        assert ue.EditorAssetLibrary.save_loaded_asset(system, False)
        print("HARVEST_FX_BUILT", asset_path)
    finally:
        context.cleanup()


build()
if "-HarvestFxBuildAndQuit" in ue.SystemLibrary.get_command_line():
    ue.SystemLibrary.quit_editor()
