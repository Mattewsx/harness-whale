import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'harness/bridge'))
from harness_ble_bridge import workspace_roster
from whale_v2 import Workspace, Link

class RegistryMembership(unittest.TestCase):
    def test_live_old_plugin_sends_seven_but_device_gets_three(self):
        main=['a','b','c']; archived=['d','e','f','g','h']
        store={'unit':{'name':'workspace','version':2},'global':{'initialized':True,
            'workspaceIds':['w'],'archivedSessionIds':archived},
            'tables':{'workspaces':{'w':{'sessionIds':main+archived}}}}
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'workspace.json'
            path.write_text(json.dumps(store))
            model=Workspace()
            def heartbeat():
                for sid in main+['d','helper1','helper2','helper3']:
                    model.update(dict(sessionId=sid,runId='r',sentAt=1,state='idle'))
            heartbeat(); self.assertEqual(len(model.sessions),7)
            model.set_roster(workspace_roster(path));heartbeat()
            wire=Link(model); wire.snapshot()
            self.assertEqual(len(wire.rows),3)
            self.assertEqual(set(model.keys),set(main))
            store['global']['archivedSessionIds'].append('a')
            path.write_text(json.dumps(store)); model.set_roster(workspace_roster(path));heartbeat()
            self.assertEqual(set(model.keys),{'b','c'})
            path.write_text('{')
            with self.assertRaises(ValueError):workspace_roster(path)
            self.assertEqual(set(model.keys),{'b','c'})
            store['global']['archivedSessionIds'].remove('a')
            path.write_text(json.dumps(store));model.set_roster(workspace_roster(path));heartbeat()
            self.assertEqual(set(model.keys),set(main))
