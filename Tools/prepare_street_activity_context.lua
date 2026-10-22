-- Read current semantic contracts, never infer office names from an old drawing.
local code=[====[
import sys,json
from pathlib import Path
sys.path.insert(0,'C:/portable/OpenKoikatsu/Tools')
from street_city_campus_contract import context
rows={k:{'building':context(k)['building'],'rooms':context(k)['rooms']} for k in ['T4','RB','SC','IB']}
Path('C:/portable/OpenKoikatsu/output/street-social-city-production/activity-scenes-v1/campus-supplement.json').write_text(json.dumps(rows,ensure_ascii=False),'utf8')
]====]
assert(os.execute('python -c "'..code:gsub('\r?\n','; ')..'"'))
