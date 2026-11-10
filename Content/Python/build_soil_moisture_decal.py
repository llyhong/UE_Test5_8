import unreal

CROPS = '/Game/_LOCAL/Crops'
DECAL = CROPS + '/M_Decal_SoilMoisture'

SCALARS = [
    ('SoilWetness', 0.0),
    ('CrackSeed', 0.0),
    ('PatchRadius', 0.94),
    ('PatchSoftness', 0.16),
    ('EdgeNoise', 0.12),
    ('DustAmount', 0.4),
    ('CrackCells', 3.5),
    ('CrackWidth', 0.055),
    ('CrackDarken', 0.15),
    ('DryRoughness', 1.0),
    ('WetDarken', 0.28),
    ('WetRoughness', 0.35),
    ('SoakSoftness', 0.25),
]
VECTORS = [
    ('DustColor', unreal.LinearColor(0.6, 0.42, 0.26, 1.0)),
    ('WetColor', unreal.LinearColor(0.0, 0.0, 0.0, 1.0)),
]

HLSL = r'''
float2 c = UV - 0.5;
float d = length(c) * 2.0;
float s = frac(CrackSeed * 0.1731) * 6.2831;
float ang = atan2(c.y, c.x);
float en = sin(ang * 3.0 + s) * 0.5 + sin(ang * 7.0 + s * 1.7) * 0.3 + sin(ang * 13.0 + s * 2.3) * 0.2;
d *= 1.0 + en * EdgeNoise;
float mask = 1.0 - smoothstep(PatchRadius - PatchSoftness, PatchRadius, d);

// Voronoi edge distance (two-pass) -> polygonal desiccation cracks
float2 p = UV * max(CrackCells, 0.5) + frac(CrackSeed * float2(0.1731, 0.2937)) * 97.0;
float2 n = floor(p);
float2 f = frac(p);
float2 mg = float2(0, 0);
float2 mr = float2(0, 0);
float md = 8.0;
for (int j = -1; j <= 1; j++)
{
    for (int i = -1; i <= 1; i++)
    {
        float2 g = float2(i, j);
        float2 cc = n + g;
        float2 o = frac(sin(float2(dot(cc, float2(127.1, 311.7)), dot(cc, float2(269.5, 183.3)))) * 43758.5453);
        float2 r = g + o - f;
        float dd = dot(r, r);
        if (dd < md) { md = dd; mr = r; mg = g; }
    }
}
float edge = 8.0;
for (int j2 = -2; j2 <= 2; j2++)
{
    for (int i2 = -2; i2 <= 2; i2++)
    {
        float2 g = mg + float2(i2, j2);
        float2 cc = n + g;
        float2 o = frac(sin(float2(dot(cc, float2(127.1, 311.7)), dot(cc, float2(269.5, 183.3)))) * 43758.5453);
        float2 r = g + o - f;
        float2 dr = r - mr;
        if (dot(dr, dr) > 1e-5) { edge = min(edge, dot(0.5 * (mr + r), normalize(dr))); }
    }
}

// Low-frequency value noise: varies crack width and dust so not every border reads the same
float2 q = p * 0.6;
float2 qi = floor(q);
float2 qf = frac(q);
qf = qf * qf * (3.0 - 2.0 * qf);
float h00 = frac(sin(dot(qi, float2(12.9898, 78.233))) * 43758.5453);
float h10 = frac(sin(dot(qi + float2(1, 0), float2(12.9898, 78.233))) * 43758.5453);
float h01 = frac(sin(dot(qi + float2(0, 1), float2(12.9898, 78.233))) * 43758.5453);
float h11 = frac(sin(dot(qi + float2(1, 1), float2(12.9898, 78.233))) * 43758.5453);
float vn = lerp(lerp(h00, h10, qf.x), lerp(h01, h11, qf.x), qf.y);
float w = CrackWidth * lerp(0.2, 1.4, vn);
float aa = max(fwidth(edge), 1e-4);
float crack = (1.0 - smoothstep(w - aa, w + aa, edge)) * mask * mask;

// Dry = lerp(lerp(G, Dust, ad), 0, ac) folded into one lerp(G, X1, a1)
float ad = mask * DustAmount * lerp(0.6, 1.0, vn);
float ac = crack * (1.0 - CrackDarken);
float a1 = 1.0 - (1.0 - ad) * (1.0 - ac);
float3 X1a = DustColor.rgb * ad * (1.0 - ac);

// Wet: absorbed water darkens albedo (toward WetColor, keeping G * WetDarken) and lowers roughness
float a2 = mask * (1.0 - WetDarken);

// Water soaks outward from the plant at the decal centre
float soft = max(SoakSoftness, 0.01);
float wet = saturate((SoilWetness * (PatchRadius + soft) - d) / soft);

float a = (1.0 - wet) * a1 + wet * a2;
float inv = a > 1e-4 ? 1.0 / a : 0.0;
OutOpacity = a;
OutRoughness = a > 1e-4 ? saturate(((1.0 - wet) * a1 * DryRoughness + wet * a2 * WetRoughness) * inv) : 1.0;
return ((1.0 - wet) * X1a + wet * a2 * WetColor.rgb) * inv;
'''

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
log = []

if eal.does_asset_exist(DECAL):
    mat = unreal.load_asset(DECAL)
    mel.delete_all_material_expressions(mat)
else:
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        DECAL.rsplit('/', 1)[1], CROPS, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_DEFERRED_DECAL)
mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)

custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
input_names = ['UV'] + [s[0] for s in SCALARS] + [v[0] for v in VECTORS]
inputs = []
for nm in input_names:
    ci = unreal.CustomInput()
    ci.set_editor_property('input_name', nm)
    inputs.append(ci)
custom.set_editor_property('inputs', inputs)
outs = []
for nm in ('OutOpacity', 'OutRoughness'):
    co = unreal.CustomOutput()
    co.set_editor_property('output_name', nm)
    co.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    outs.append(co)
custom.set_editor_property('additional_outputs', outs)
custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
custom.set_editor_property('description', 'SoilMoistureDecal')
custom.set_editor_property('code', HLSL)

ok = []
uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -700, -100)
ok.append(mel.connect_material_expressions(uv, '', custom, 'UV'))
row = 0
for nm, dv in SCALARS:
    sp = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, row * 70)
    sp.set_editor_property('parameter_name', nm)
    sp.set_editor_property('default_value', dv)
    sp.set_editor_property('group', 'SoilMoisture')
    ok.append(mel.connect_material_expressions(sp, '', custom, nm))
    row += 1
for nm, dv in VECTORS:
    vp = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, row * 70)
    vp.set_editor_property('parameter_name', nm)
    vp.set_editor_property('default_value', dv)
    vp.set_editor_property('group', 'SoilMoisture')
    ok.append(mel.connect_material_expressions(vp, '', custom, nm))
    row += 2
ok.append(mel.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR))
ok.append(mel.connect_material_property(custom, 'OutOpacity', unreal.MaterialProperty.MP_OPACITY))
ok.append(mel.connect_material_property(custom, 'OutRoughness', unreal.MaterialProperty.MP_ROUGHNESS))
log.append('connections ok=%d/%d' % (sum(1 for x in ok if x), len(ok)))
mel.recompile_material(mat)
eal.save_asset(DECAL, False)
