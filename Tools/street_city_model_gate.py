"""Inspect actual GLB scene vertices, independently of approval metadata.

glTF uses Y up. Reports use the city convention X east, Y north, Z up.
Unsupported animated/compressed geometry fails closed instead of guessing bounds.
"""
import hashlib,json,math,struct
from pathlib import Path
from street_city_parts import geometry_signature

def identity():return [[float(r==c) for c in range(4)] for r in range(4)]
def multiply(a,b):return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
def matrix(n):
    if 'matrix' in n:return [[n['matrix'][c*4+r] for c in range(4)] for r in range(4)]
    x,y,z,w=n.get('rotation',[0,0,0,1]);s=n.get('scale',[1,1,1]);t=n.get('translation',[0,0,0])
    rows=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
    a=identity()
    for r in range(3):
        for c in range(3):a[r][c]=rows[r][c]*s[c]
        a[r][3]=t[r]
    return a

def inspect_glb(path):
    data=Path(path).read_bytes()
    if len(data)<28 or struct.unpack_from('<III',data)!= (0x46546c67,2,len(data)):raise ValueError('Invalid GLB header')
    chunks={};offset=12
    while offset<len(data):
        length,kind=struct.unpack_from('<II',data,offset);offset+=8
        if offset+length>len(data):raise ValueError('Truncated GLB chunk')
        chunks[kind]=data[offset:offset+length];offset+=length
    g=json.loads(chunks[0x4e4f534a]);binary=chunks.get(0x004e4942,b'')
    if g.get('animations') or g.get('skins'):raise ValueError('Animated/skinned asset needs pose-specific acceptance')
    if any(b.get('uri') for b in g.get('buffers',[])):raise ValueError('External buffers are not a self-contained approved GLB')
    cache={};counts={'primitives':0,'vertices':0,'normal_primitives':0,'uv_primitives':0}
    def vertices(mi):
        if mi in cache:return cache[mi]
        result=[]
        for p in g['meshes'][mi]['primitives']:
            if p.get('mode',4)!=4 or p.get('targets'):raise ValueError('Only static triangle primitives accepted')
            ai=p['attributes']['POSITION'];a=g['accessors'][ai]
            if a.get('sparse') or a['componentType']!=5126 or a['type']!='VEC3' or 'bufferView' not in a:raise ValueError('Unsupported position encoding; decode before acceptance')
            v=g['bufferViews'][a['bufferView']]
            if v.get('buffer',0)!=0:raise ValueError('Unsupported multiple buffers')
            start=v.get('byteOffset',0)+a.get('byteOffset',0);stride=v.get('byteStride',12)
            end=v.get('byteOffset',0)+v['byteLength']
            if a['count']<=0 or stride<12 or start+(a['count']-1)*stride+12>min(end,len(binary)):raise ValueError('Position buffer exceeds bounds')
            result.extend(struct.unpack_from('<fff',binary,start+k*stride) for k in range(a['count']))
            counts['primitives']+=1;counts['vertices']+=a['count']
            counts['normal_primitives']+=int('NORMAL' in p['attributes']);counts['uv_primitives']+=int('TEXCOORD_0' in p['attributes'])
        cache[mi]=result;return result
    low=[math.inf]*3;high=[-math.inf]*3;mesh_nodes=0;visited=set()
    def visit(i,parent,ancestors):
        nonlocal mesh_nodes
        if i in ancestors:raise ValueError('Cyclic GLB hierarchy')
        if i in visited:raise ValueError('Repeated GLB scene node')
        visited.add(i)
        n=g['nodes'][i];world=multiply(parent,matrix(n))
        if 'mesh' in n:
            mesh_nodes+=1
            for v in vertices(n['mesh']):
                q=[sum(world[r][c]*v[c] for c in range(3))+world[r][3] for r in range(3)]
                city=[q[0],-q[2],q[1]]
                if not all(math.isfinite(x) for x in city):raise ValueError('Nonfinite vertex or transform')
                for k in range(3):low[k]=min(low[k],city[k]);high[k]=max(high[k],city[k])
        for child in n.get('children',[]):visit(child,world,ancestors|{i})
    for i in g['scenes'][g.get('scene',0)].get('nodes',[]):visit(i,identity(),set())
    if any('mesh' in n and i not in visited for i,n in enumerate(g['nodes'])):raise ValueError('Uninspected mesh outside default scene')
    if not mesh_nodes or min(high[k]-low[k] for k in range(3))<=0:raise ValueError('Empty or degenerate model')
    return dict(model_sha256=hashlib.sha256(data).hexdigest(),bounds_min_m=low,bounds_max_m=high,
        measured_size_m=[high[k]-low[k] for k in range(3)],mesh_nodes=mesh_nodes,**counts)

def geometry_errors(spec,actual,tolerance=.003):
    w,d,h=spec['size_m'];pivot=spec['pivot']
    if pivot=='floor_center':expected_low=[-w/2,-d/2,0];expected_high=[w/2,d/2,h]
    elif pivot=='hinge_bottom':expected_low=[0,-d/2,0];expected_high=[w,d/2,h]
    else:return ['unsupported pivot '+pivot]
    errors=[]
    for key,expected in [('bounds_min_m',expected_low),('bounds_max_m',expected_high)]:
        if any(abs(x-y)>tolerance for x,y in zip(actual[key],expected)):errors.append(key+' differs from canonical dimensions/pivot')
    return errors

def normalization_errors(original,target,limit=1.05):
    if len(original)!=3 or min(original)<=0:return ['degenerate source dimensions']
    scale=[t/o for o,t in zip(original,target)]
    if max(scale)/min(scale)>limit:return ['nonuniform correction exceeds limit; check orientation or regenerate']
    return []

def approval_errors(spec,actual,meta,ledger,root):
    errors=geometry_errors(spec,actual)
    for field,value in [('approved',True),('pivot',spec['pivot']),('axes','X east,Y north,Z up'),('model_sha256',actual['model_sha256']),('geometry_signature',geometry_signature(spec))]:
        if meta.get(field)!=value:errors.append('approval '+field+' missing/stale')
    recorded=meta.get('measured_size_m',[])
    if len(recorded)!=3 or any(abs(x-y)>.00001 for x,y in zip(recorded,actual['measured_size_m'])):errors.append('metadata differs from measured vertices')
    entry=next((e for e in ledger.get('entries',[]) if e['asset_id']==spec['id'] and e.get('geometry_signature')==geometry_signature(spec)),None)
    if not entry or not entry.get('actual_model_approved'):errors.append('no approved matching generation ledger entry')
    else:
        if not entry.get('reference_status','').startswith('approved'):errors.append('reference not approved')
        if not entry.get('tripo_model_id') or entry.get('tripo_model_id')!=meta.get('tripo_model_id'):errors.append('Tripo source mismatch')
        ref=Path(root)/entry.get('tripo_input_file','')
        if not ref.is_file() or hashlib.sha256(ref.read_bytes()).hexdigest()!=entry.get('tripo_input_sha256'):errors.append('reference file missing/changed')
    if not meta.get('quality_review_file'):errors.append('mesh quality review missing')
    else:
        review=Path(root)/meta['quality_review_file']
        if not review.is_file():errors.append('mesh quality review unavailable')
        else:
            q=json.loads(review.read_text('utf-8'))
            if q.get('model_sha256')!=actual['model_sha256'] or not all(q.get(k) is True for k in ('silhouette','topology_normals','materials_uv','submeshes','interaction_clearances')):errors.append('mesh quality review incomplete/stale')
    return errors
