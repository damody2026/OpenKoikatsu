"""Regression: stale or unremeshed geometry must never enter production."""
import json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import street_city_remesh_gate as gate

class RemeshGateTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
  self.engine=self.root/'instant-meshes.exe';self.engine.write_bytes(b'engine')
  self.source=self.root/'source.glb';self.source.write_bytes(b'glTF-source')
  self.output=self.root/'remeshed-candidate.glb';self.output.write_bytes(b'glTF-remeshed')
  self.receipt=self.root/'remesh-report.json'
  self.data=dict(status='passed',tool=str(self.engine),tool_sha256=gate.sha(self.engine),source=str(self.source),source_sha256=gate.sha(self.source),output_sha256=gate.sha(self.output),triangle_cap=100,triangles=12,records=[dict(status='passed',triangles=12,command=[str(self.engine),'source.obj'])])
  self.write();self.mock=patch.object(gate,'ENGINE',self.engine);self.mock.start();self.addCleanup(self.mock.stop)
 def write(self):self.receipt.write_text(json.dumps(self.data))
 def test_accepted_reuse(self):self.assertEqual(gate.verify(self.root)['sha256'],gate.sha(self.output))
 def test_changed_source_requires_remesh(self):
  self.source.write_bytes(b'changed')
  with self.assertRaisesRegex(AssertionError,'geometry changed'):gate.verify(self.root)
 def test_modified_output_rejected(self):
  self.output.write_bytes(b'glTF-other')
  with self.assertRaisesRegex(AssertionError,'output changed'):gate.verify(self.root)
 def test_failed_remesh_rejected(self):
  self.data['status']='rejected_requires_repair';self.write()
  with self.assertRaisesRegex(AssertionError,'did not pass'):gate.verify(self.root)
 def test_no_execution_evidence_rejected(self):
  self.data['records'][0]['command']=[];self.write()
  with self.assertRaisesRegex(AssertionError,'execution evidence'):gate.verify(self.root)
 def test_wrong_adopted_version_rejected(self):
  with self.assertRaisesRegex(AssertionError,'adopted asset'):gate.verify(self.root,'other')

if __name__=='__main__':unittest.main()
