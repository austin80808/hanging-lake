"""Create the Bowl level: empty map + PlayerStart + lights.

The game mode builds the terrain, water and fish at BeginPlay from the exported
geodata, so this level only needs a start point and an atmosphere.
"""
import unreal

MAP_DIR = "/Game/Maps"
MAP_NAME = "Bowl"
FULL = f"{MAP_DIR}/{MAP_NAME}"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# new_level refuses to overwrite, so clear any previous map first. Deleting the
# asset alone can leave the folder marked as existing, so also clear the folder.
if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.EditorAssetLibrary.delete_asset(FULL)
unreal.EditorAssetLibrary.make_directory(MAP_DIR)

les.new_level(FULL)
unreal.log(f"HL: new level at {FULL}")

# The game mode teleports the pawn to the shoreline at BeginPlay.
start = eas.spawn_actor_from_class(
    unreal.PlayerStart, unreal.Vector(0, 0, 500), unreal.Rotator(0, 0, 0))
start.set_actor_label("PlayerStart")

# Low, warm sun raking down the canyon.
sun = eas.spawn_actor_from_class(
    # unreal.Rotator takes (roll, pitch, yaw): name them, or the sun ends up below
    # the horizon and the sky renders black. The game also re-aims it at start.
    unreal.DirectionalLight, unreal.Vector(0, 0, 2000), unreal.Rotator(roll=0, pitch=-38, yaw=148))
sun.set_actor_label("Sun")
comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
if comp:
    comp.set_intensity(6.0)
    comp.set_light_color(unreal.LinearColor(1.0, 0.93, 0.82, 1.0))
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)

sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1000))
sky.set_actor_label("SkyLight")

atm = eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
atm.set_actor_label("SkyAtmosphere")

fog = eas.spawn_actor_from_class(
    unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
fog.set_actor_label("Fog")
fogc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
if fogc:
    fogc.set_editor_property("fog_density", 0.012)
    fogc.set_editor_property("fog_height_falloff", 0.12)

# Fixed exposure: auto-exposure would fight the view of the bed through the water.
ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
ppv.set_actor_label("PostProcess")
ppv.set_editor_property("unbound", True)
settings = ppv.get_editor_property("settings")
settings.set_editor_property("override_auto_exposure_min_brightness", True)
settings.set_editor_property("auto_exposure_min_brightness", 1.0)
settings.set_editor_property("override_auto_exposure_max_brightness", True)
settings.set_editor_property("auto_exposure_max_brightness", 1.0)
settings.set_editor_property("override_bloom_intensity", True)
settings.set_editor_property("bloom_intensity", 0.35)
ppv.set_editor_property("settings", settings)

# save_current_level needs a filename, which a brand-new level does not have.
world = unreal.EditorLevelLibrary.get_editor_world()
unreal.EditorLoadingAndSavingUtils.save_map(world, FULL)
unreal.log(f"HL: created {FULL}")
