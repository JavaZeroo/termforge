"""Public plan/profile/text/launcher seams; never launch an original target."""
from __future__ import annotations
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch

from benchmarks.tools import windterm_c0_settings as settings
from research.ui.launch import manifest_digest


def missing():return {'status':'unavailable','type':'undefined','value':None}


class SettingsTests(unittest.TestCase):
    def test_fixed_public_proposal_has_no_actual_values_or_execution_authorization(self):
        plan=json.loads((settings.ROOT/'benchmarks/tools/results/2026-10-29-c0-settings-proposal/plan.json').read_text())
        self.assertFalse(plan['execution_authorized']);self.assertEqual(plan['actual_original_runs'],0)
        self.assertIsNone(plan['actual_settings']);self.assertFalse(plan['c0_verified'])
        self.assertEqual(len(plan['workload']['lines']),14);self.assertEqual(plan['workload']['payload_bytes'],536)
        self.assertEqual(plan['requested_session']['window.rows'],40);self.assertEqual(plan['static_config_hints']['session']['window.rows'],24)
        self.assertEqual(plan['source_files_sha256'],{name:settings.sha(settings.ROOT/name) for name in settings.SOURCE_FILES})

    def snapshot(self):
        return {'screen_line_capacity':23,'widget_geometry':[0,0,905,428],'widget_font':{'family':'Synthetic Fallback','point_size':9,'pixel_size':-1},
                'view_lookup':[{'method':'activeView','type':'object'}],
                'view_metrics':{'window_rows':{'status':'read','type':'number','value':24},'window_columns':{'status':'read','type':'number','value':80}},
                'settings_reads':[{'owner':owner,'key':key,'result':missing()} for owner in settings.OWNERS for key in settings.SETTING_KEYS],
                'discovery':[{'owner':owner,'result':{'status':'owner-unavailable','scanned':0,'truncated':False,'hits':[]}} for owner in settings.OWNERS]}

    def test_actual_readouts_and_unknowns_do_not_inherit_requested_defaults(self):
        actual=settings.settings_snapshot(self.snapshot())
        self.assertEqual(actual['view_metrics']['window_rows']['value'],24)
        self.assertEqual(actual['view_metrics']['window_columns']['value'],80)
        self.assertEqual(actual['document_screen_line_capacity'],23)
        self.assertEqual(actual['widget_font']['family'],'Synthetic Fallback')
        self.assertEqual(actual['widget_font']['point_size'],9)
        self.assertTrue(all(row['result']['value'] is None for row in actual['settings_reads']))
        self.assertEqual(settings.SESSION_REQUESTS['window.rows'],40)

    def test_only_declared_global_keys_change_after_whole_manifest_validation(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);app=out/'app';(app/'global').mkdir(parents=True)
            original={'text.fontFamily1':'Synthetic Original','text.fontSize':10,'terminal.autoComplete.enabled':True,'unrelated':{'keep':[1,2,3]}}
            (app/'global/wind.config').write_text(json.dumps(original,indent=2));(app/'other.bin').write_bytes(b'unchanged')
            manifest={name:settings.sha(app/name) for name in ('global/wind.config','other.bin')}
            (out/'files.json').write_text(json.dumps(manifest))
            session=out/'profile/.wind/profiles/default.v10/terminal/user.sessions';session.parent.mkdir(parents=True);session.write_text(json.dumps([{'session.label':'synthetic','keep':True}]))
            result=settings.configure_copy(out,manifest_digest(manifest))
            self.assertEqual(result['changed_distribution_files'],['global/wind.config'])
            self.assertEqual(json.loads((app/'global/wind.config').read_text()),{**original,**settings.GLOBAL_REQUESTS})
            self.assertEqual((app/'other.bin').read_bytes(),b'unchanged')
            self.assertEqual(json.loads(session.read_text()),[{'session.label':'synthetic','keep':True,**settings.SESSION_REQUESTS}])

    def test_bom_comments_and_unrelated_bytes_survive_exact_key_token_patch(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);app=out/'app';(app/'global').mkdir(parents=True)
            original=b'\xef\xbb\xbf{\r\n  "text.fontFamily1": "Synthetic Original",\r\n  "text.fontSize": 10,\r\n  "terminal.autoComplete.enabled": true,\r\n  "unrelated": 1 // synthetic comment\r\n}\r\n'
            (app/'global/wind.config').write_bytes(original);manifest={'global/wind.config':settings.sha(app/'global/wind.config')};(out/'files.json').write_text(json.dumps(manifest))
            session=out/'profile/.wind/profiles/default.v10/terminal/user.sessions';session.parent.mkdir(parents=True);session.write_text('[{}]')
            result=settings.configure_copy(out,manifest_digest(manifest))
            expected=original.replace(b'"Synthetic Original"',b'"Cascadia Mono"').replace(b'"text.fontSize": 10',b'"text.fontSize": 12').replace(b'"terminal.autoComplete.enabled": true',b'"terminal.autoComplete.enabled": false')
            self.assertEqual((app/'global/wind.config').read_bytes(),expected)
            self.assertTrue(result['all_other_global_bytes_preserved'])

    def test_manifest_failure_precedes_any_configuration_write(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder);(out/'files.json').write_text('{}')
            with self.assertRaisesRegex(ValueError,'Baseline manifest'):settings.configure_copy(out)
            self.assertEqual([path.name for path in out.iterdir()],['files.json'])

    def test_whole_fourteen_line_oracle_is_reused_without_marker_shortcut(self):
        expected,payload=settings.base.workload('0'*32)
        self.assertEqual(len(expected),14);self.assertEqual(len(payload),536)
        self.assertEqual(settings.base.check_text({'lines':expected+[''],'line_count':15},expected)['status'],'passed')
        with self.assertRaises(ValueError):settings.base.check_text({'lines':[expected[-1]],'line_count':1},expected)

    def test_unreadable_statuses_keep_null_and_invalid_private_values_reject(self):
        for status in ('invalid-type','invalid-scalar','read-threw','non-scalar','owner-unavailable'):
            record={'status':status,'type':'string','value':None}
            self.assertEqual(settings.scalar_read(record,'text.fontFamily1'),record)
        for record,key in (({'status':'read','type':'number','value':float('nan')},'window_rows'),({'status':'read','type':'string','value':'C:/private/font'},'text.fontFamily1'),({'status':'read','type':'boolean','value':True},'window_rows')):
            with self.subTest(key=key),self.assertRaises(ValueError):settings.scalar_read(record,key)

    def test_settings_inventory_and_discovery_are_whitelisted(self):
        for change in (lambda row:row['settings_reads'].pop(),lambda row:row['discovery'][0]['result'].update(hits=['privatePath']),lambda row:row['view_metrics'].update(privatePath=missing())):
            snapshot=self.snapshot();change(snapshot)
            with self.assertRaises(ValueError):settings.settings_snapshot(snapshot)

    @unittest.skipUnless(shutil.which('node'),'Node required for external QJSEngine boundary')
    def test_probe_known_calls_and_bounded_name_scalar_reading(self):
        result=subprocess.run(['node',str(settings.ROOT/'research/ui/test_cmd_settings_probe.js')],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_false_plan_public_cli_refuses_before_native_launcher(self):
        for optimized in (False,True):
            with self.subTest(optimized=optimized),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);plan=root/'false.json';plan.write_text(json.dumps({'schema':settings.SCHEMA,'issue':133,'maximum_original_runs':1,'execution_authorized':False}))
                result=subprocess.run([sys.executable,*(['-O'] if optimized else []),str(Path(settings.__file__).resolve()),'run','--plan',str(plan)],capture_output=True,text=True,timeout=10)
                self.assertNotEqual(result.returncode,0);self.assertIn('execution not authorized',result.stderr)
                self.assertEqual([path.name for path in root.iterdir()],['false.json'])

    def test_public_export_false_identity_creates_no_public_output_under_optimization(self):
        for optimized in (False,True):
            with self.subTest(optimized=optimized),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);out=root/'raw';out.mkdir();plan=root/'plan.json';destination=root/'public/result.json'
                plan.write_text(json.dumps({'schema':settings.SCHEMA,'issue':133,'maximum_original_runs':1,'execution_authorized':False}))
                for name,value in (('run.json',{'schema':settings.SCHEMA}),('cleanup.json',{}),('profile-check.json',{})):(out/name).write_text(json.dumps(value))
                result=subprocess.run([sys.executable,*(['-O'] if optimized else []),str(Path(settings.__file__).resolve()),'summarize','--out',str(out),'--plan',str(plan),'--destination',str(destination)],capture_output=True,text=True,timeout=10)
                self.assertNotEqual(result.returncode,0);self.assertIn('unauthorized diagnostic',result.stderr)
                self.assertFalse(destination.exists());self.assertFalse(destination.parent.exists())

    def test_public_prepare_rejects_cmd_meta_path_before_source_read_or_plan_output(self):
        for optimized in (False,True):
            suffix='optimized' if optimized else 'normal';out=settings.ROOT/f'local/settings&REVIEW_MARKER-{suffix}';plan=settings.ROOT/f'local/settings-rejected-{suffix}.json'
            self.assertFalse(out.exists());self.assertFalse(plan.exists())
            result=subprocess.run([sys.executable,*(['-O'] if optimized else []),str(Path(settings.__file__).resolve()),'prepare','--source',str(settings.ROOT/'local/missing-original'),'--out',str(out),'--rea',sys.executable,'--plan',str(plan)],capture_output=True,text=True,timeout=10)
            self.assertNotEqual(result.returncode,0);self.assertIn('Unsupported cmd path',result.stderr);self.assertNotIn('FileNotFoundError',result.stderr)
            self.assertFalse(out.exists());self.assertFalse(plan.exists())

    def test_composed_probe_and_fixed_old_templates_have_separate_identity(self):
        old=settings.OLD_PROBE.read_text(encoding='utf-8');new=settings.PROBE.read_text(encoding='utf-8')
        self.assertEqual(settings.probe_template(),old.split('function state(){')[0]+new)
        self.assertEqual(settings.sha(Path(settings.base.__file__)),'e517f8cab36d55e5d4bdda3fc14d4641e5ce6256991af04762dbf43c25961297')
        self.assertEqual(settings.sha(settings.OLD_PROBE),'9ed0d6ee6bd6e82bae6d9a6802690b3b0b83f437c7b20f0333008b5f6c214756')

    @unittest.skipUnless(sys.platform=='win32','Synthetic WinAPI boundary')
    def test_native_boundary_assigns_before_resume_and_records_cleanup_on_cli_failure(self):
        from research.ui import cmd_owned_launcher as launcher
        for cli_failure in (False,True):
            with self.subTest(cli_failure=cli_failure),tempfile.TemporaryDirectory() as folder:
                out=Path(folder);app=out/'app';app.mkdir();(app/'WindTerm.exe').write_bytes(b'synthetic-native-boundary-only')
                session=out/'profile/.wind/profiles/default.v10/terminal/user.sessions';session.parent.mkdir(parents=True);session.write_text(json.dumps([{'session.protocol':'Shell','session.label':'TF_UI_CMD','session.uuid':'00000000-0000-4000-8000-000000000001'}]))
                events=[];kernel,user=Mock(),Mock();user.CreateDesktopW.return_value=10;user.CloseDesktop.return_value=1;kernel.WaitForSingleObject.return_value=0
                def create(*args):
                    info=args[-1]._obj;info.process,info.thread,info.pid,info.tid=111,222,333,444;events.append('created-suspended');return 1
                kernel.CreateProcessW.side_effect=create;kernel.ResumeThread.side_effect=lambda handle:events.append('resumed') or 1
                class FakeJob:
                    @staticmethod
                    def check(value):
                        if not value:raise RuntimeError('synthetic native call failed')
                    def assign(self,pid):events.append('assigned')
                    def terminate(self):events.append('terminated-job')
                    def drain(self):return {'active':0,'total':1,'terminated':0}
                    def close(self):events.append('closed-job')
                def cli_call(arguments,directory,job,timeout):
                    self.assertEqual(timeout,45);self.assertIn('30000',arguments);events.append('cli-wait')
                    if cli_failure:raise TimeoutError('synthetic CLI timeout')
                    return 0
                template='__UI_THREAD_ID__ __RUN_ID_JSON__ __RUN_DIRECTORY_JSON__ __DESKTOP_JSON__ __TIMEOUT_MS__'
                with patch.object(launcher.c,'WinDLL',side_effect=lambda name,**kw:user if name=='user32' else kernel),patch.object(launcher,'OwnedJob',FakeJob),patch.object(launcher,'isolated_environment',return_value={'SYNTHETIC':'1'}),patch.object(launcher,'cli_command',return_value=[sys.executable]),patch.object(launcher,'run_cli',side_effect=cli_call),patch.object(launcher.time,'sleep'):
                    if cli_failure:
                        with self.assertRaises(TimeoutError):launcher.launch_owned(app,out,Path(sys.executable),'0'*32,template,{'schema':'synthetic-boundary'})
                    else:launcher.launch_owned(app,out,Path(sys.executable),'0'*32,template,{'schema':'synthetic-boundary'})
                self.assertLess(events.index('assigned'),events.index('resumed'));self.assertIn('terminated-job',events);self.assertIn('closed-job',events)
                self.assertEqual(json.loads((out/'cleanup.json').read_text())['active'],0)
                self.assertEqual(json.loads((out/'profile-check.json').read_text()),{'only_synthetic_session':True,'session_count':1})


if __name__=='__main__':unittest.main()
