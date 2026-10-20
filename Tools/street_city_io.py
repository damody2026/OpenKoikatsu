"""Replace generated artifacts atomically, including files open in a preview."""
from pathlib import Path
import json
import time

def replace(temp,path):
    # Explorer/preview readers may hold a transient sharing lock. Preserve the
    # previous complete file and retry the atomic operation, never delete it.
    for attempt in range(8):
        try:temp.replace(path);return
        except PermissionError:
            if attempt==7:raise
            time.sleep(.1*(attempt+1))

def write_json(path,value):
    path=Path(path);temp=path.with_suffix(path.suffix+'.tmp')
    with temp.open('w',encoding='utf-8',buffering=1024*1024) as stream:
        json.dump(value,stream,ensure_ascii=False,separators=(',',':'))
    replace(temp,path)

def write_text(path,value):
    path=Path(path);temp=path.with_suffix(path.suffix+'.tmp')
    temp.write_text(value,'utf-8');replace(temp,path)

from contextlib import contextmanager

@contextmanager
def text_stream(path,encoding='utf-8',newline=None):
    """Streaming CSV/JSONL export with the same atomic replacement contract."""
    path=Path(path);temp=path.with_suffix(path.suffix+'.tmp')
    with temp.open('w',encoding=encoding,newline=newline,buffering=1024*1024) as stream:
        yield stream
    replace(temp,path)
