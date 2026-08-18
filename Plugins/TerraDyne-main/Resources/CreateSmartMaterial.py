import unreal

def create_smart_material():
    asset_path = "/Game/TerraDyne/Materials"
    mat_name = "M_TerraDyne_Smart"
    full_path = f"{asset_path}/{mat_name}"
    
    # 1. Create Asset
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        unreal.log(f"Material {full_path} already exists. Overwriting...")
        unreal.EditorAssetLibrary.delete_asset(full_path)
        
    mat_factory = unreal.MaterialFactoryNew()
    material = asset_tools.create_asset(mat_name, asset_path, unreal.Material, mat_factory)
    
    # 2. Setup Nodes
    # Colors
    c_grass = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, 0)
    c_grass.set_editor_property("parameter_name", "Color_Grass")
    c_grass.set_editor_property("default_value", unreal.LinearColor(0.1, 0.3, 0.05, 1)) # Deep Green

    c_rock = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, 200)
    c_rock.set_editor_property("parameter_name", "Color_Rock")
    c_rock.set_editor_property("default_value", unreal.LinearColor(0.15, 0.12, 0.1, 1)) # Dark Grey/Brown

    c_snow = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, 400)
    c_snow.set_editor_property("parameter_name", "Color_Snow")
    c_snow.set_editor_property("default_value", unreal.LinearColor(0.9, 0.95, 1.0, 1)) # White

    c_sand = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, 600)
    c_sand.set_editor_property("parameter_name", "Color_Sand")
    c_sand.set_editor_property("default_value", unreal.LinearColor(0.7, 0.6, 0.4, 1)) # Sand

    # Slope Logic
    # VertexNormalWS -> Mask B (Z) -> 1-x -> Clamp -> Slope
    v_norm = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -800, 100)
    mask_z = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionComponentMask, -650, 100)
    mask_z.set_editor_property("R", False)
    mask_z.set_editor_property("G", False)
    mask_z.set_editor_property("B", True)
    mask_z.set_editor_property("A", False)
    
    one_minus = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionOneMinus, -550, 100)
    
    slope_contrast = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionPower, -450, 100)
    
    # Exponent Constant
    slope_exp = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, -550, 200)
    slope_exp.set_editor_property("R", 4.0)

    # Connect Slope
    unreal.MaterialEditingLibrary.connect_material_expressions(v_norm, "", mask_z, "")
    unreal.MaterialEditingLibrary.connect_material_expressions(mask_z, "", one_minus, "")
    unreal.MaterialEditingLibrary.connect_material_expressions(one_minus, "", slope_contrast, "Base")
    unreal.MaterialEditingLibrary.connect_material_expressions(slope_exp, "", slope_contrast, "Exponent")

    # Height Logic
    # WorldPos -> Mask B (Z)
    w_pos = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -800, 400)
    h_mask = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionComponentMask, -650, 400)
    h_mask.set_editor_property("R", False)
    h_mask.set_editor_property("G", False)
    h_mask.set_editor_property("B", True)
    h_mask.set_editor_property("A", False)
    
    # Divide by 5000 (Height Scale) to normalize roughly
    h_div = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionDivide, -550, 400)
    h_scale = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, -650, 500)
    h_scale.set_editor_property("R", 5000.0)
    
    unreal.MaterialEditingLibrary.connect_material_expressions(w_pos, "", h_mask, "")
    unreal.MaterialEditingLibrary.connect_material_expressions(h_mask, "", h_div, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(h_scale, "", h_div, "B")

    # Blends
    # 1. Base: Grass vs Rock (Slope)
    blend_slope = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -100, 100)
    unreal.MaterialEditingLibrary.connect_material_expressions(c_grass, "", blend_slope, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(c_rock, "", blend_slope, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(slope_contrast, "", blend_slope, "Alpha")

    # 2. Add Snow (Height > 0.8)
    # If H > 0.8, blend to Snow.
    # Simple Subtract -> Saturate logic for mask
    snow_sub = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionSubtract, -300, 400)
    snow_offset = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 500)
    snow_offset.set_editor_property("R", 0.6)
    
    snow_sat = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionSaturate, -200, 400)
    # Multiply to sharpen snow line
    snow_mul = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 400)
    snow_gain = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, -350, 550)
    snow_gain.set_editor_property("R", 10.0)
    
    unreal.MaterialEditingLibrary.connect_material_expressions(h_div, "", snow_sub, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(snow_offset, "", snow_sub, "B")
    
    unreal.MaterialEditingLibrary.connect_material_expressions(snow_sub, "", snow_mul, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(snow_gain, "", snow_mul, "B")
    
    unreal.MaterialEditingLibrary.connect_material_expressions(snow_mul, "", snow_sat, "")

    blend_snow = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, 100, 200)
    unreal.MaterialEditingLibrary.connect_material_expressions(blend_slope, "", blend_snow, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(c_snow, "", blend_snow, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(snow_sat, "", blend_snow, "Alpha")

    # 3. Add Sand (Height < -0.1)
    # Sand mask = (H < -0.1) -> 1 - (H + 0.1) * gain?
    # Actually just 1-Saturate(H + 0.5) maybe.
    
    # Final Output
    # Connect to Material Properties
    # Try generic connect function
    try:
        unreal.MaterialEditingLibrary.connect_material_property(blend_snow, "", unreal.MaterialProperty.MP_BASE_COLOR)
        
        # Roughness (0.8 everywhere, 0.4 on snow)
        roughness = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, 200, 400)
        roughness.set_editor_property("R", 0.8)
        unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    except Exception as e:
        unreal.log_warning(f"Connect failed: {e}. Trying direct assignment...")
        material.base_color.expression = blend_snow
        
        roughness = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, 200, 400)
        roughness.set_editor_property("R", 0.8)
        material.roughness.expression = roughness

    # 3. Recompile
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(full_path)
    unreal.log(f"Created Smart Procedural Material: {full_path}")

if __name__ == "__main__":
    create_smart_material()
