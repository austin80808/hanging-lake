"""Rebuild the bowl materials and fix the Bowl level's sky.

Root cause of the black bowl (2026-11-04): earlier scripts connected
MaterialExpressionVertexColor by an output named "RGB". That output is unnamed
(""), so the connection failed silently and the unlit materials had no
emissive input -> pure black. Connections here are checked and logged.
"""
import unreal

MAT_DIR = "/Game/Materials"
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def check(ok, what):
    unreal.log(f"HLFIX {what}: {'ok' if ok else 'FAILED'}")
    if not ok:
        raise RuntimeError(what)


def fresh(name):
    path = f"{MAT_DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


# Rock: lit, base colour from vertex colour, plus a small emissive floor so the
# bowl never goes fully black in shadow.
rock = fresh("M_BowlRock")
rock.set_editor_property("two_sided", True)
vc = mel.create_material_expression(rock, unreal.MaterialExpressionVertexColor, -700, 0)
check(mel.connect_material_property(vc, "", unreal.MaterialProperty.MP_BASE_COLOR), "rock base colour")
k = mel.create_material_expression(rock, unreal.MaterialExpressionConstant, -700, 220)
k.set_editor_property("r", 0.04)
mul = mel.create_material_expression(rock, unreal.MaterialExpressionMultiply, -450, 120)
check(mel.connect_material_expressions(vc, "", mul, "A"), "rock emissive A")
check(mel.connect_material_expressions(k, "", mul, "B"), "rock emissive B")
check(mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "rock emissive")
rough = mel.create_material_expression(rock, unreal.MaterialExpressionConstant, -700, 340)
rough.set_editor_property("r", 0.85)
check(mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS), "rock roughness")
mel.recompile_material(rock)

# Water: a multiply (modulate) blend. Real water absorbs red, so the pale bed
# reads turquoise through it; a translucent overlay only washed it out milky.
# Vertex alpha masks it to the lake outline: lerp(white, tint, alpha).
water = fresh("M_BowlWater")
water.set_editor_property("two_sided", True)
water.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MODULATE)
water.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
tint = mel.create_material_expression(water, unreal.MaterialExpressionConstant3Vector, -700, 0)
tint.set_editor_property("constant", unreal.LinearColor(0.12, 0.80, 0.82, 1.0))
one = mel.create_material_expression(water, unreal.MaterialExpressionConstant3Vector, -700, -150)
one.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
wvc = mel.create_material_expression(water, unreal.MaterialExpressionVertexColor, -700, 200)
lerp = mel.create_material_expression(water, unreal.MaterialExpressionLinearInterpolate, -400, 0)
check(mel.connect_material_expressions(one, "", lerp, "A"), "water lerp A")
check(mel.connect_material_expressions(tint, "", lerp, "B"), "water lerp B")
check(mel.connect_material_expressions(wvc, "A", lerp, "Alpha"), "water lerp alpha")
check(mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "water emissive")
mel.recompile_material(water)
# Falls: thin white translucent ribbons; vertex alpha fades their edges.
falls = fresh("M_Falls")
falls.set_editor_property("two_sided", True)
falls.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
falls.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
white = mel.create_material_expression(falls, unreal.MaterialExpressionConstant3Vector, -700, 0)
white.set_editor_property("constant", unreal.LinearColor(1.3, 1.35, 1.4, 1.0))
check(mel.connect_material_property(white, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "falls emissive")
fvc = mel.create_material_expression(falls, unreal.MaterialExpressionVertexColor, -700, 200)
fop = mel.create_material_expression(falls, unreal.MaterialExpressionConstant, -700, 360)
fop.set_editor_property("r", 0.75)
fmul = mel.create_material_expression(falls, unreal.MaterialExpressionMultiply, -450, 260)
check(mel.connect_material_expressions(fvc, "A", fmul, "A"), "falls opacity A")
check(mel.connect_material_expressions(fop, "", fmul, "B"), "falls opacity B")
check(mel.connect_material_property(fmul, "", unreal.MaterialProperty.MP_OPACITY), "falls opacity")
mel.recompile_material(falls)

unreal.EditorAssetLibrary.save_directory(MAT_DIR)

# Bowl level: the sun must drive the SkyAtmosphere or the sky renders black.
unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/Bowl")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
    n = a.get_class().get_name()
    if n == "DirectionalLight":
        c = a.get_component_by_class(unreal.DirectionalLightComponent)
        unreal.log(f"HLFIX sun before: atmosphere_sun_light={c.get_editor_property('atmosphere_sun_light')} intensity={c.get_editor_property('intensity')}")
        c.set_editor_property("atmosphere_sun_light", True)
        c.set_editor_property("intensity", 10.0)
    elif n == "SkyLight":
        c = a.get_component_by_class(unreal.SkyLightComponent)
        c.set_editor_property("real_time_capture", True)
    elif n == "PostProcessVolume":
        unreal.log(f"HLFIX ppv enabled={a.get_editor_property('enabled')}")
unreal.EditorLoadingAndSavingUtils.save_map(unreal.EditorLevelLibrary.get_editor_world(), "/Game/Maps/Bowl")
unreal.log("HLFIX done")
unreal.SystemLibrary.quit_editor()
