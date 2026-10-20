"""Publish metadata without exposing a partially written JSON/HTML to readers."""
import os,time
from pathlib import Path

def write_text(path,text,encoding='utf-8'):
 path=Path(path);temp=path.with_name(path.name+f'.{os.getpid()}.tmp')
 temp.write_text(text,encoding)
 for attempt in range(12):
  try:temp.replace(path);return
  except PermissionError:
   if attempt==11:raise
   time.sleep(.01)
