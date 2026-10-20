"""Capture Blender Python failures even when Blender itself exits with code 0."""
import argparse, json, runpy, sys, traceback
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--script',required=True,type=Path);parser.add_argument('--status-file',required=True,type=Path);parser.add_argument('arguments',nargs=argparse.REMAINDER)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:]);script=args.script.resolve();status=args.status_file.resolve();status.parent.mkdir(parents=True,exist_ok=True)
status.write_text(json.dumps({'status':'running','script':str(script)},indent=2),'utf-8')
arguments=args.arguments[1:] if args.arguments[:1]==['--'] else args.arguments
sys.argv=[str(script),'--',*arguments]
try:
    runpy.run_path(str(script),run_name='__main__')
except BaseException:
    status.write_text(json.dumps({'status':'failed','script':str(script),'error':traceback.format_exc()},ensure_ascii=False,indent=2),'utf-8')
    raise
else:
    status.write_text(json.dumps({'status':'succeeded','script':str(script)},indent=2),'utf-8')
