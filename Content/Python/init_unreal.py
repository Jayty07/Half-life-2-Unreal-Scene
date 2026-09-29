"""Runs automatically when the editor starts (PythonScriptPlugin convention).

On the first launch the PointInsertion map does not exist yet, so it is
generated once the editor has finished loading.
"""

import unreal

import hl2_blockout

_tick_handle = None


def _on_tick(_delta_seconds):
    global _tick_handle
    if unreal.AssetRegistryHelpers.get_asset_registry().is_loading_assets():
        return

    unreal.unregister_slate_post_tick_callback(_tick_handle)
    _tick_handle = None

    try:
        hl2_blockout.ensure_map()
    except Exception as error:  # keep editor startup alive if generation fails
        unreal.log_error("HL2Blockout: map generation failed: {}".format(error))


_tick_handle = unreal.register_slate_post_tick_callback(_on_tick)
