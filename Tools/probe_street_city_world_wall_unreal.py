import unreal,json
from pathlib import Path
OUT=Path(__file__).resolve().parents[1]/'output/street-social-city-production/world-wall-grok-v1'
try:
 data={}
 for name in ['CustomMaterialOutputType','CustomInput','MaterialExpressionCustom','MaterialExpressionVertexNormalWS','MaterialExpressionWorldPosition','MaterialExpressionTextureObjectParameter']:
  cls=getattr(unreal,name,None);data[name]=str(cls)
  if name=='CustomMaterialOutputType':data['custom_outputs']=[k for k in dir(cls) if k.isupper()]
 for name in ['MaterialVectorCoordTransform','MaterialVectorCoordTransformSource']:data[name]=[k for k in dir(getattr(unreal,name,object)) if k.isupper()]
 (OUT/'unreal-api.json').write_text(json.dumps(data,indent=2),'utf8')
finally:unreal.SystemLibrary.quit_editor()
