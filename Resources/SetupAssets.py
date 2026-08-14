import unreal

def run():
    asset_lib = unreal.EditorAssetLibrary()
    
    old_path = "/Game/TerraDyne/Blueprints/BP_TerraDyne_Wizard"
    new_path = "/Game/TerraDyne/Blueprints/BP_TerraDyneSceneSetup"
    
    if asset_lib.does_asset_exist(old_path):
        unreal.log(f"Renaming {old_path} to {new_path}...")
        if asset_lib.rename_asset(old_path, new_path):
            unreal.log("Rename successful.")
        else:
            unreal.log_error("Rename failed.")
    else:
        unreal.log(f"Asset {old_path} not found. Checking if target exists...")
        if asset_lib.does_asset_exist(new_path):
             unreal.log(f"{new_path} already exists. Good.")

    # Save
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)

if __name__ == "__main__":
    run()
