import unreal
path='/Game/Materials/M_Backdrop'
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_Backdrop','/Game/Materials',unreal.Material,unreal.MaterialFactoryNew())
    mat.set_editor_property('two_sided',True)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    color=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionVectorParameter,-250,0)
    color.set_editor_property('parameter_name','Color')
    color.set_editor_property('default_value',unreal.LinearColor(.68,.76,.79,1))
    unreal.MaterialEditingLibrary.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
