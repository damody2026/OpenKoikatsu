"""Metric, exportable PBR finishes. Texture pixels do not alter approved geometry."""
import json, hashlib
from pathlib import Path

SPECS = {
    'floor': dict(name='S02 porcelain matte 600mm', repeat_m=[.6,.6], grout_m=.003, roughness=.78, color=[190,193,183]),
    'wet-wall': dict(name='S02 washable wall tile 300x600mm', repeat_m=[.3,.6], grout_m=.002, roughness=.36, color=[226,226,215]),
    'paint': dict(name='S02 warm washable paint', repeat_m=[1,1], grout_m=0, roughness=.65, color=[224,220,209]),
}

def generate(folder):
    import numpy as np
    from PIL import Image
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    n=2048;rng=np.random.default_rng(20261014)
    for key,s in SPECS.items():
        noise=rng.normal(0,1,(n,n)).astype('float32')
        # Low amplitude pigment variation, no baked shadows or artificial directional light.
        yy,xx=np.mgrid[:n,:n];variation=np.sin(xx*.021)*np.sin(yy*.017)*.6+noise*.55
        albedo=np.clip(np.array(s['color'])[None,None,:]+variation[:,:,None],0,255).astype('uint8')
        height=noise*.000015
        grout=np.zeros((n,n),dtype=bool)
        if s['grout_m']:
            gx=max(1,round(n*s['grout_m']/s['repeat_m'][0]/2));gy=max(1,round(n*s['grout_m']/s['repeat_m'][1]/2))
            grout[:gy,:]=True;grout[-gy:,:]=True;grout[:,:gx]=True;grout[:,-gx:]=True
            albedo[grout]=[139,144,137];height[grout]=-.00045
        dx=(np.roll(height,-1,1)-np.roll(height,1,1))/(2*s['repeat_m'][0]/n)
        dy=(np.roll(height,-1,0)-np.roll(height,1,0))/(2*s['repeat_m'][1]/n)
        normal=np.stack([-dx,dy,np.ones_like(dx)],2);normal/=np.linalg.norm(normal,axis=2)[:,:,None]
        rough=np.clip(s['roughness']*255+noise*1.2,0,255).astype('uint8');rough[grout]=230
        for suffix,array in [('basecolor',albedo),('normal',np.clip((normal*.5+.5)*255,0,255).astype('uint8')),('roughness',rough)]:
            Image.fromarray(array).save(folder/f'{key}-{suffix}-2k.png')

def apply(scene,folder):
    import bpy
    from mathutils import Vector
    folder=Path(folder);materials={}
    for key,s in SPECS.items():
        mat=bpy.data.materials.new(s['name']);mat.use_nodes=True
        nodes=mat.node_tree.nodes;links=mat.node_tree.links;bs=nodes.get('Principled BSDF')
        bs.inputs['Metallic'].default_value=0
        for suffix,socket in [('basecolor','Base Color'),('roughness','Roughness'),('normal','Normal')]:
            tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(folder/f'{key}-{suffix}-2k.png'),check_existing=True);tex.image.pack();tex.extension='REPEAT'
            if suffix!='basecolor':tex.image.colorspace_settings.name='Non-Color'
            if suffix=='normal':
                nm=nodes.new('ShaderNodeNormalMap');links.new(tex.outputs['Color'],nm.inputs['Color']);links.new(nm.outputs['Normal'],bs.inputs[socket])
            else:links.new(tex.outputs['Color'],bs.inputs[socket])
        mat['finish_role']=key;mat['repeat_m']=s['repeat_m'];materials[key]=mat
    assignments=[]
    for obj in list(scene.objects):
        if obj.type!='MESH' or not str(obj.get('asset_id','')).startswith('structure-F'):continue
        kind=obj.get('part_kind');name=obj.name
        if kind in ('floor','stair_tread','stair_landing'):
            key='floor'
        elif kind in ('wall','ceiling'):key='wet-wall' if ('restroom' in name or '-bath-' in name) else 'paint'
        else:continue
        obj.data=obj.data.copy();obj.data.materials.clear()
        keys=list(materials);[obj.data.materials.append(materials[k]) for k in keys]
        uv=obj.data.uv_layers.active or obj.data.uv_layers.new(name='UVMap')
        for poly in obj.data.polygons:
            center=obj.matrix_world@poly.center
            role=key
            poly.material_index=keys.index(role);period=SPECS[role]['repeat_m']
            normal=(obj.matrix_world.to_3x3().inverted().transposed()@poly.normal).normalized()
            axes=[a for a in range(3) if a!=max(range(3),key=lambda a:abs(normal[a]))]
            for li in poly.loop_indices:
                pt=obj.matrix_world@obj.data.vertices[obj.data.loops[li].vertex_index].co
                uv.data[li].uv=(pt[axes[0]]/period[0],pt[axes[1]]/period[1])
        obj['metric_pbr_surface']=True;assignments.append(obj.name)
    return dict(version=1,texture_resolution=2048,texture_directory=str(folder.resolve()),units='metres',specifications=SPECS,objects=assignments,
        files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.glob('*-2k.png')},
        limitations=['Procedural original textures, not measured product scans; no certified slip rating','Surface assignment only; remeshed geometry unchanged'])

if __name__=='__main__':
    from street_city_building_context import context
    generate(context('S02')['out']/'materials')
