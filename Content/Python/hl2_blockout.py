"""Editor helpers that create /Game/Maps/PointInsertion from the C++ blockout actor.

Run manually from the editor's Output Log (Python mode):
    import hl2_blockout; hl2_blockout.create_map()
"""

import unreal

MAP_PATH = "/Game/Maps/PointInsertion"


def _level_subsystem():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _actor_subsystem():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def populate_current_level():
    """Ensure the open level contains the blockout actor and a PlayerStart on the train."""
    actors = _actor_subsystem().get_all_level_actors()

    blockout = next((a for a in actors if isinstance(a, unreal.HL2PointInsertionBlockout)), None)
    if blockout is None:
        blockout = _actor_subsystem().spawn_actor_from_class(
            unreal.HL2PointInsertionBlockout, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0)
        )
        blockout.set_actor_label("PointInsertionBlockout")
        unreal.log("HL2Blockout: spawned PointInsertionBlockout")

    if not any(isinstance(a, unreal.PlayerStart) for a in actors):
        start = blockout.get_player_start_transform()
        player_start = _actor_subsystem().spawn_actor_from_class(
            unreal.PlayerStart, start.translation, start.rotation.rotator()
        )
        player_start.set_actor_label("PlayerStart_Train")
        unreal.log("HL2Blockout: spawned PlayerStart in the train car")

    return blockout


def create_map(overwrite=False):
    """Create (or open) /Game/Maps/PointInsertion, populate it and save it."""
    level_subsystem = _level_subsystem()

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH) and not overwrite:
        level_subsystem.load_level(MAP_PATH)
    else:
        if not level_subsystem.new_level(MAP_PATH):
            unreal.log_error("HL2Blockout: could not create " + MAP_PATH)
            return False

    populate_current_level()
    level_subsystem.save_current_level()
    unreal.log("HL2Blockout: saved " + MAP_PATH)
    return True


def ensure_map():
    """Create the map on first editor launch; leave it alone afterwards."""
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        return False
    return create_map()
