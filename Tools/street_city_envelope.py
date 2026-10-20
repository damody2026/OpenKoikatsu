"""Exact planar accounting of the exterior envelope; coordinates are metres.

Unassigned area is a design error, never an implicit courtyard or scale change.
This checks program coverage, not walls, collision meshes or code compliance.
"""

def subtract_rect(source, cut):
    x0,y0,x1,y1=source; a,b,c,d=cut
    a,b,c,d=max(x0,a),max(y0,b),min(x1,c),min(y1,d)
    if c<=a or d<=b:return [source]
    return [r for r in [(x0,y0,a,y1),(c,y0,x1,y1),(a,y0,c,b),(a,d,c,y1)]
            if r[2]-r[0]>1e-7 and r[3]-r[1]>1e-7]

def box(center, size, rotation=0, margin=0):
    x,y=center[:2];w,d=size[:2]
    if rotation%180==90:w,d=d,w
    return [x-w/2-margin,y-d/2-margin,x+w/2+margin,y+d/2+margin]

from street_city_geometry import courtyard, main_hall

def envelope_contract(bid, footprint, floor, rooms):
    w,d=footprint
    outer=[-w/2,-d/2,w/2,d/2]
    # Exterior wall bands are accounted separately from usable floor area.
    inner=[outer[0]+.2,outer[1]+.2,outer[2]-.2,outer[3]-.2]
    if floor.get('small_shop_design'):
        pieces=[inner]
        for cell in floor['small_shop_design']['cells']:
            pieces=[p for r in pieces for p in subtract_rect(r,cell['bounds_m'])]
        assert all(min(r[2]-r[0],r[3]-r[1])<=.200001 for r in pieces),'unassigned small-shop region'
        return dict(exterior_shape='rectangle',exterior_bounds_m=outer,interior_envelope_bounds_m=inner,exterior_wall_design_thickness_m=.2,explicit_exterior_voids_m=[],independent_interior_scale_allowed=False,implicit_courtyard_allowed=False,unassigned_rectangles_m=[],unassigned_area_m2=0,partition_wall_bands_m=pieces,coverage_status='complete_dimensioned_cells_and_wall_allocations',final_generation_allowed=False)
    voids=[courtyard(footprint)] if bid=='E1' else []
    pieces=[inner]
    occupied=[box(q['position_m'],q['clear_size_m'],q['rotation_z_deg'],.2) for q in rooms]
    hall=main_hall(bid,w,floor['corridor_y_m']);occupied+=[box(hall['center_m'],hall['size_m'])]
    occupied+=[box(q['center_m'],q['size_m']) for q in floor.get('branch_corridors',[])]
    occupied+=[box(q['position_m'],q['size_m']) for key in ('vertical_circulation','service_access_corridors','support_rooms') for q in floor.get(key,[])]
    occupied+=[q['gross_bounds_m'] for key in ('annex_rooms','core_annex_rooms','structural_bands') for q in floor.get(key,[])]
    for cut in voids+occupied:
        pieces=[part for source in pieces for part in subtract_rect(source,cut)]
    area=sum((r[2]-r[0])*(r[3]-r[1]) for r in pieces)
    return dict(exterior_shape='U' if bid=='E1' else 'rectangle',
        exterior_bounds_m=outer,interior_envelope_bounds_m=inner,
        exterior_wall_design_thickness_m=.2,explicit_exterior_voids_m=voids,
        independent_interior_scale_allowed=False,implicit_courtyard_allowed=False,
        rule='室內沿用室外外牆輪廓及同一公尺座標；僅扣牆厚及外觀已標示的中庭／挑空；每塊剩餘面積必須有用途、分隔及可達入口。',
        unassigned_rectangles_m=[[round(v,4) for v in r] for r in pieces],
        unassigned_area_m2=round(area,3),
        coverage_status='needs_relayout' if area>.01 else 'program_coverage_only; mesh_checks_pending',
        final_generation_allowed=False)
