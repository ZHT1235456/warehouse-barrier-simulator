"""Run through UnrealEditor-Cmd's PythonScript commandlet, not system Python."""
import unreal
path = '/Game/Materials/M_Warehouse'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.log('Warehouse material already exists')
else:
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_Warehouse', '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    lib = unreal.MaterialEditingLibrary
    color = lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -550, 0)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(.4, .4, .4, 1))
    rough = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -350, 220)
    rough.set_editor_property('parameter_name', 'Roughness')
    rough.set_editor_property('default_value', .72)
    metal = lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -350, 320)
    metal.set_editor_property('parameter_name', 'Metallic')
    metal.set_editor_property('default_value', .08)
    noise = lib.create_material_expression(mat, unreal.MaterialExpressionNoise, -800, -200)
    noise.set_editor_property('scale', .07)
    noise.set_editor_property('output_min', .82)
    noise.set_editor_property('output_max', 1.0)
    mult = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, 0)
    lib.connect_material_expressions(color, '', mult, 'A')
    lib.connect_material_expressions(noise, '', mult, 'B')
    lib.connect_material_property(mult, '', unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(metal, '', unreal.MaterialProperty.MP_METALLIC)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log('Warehouse PBR material saved')
