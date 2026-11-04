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

# Water: translucent unlit tint; vertex alpha masks it to the lake outline.
water = fresh("M_BowlWater")
water.set_editor_property("two_sided", True)
water.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
water.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
tint = mel.create_material_expression(water, unreal.MaterialExpressionConstant3Vector, -700, 0)
tint.set_editor_property("constant", unreal.LinearColor(0.10, 0.42, 0.40, 1.0))
check(mel.connect_material_property(tint, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "water emissive")
wvc = mel.create_material_expression(water, unreal.MaterialExpressionVertexColor, -700, 200)
op = mel.create_material_expression(water, unreal.MaterialExpressionConstant, -700, 360)
op.set_editor_property("r", 0.45)
wmul = mel.create_material_expression(water, unreal.MaterialExpressionMultiply, -450, 260)
check(mel.connect_material_expressions(wvc, "A", wmul, "A"), "water opacity A")
check(mel.connect_material_expressions(op, "", wmul, "B"), "water opacity B")
check(mel.connect_material_property(wmul, "", unreal.MaterialProperty.MP_OPACITY), "water opacity")
mel.recompile_material(water)
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
