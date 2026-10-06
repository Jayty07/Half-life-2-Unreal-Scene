"""Editor helpers that create the blockout maps from the C++ blockout actors.

Run manually from the editor's Output Log (Python mode):
    import hl2_blockout; hl2_blockout.create_map()                  # /Game/Maps/PointInsertion
    import hl2_blockout; hl2_blockout.create_neighborhoods_map()    # /Game/Maps/Neighborhoods (optional)
"""

import unreal

MAP_PATH = "/Game/Maps/PointInsertion"
NEIGHBORHOODS_MAP_PATH = "/Game/Maps/Neighborhoods"


def _level_subsystem():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _actor_subsystem():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def populate_current_level(blockout_class=None, label="PointInsertionBlockout", start_label="PlayerStart_Train"):
    """Ensure the open level contains the blockout actor and a PlayerStart at its spawn point."""
    blockout_class = blockout_class or unreal.HL2PointInsertionBlockout
    actors = _actor_subsystem().get_all_level_actors()

    blockout = next((a for a in actors if isinstance(a, blockout_class)), None)
    if blockout is None:
        blockout = _actor_subsystem().spawn_actor_from_class(
            blockout_class, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 0.0)
        )
        blockout.set_actor_label(label)
        unreal.log("HL2Blockout: spawned " + label)

    if not any(isinstance(a, unreal.PlayerStart) for a in actors):
        start = blockout.get_player_start_transform()
        player_start = _actor_subsystem().spawn_actor_from_class(
            unreal.PlayerStart, start.translation, start.rotation.rotator()
        )
        player_start.set_actor_label(start_label)
        unreal.log("HL2Blockout: spawned " + start_label)

    return blockout


def _create(map_path, overwrite, **populate_args):
    level_subsystem = _level_subsystem()

    if unreal.EditorAssetLibrary.does_asset_exist(map_path) and not overwrite:
        level_subsystem.load_level(map_path)
    else:
        if not level_subsystem.new_level(map_path):
            unreal.log_error("HL2Blockout: could not create " + map_path)
            return False

    populate_current_level(**populate_args)
    level_subsystem.save_current_level()
    unreal.log("HL2Blockout: saved " + map_path)
    return True


def create_map(overwrite=False):
    """Create (or open) /Game/Maps/PointInsertion, populate it and save it."""
    return _create(MAP_PATH, overwrite)


def create_neighborhoods_map(overwrite=False):
    """Create (or open) the optional /Game/Maps/Neighborhoods blockout, populate it and save it."""
    return _create(
        NEIGHBORHOODS_MAP_PATH,
        overwrite,
        blockout_class=unreal.HL2NeighborhoodsBlockout,
        label="NeighborhoodsBlockout",
        start_label="PlayerStart_Hub",
    )


def ensure_map():
    """Create the PointInsertion map on first editor launch; leave it alone afterwards."""
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        return False
    return create_map()
