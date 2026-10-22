"""Explicit alpha-channel material graph, independent of glTF importer defaults."""
import unreal
from street_city_streetscape import OUT,sha

def foliage_material(aid):
 eal=unreal.EditorAssetLibrary;folder='/Game/UniversityCity/StreetScenery/Foliage';eal.make_directory(folder);source=OUT/'textures'/(aid+'-leaves.png');name=aid+'_RGBA';texture=eal.load_asset(folder+'/'+name)
 if not texture:
  task=unreal.AssetImportTask();task.set_editor_property('filename',str(source));task.set_editor_property('destination_path',folder);task.set_editor_property('destination_name',name);task.set_editor_property('automated',True);task.set_editor_property('save',True);unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);texture=eal.load_asset(folder+'/'+name)
 assert texture
 texture.set_editor_property('compression_no_alpha',False);texture.set_editor_property('srgb',True);texture.set_editor_property('power_of_two_mode',unreal.TexturePowerOfTwoSetting.STRETCH_TO_POWER_OF_TWO);texture.set_editor_property('max_texture_size',2048);texture.set_editor_property('do_scale_mips_for_alpha_coverage',True);texture.set_editor_property('alpha_coverage_thresholds',unreal.Vector4(0,0,0,.33));eal.save_loaded_asset(texture)
 material=eal.load_asset(folder+'/M_'+aid+'_Cutout')
 if not material:material=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_'+aid+'_Cutout',folder,unreal.Material,unreal.MaterialFactoryNew())
 mel=unreal.MaterialEditingLibrary;mel.delete_all_material_expressions(material);material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED);material.set_editor_property('two_sided',True);material.set_editor_property('opacity_mask_clip_value',.33)
 sample=mel.create_material_expression(material,unreal.MaterialExpressionTextureSample,-400,0);sample.set_editor_property('texture',texture)
 assert mel.connect_material_property(sample,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
 assert mel.connect_material_property(sample,'A',unreal.MaterialProperty.MP_OPACITY_MASK)
 rough=mel.create_material_expression(material,unreal.MaterialExpressionConstant,-400,200);rough.set_editor_property('r',.85);mel.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
 mel.recompile_material(material);eal.set_metadata_tag(material,'StreetLeafAlphaSHA256',sha(source));eal.set_metadata_tag(material,'StreetLeafMaskChannel','A');assert eal.save_loaded_asset(material)
 return material

def apply_to_mesh(mesh,material):
 slots=mesh.get_editor_property('static_materials')
 for i,slot in enumerate(slots):
  if 'masked' in str(slot.get_editor_property('material_slot_name')).lower():mesh.set_material(i,material)
 assert unreal.EditorAssetLibrary.save_loaded_asset(mesh)
 assert all(mesh.get_material(i)==material for i,slot in enumerate(slots) if 'masked' in str(slot.get_editor_property('material_slot_name')).lower())

def configure_daylight(subsystem):
 actors={a.get_actor_label():a for a in subsystem.get_all_level_actors()};sky=actors.get('CitySkyLight');assert sky
 cube=unreal.EditorAssetLibrary.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap');assert cube
 sky.light_component.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);sky.light_component.set_editor_property('cubemap',cube);sky.light_component.set_intensity(2.5);sky.light_component.recapture_sky()
 fill=actors.get('CityStreetFill') or subsystem.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(55000,55000,50000),unreal.Rotator(pitch=-65,yaw=145,roll=0));fill.set_actor_label('CityStreetFill');fill.light_component.set_mobility(unreal.ComponentMobility.MOVABLE);fill.light_component.set_intensity(1.5);fill.light_component.set_cast_shadows(False)
