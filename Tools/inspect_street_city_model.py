"""Read-only candidate inspection. This command never creates an approval."""
import argparse,json
from pathlib import Path
from street_city_model_gate import inspect_glb,geometry_errors
from street_city_io import write_json
from street_city_catalog import load_asset_catalog

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--asset',required=True);p.add_argument('--file',type=Path,required=True);p.add_argument('--report',type=Path,required=True);args=p.parse_args()
spec=load_asset_catalog(root/'Docs/StreetSocialCity/production')[args.asset]
actual=inspect_glb(args.file);errors=geometry_errors(spec,actual)
report=dict(asset_id=args.asset,source_file=str(args.file.resolve()),status='dimensions_passed_pending_quality_review' if not errors else 'rejected',approved=False,errors=errors,**actual)
write_json(args.report,report);print(json.dumps(report,ensure_ascii=False))
if errors:raise SystemExit(1)
