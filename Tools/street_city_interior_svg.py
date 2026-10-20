"""Floor review diagrams from measured, exported geometry; one unit is one metre."""
import html,math
LABELS={'wardrobe':'衣櫃','fridge':'冰箱','toilet':'馬桶','sink':'洗手台','handwash':'洗手盆','double-sink':'工作水槽','bed':'床','table':'桌','chair':'椅','bookshelf':'書架','washing-machine':'洗衣機','dryer':'烘衣機','steel-table':'工作桌','steel-counter':'服務檯','large-wardrobe':'衣櫃','large-locker':'置物櫃','retail-rack':'貨架','retail-case':'展櫃','display-mannequin':'服飾人台','salon-chair':'美髮椅','shampoo-station':'洗髮椅'}
LABELS.update({'window':'窗','kitchen':'廚具','sofa':'沙發','shower':'淋浴','service-counter':'服務檯','prep-counter':'備餐檯','coffee-machine':'咖啡機','hob':'爐具','cup-sealer':'封口機','deck-oven':'烤箱','bread-rack':'麵包架','record-bin':'唱片櫃','clothes-rack':'衣架','fruit-bin':'水果台','large-service-cabinet':'儲物櫃','large-service-tall':'儲物櫃','large-standard-cubicle':'廁間','large-accessible-cubicle':'廁間','large-console':'櫃檯','large-bunk':'雙層床','large-lounger':'躺椅'})

def write_floorplates(output,bid,building,mesh_bounds,fixture_boxes,fronts,doors,door_yaws,source_sha):
 w,d=building['footprint_m'];floors=building.get('small_shop_design',{}).get('floors',building.get('floors',[]));output.mkdir(parents=True,exist_ok=True)
 def xy(x,y):return (x+w/2+3,d/2-y+3)
 for floor in floors:
  no=floor['floor'];z=floor['z_m'];height=d+9;parts=[f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w+6} {height}" width="1100" role="img"><title>{bid} F{no:02d} actual exported interior</title><style>text{{font-family:"Microsoft JhengHei",sans-serif;fill:#243c3b}}.wall{{fill:#526b69}}.fixture{{fill:#e7d9bd;stroke:#a98d5c;stroke-width:.025}}.door{{fill:none;stroke:#926236;stroke-width:.035}}</style><rect width="100%" height="100%" fill="#faf9f4"/><text x="3" y="1.1" font-size=".5">{bid} F{no:02d} · {w:g} × {d:g} m · Z={z:g} m</text><text x="3" y="1.9" font-size=".28">實際GLB量測 · 外牆／隔間／家具位置 · 箭頭為設備使用面</text><rect x="3" y="3" width="{w}" height="{d}" fill="#f1f0e8" stroke="#243c3b" stroke-width=".04"/>']
  for kind,aid,lo,hi,obj_floor in mesh_bounds:
   if kind=='stair_tread' and obj_floor in [no,no-1]:
    x,y=xy(lo[0],hi[1]);parts.append(f'<rect x="{x}" y="{y}" width="{hi[0]-lo[0]}" height="{hi[1]-lo[1]}" fill="#e2e7e2" stroke="#718983" stroke-width=".025" stroke-dasharray="{0 if obj_floor==no else .1}"/>')
   if kind!='wall' and aid not in ['large-standard-cubicle','large-accessible-cubicle']:continue
   if not lo[2]<=z+1.2<=hi[2]:continue
   x,y=xy(lo[0],hi[1]);parts.append(f'<rect class="wall" x="{x}" y="{y}" width="{hi[0]-lo[0]}" height="{hi[1]-lo[1]}"/>')
  for box in fixture_boxes:
   if box['floor']!=no or box['asset'] not in LABELS:continue
   lo,hi=box['bounds'];x,y=xy(lo[0],hi[1]);extra=' style="fill:#d9eef0;stroke:#5f8a91"' if box['asset']=='window' else '';parts.append(f'<rect class="fixture"{extra} x="{x}" y="{y}" width="{hi[0]-lo[0]}" height="{hi[1]-lo[1]}"/>');cx,cy=xy((lo[0]+hi[0])/2,(lo[1]+hi[1])/2)
   front=fronts.get(box['name'])
   if front:
    reach=abs(front[0])*(hi[0]-lo[0])/2+abs(front[1])*(hi[1]-lo[1])/2;sx,sy=xy((lo[0]+hi[0])/2+front[0]*reach,(lo[1]+hi[1])/2+front[1]*reach);px,py=xy((lo[0]+hi[0])/2+front[0]*(reach+.45),(lo[1]+hi[1])/2+front[1]*(reach+.45));parts.append(f'<path d="M {sx},{sy} L {px},{py}" stroke="#126f6a" stroke-width=".04"/>');ux,uy=front[0],-front[1];parts.append(f'<path d="M {px-.13*ux-.09*uy},{py-.13*uy+.09*ux} L {px},{py} L {px-.13*ux+.09*uy},{py-.13*uy-.09*ux}" fill="none" stroke="#126f6a" stroke-width=".04"/>')
   if min(hi[0]-lo[0],hi[1]-lo[1])>.55:
    label=LABELS[box['asset']];font=min(max(.22,(w+6)*.011),(hi[0]-lo[0])/(len(label)+.4),(hi[1]-lo[1])*.7);parts.append(f'<text x="{cx}" y="{cy+font*.35}" text-anchor="middle" font-size="{font}">{label}</text>')
  for door in doors:
   if door['floor']!=no:continue
   x,y=door.get('center_m',door.get('position_m',[]))[:2];yaw=door_yaws.get(door['id'],math.radians(door.get('yaw_deg',0)));width=door['width_m'];double=width>1.5;radius=width/2 if double else width
   for sign in ([-1,1] if double else [1] if door.get('hinge_side_override') else [-1]):
    hx=x+sign*width/2*math.cos(yaw);hy=y+sign*width/2*math.sin(yaw);closed=(hx-sign*radius*math.cos(yaw),hy-sign*radius*math.sin(yaw));angle=yaw-sign*math.pi/2;opened=(hx-sign*radius*math.cos(angle),hy-sign*radius*math.sin(angle));a,bp=xy(hx,hy);c,e=xy(*closed);g,h=xy(*opened)
    if door['kind'] in ['sliding','lift']:parts.append(f'<path class="door" d="M {a},{bp} L {c},{e}" stroke-dasharray=".12,.08"/>')
    else:parts.append(f'<path class="door" d="M {a},{bp} L {g},{h} M {c},{e} A {radius},{radius} 0 0 {0 if sign<0 else 1} {g},{h}"/>')
  for room in building.get('review_rooms',{}).get(str(no),[]):
   a,bb,c,e=room['bounds_m'];x,y=xy((a+c)/2,(bb+e)/2);parts.append(f'<text x="{x}" y="{y}" text-anchor="middle" font-size=".3" stroke="#f1f0e8" stroke-width=".06" paint-order="stroke fill">{html.escape(room["label"])}</text>')
  sy=d+5;parts.append(f'<text x="3" y="{sy-.5}" font-size=".3">比例尺（公尺）</text>')
  for i,length in enumerate([1,5,10]):
   yy=sy+i*.65;parts.append(f'<path d="M 3,{yy-.12} v .24 M 3,{yy} h {length} M {3+length},{yy-.12} v .24" stroke="#243c3b" stroke-width=".035"/><text x="{3+length+.2}" y="{yy+.1}" font-size=".28">{length}m</text>')
  parts.append(f'<text x="3" y="{height-.75}" font-size=".24">模型局部座標 · 世界旋轉 {building.get("review_world_yaw",0):g}°</text><text x="3" y="{height-.3}" font-size=".2">GLB SHA256: {html.escape(source_sha)}</text></svg>');(output/f'{bid}-F{no:02d}.svg').write_text(''.join(parts),'utf-8')
