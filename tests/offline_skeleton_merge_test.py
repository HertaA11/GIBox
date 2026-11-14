import copy
import importlib.util
import json
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location('merge', Path(__file__).resolve().parents[1] / 'tools/merge_offline_skeletons.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    rec = dict(mesh_id='capture:1', name='Body', mesh='Body', vertices=3, submeshes=1,
               bones=['Bip001'], parents=[-1], bindposes=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1])
    doc = dict(format='gibox-offline-skeletons', version=1, build='abc', meshes=[rec],
               prefabs=[dict(owner='Avatar_Girl_Catalyst_Vodyanitsa', kind='character', renderers=[['Body',0]])])
    a,b,out = [d / p for p in ('a.json','b.json','out.json')]
    a.write_text(json.dumps(doc)); b.write_text(json.dumps(doc))
    assert m.merge([a,b],out) == (1,1)
    parsed=json.loads(out.read_text()); assert parsed['prefabs'][0]['renderers']==[['Body',0]]
    before=out.read_bytes()
    bad=copy.deepcopy(doc);bad['meshes'][0]['bones']=['Different'];b.write_text(json.dumps(bad))
    try: m.merge([a,b],out)
    except ValueError: pass
    else: raise AssertionError('conflicting bones accepted')
    assert out.read_bytes()==before
    bad=copy.deepcopy(doc);bad['build']='other';b.write_text(json.dumps(bad))
    try: m.merge([a,b],out)
    except ValueError: pass
    else: raise AssertionError('mixed game versions accepted')
    bad=copy.deepcopy(doc);bad['meshes'][0]['parents']=[0];b.write_text(json.dumps(bad))
    try: m.merge([a,b],out)
    except ValueError: pass
    else: raise AssertionError('parent cycle accepted')
print('Skeleton contribution merge: PASS (deduplication, indices, conflict and version rejection, atomic output)')
