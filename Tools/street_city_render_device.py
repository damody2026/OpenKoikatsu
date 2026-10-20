"""Use an available Cycles GPU; report the actual selected backend."""
def configure(scene):
 import bpy
 pref=bpy.context.preferences.addons['cycles'].preferences
 for backend in ('OPTIX','CUDA','HIP'):
  try:
   pref.compute_device_type=backend;pref.refresh_devices();devices=[d for d in pref.devices if d.type==backend]
   if not devices:continue
   for device in pref.devices:device.use=device in devices
   scene.cycles.device='GPU';result=dict(backend=backend,devices=[d.name for d in devices]);print('CITY RENDER DEVICE',result,flush=True);return result
  except (TypeError,ValueError,RuntimeError):continue
 scene.cycles.device='CPU';return dict(backend='CPU',devices=['CPU'])

if __name__=='__main__':
 import bpy
 bpy.context.scene.render.engine='CYCLES';print(configure(bpy.context.scene))
