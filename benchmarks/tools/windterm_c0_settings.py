"""Independent #133 requested/actual settings diagnostic; false plans never launch."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from benchmarks.tools import windterm_cmd as base
from research.ui.launch import EXPECTED_FILES,manifest_digest

SCHEMA='termforge.windterm-c0-settings.v1'
PROBE=ROOT/'research/ui/qt_cmd_settings.js'
OLD_PROBE=ROOT/'research/ui/qt_cmd_diagnostic.js'
GLOBAL_REQUESTS={'text.fontFamily1':'Cascadia Mono','text.fontSize':12,'terminal.autoComplete.enabled':False}
SESSION_REQUESTS={'window.autoAdjustRowsAndColumns':False,'window.columns':120,'window.rows':40,'window.scrollbackLimit':10000}
SETTING_KEYS=tuple(SESSION_REQUESTS)+tuple(GLOBAL_REQUESTS)
OWNERS=('active_text','active_view','terminal','global_settings')
NAMES=('windowRows','windowColumns','screenLineCapacity','settings','config','configuration','getSetting','setting','value','scrollbackLimit','fontFamily1','fontSize','autoCompleteEnabled')
MODULES=('WindTerm.exe','Qt5Core.dll','Qt5Gui.dll','Qt5Widgets.dll','Qt5Qml.dll')
SOURCE_FILES=('benchmarks/tools/windterm_c0_settings.py','research/ui/qt_cmd_settings.js','research/ui/cmd_owned_launcher.py','benchmarks/tools/windterm_cmd.py','research/ui/qt_cmd_diagnostic.js','research/ui/launch.py','research/ui/r1_launch.py','benchmarks/tools/play.py','experiments/conpty/run.py')
require,sha,write_json=base.require,base.digest,base.write_json


def probe_template():
    old=OLD_PROBE.read_text(encoding='utf-8');marker='function state(){'
    require(old.count(marker)==1,'Fixed Qt helper boundary changed')
    return old.split(marker,1)[0]+PROBE.read_text(encoding='utf-8')


def validate_cmd_paths(out):
    for original in (out,base.__file__,sys.executable):
        path=Path(original)
        for text in (str(path),str(path.resolve())):
            require(not any(char in base.CMD_PATH_META or ord(char)<32 for char in text),'Unsupported cmd path characters')


def patch_global(raw):
    """Replace three unique scalar tokens, preserving vendor JSON-like syntax byte-for-byte."""
    spans=[];original={}
    for key,requested in GLOBAL_REQUESTS.items():
        pattern=re.compile(rb'(?m)^[ \t]*"'+re.escape(key.encode('ascii'))+rb'"[ \t]*:[ \t]*(?P<value>"(?:[^"\\\r\n]|\\.)*"|true|false|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?)[ \t]*,?[ \t]*(?://[^\r\n]*)?\r?$')
        matches=list(pattern.finditer(raw));require(len(matches)==1,'Declared global key must occur once on a scalar line')
        match=matches[0];value=json.loads(match['value']);require(type(value) is type(requested),'Static global scalar type differs')
        original[key]=value;spans.append((match.start('value'),match.end('value'),json.dumps(requested).encode('ascii')))
    patched=raw
    for begin,end,replacement in sorted(spans,reverse=True):patched=patched[:begin]+replacement+patched[end:]
    return patched,original


def configure_copy(out,expected_manifest=EXPECTED_FILES):
    """Verify the entire baseline, then apply only declared keys in an owned copy."""
    app=out/'app';manifest=json.loads((out/'files.json').read_text())
    require(manifest_digest(manifest)==expected_manifest,'Baseline manifest identity differs')
    measured={name:sha(app/name) for name in manifest}
    require(measured==manifest,'Prepared baseline files changed')
    path=app/'global/wind.config';before=path.read_bytes();changed,original=patch_global(before)
    (out/'global-config-before.bin').write_bytes(before)
    path.write_bytes(changed)
    configured={name:sha(app/name) for name in manifest}
    require([name for name in manifest if manifest[name]!=configured[name]]==['global/wind.config'],'Unexpected distribution file delta')
    session=out/'profile/.wind/profiles/default.v10/terminal/user.sessions';rows=json.loads(session.read_text())
    require(len(rows)==1,'One synthetic session required');rows[0].update(SESSION_REQUESTS)
    session.write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8',newline='\n')
    delta={'baseline_manifest_sha256':expected_manifest,'configured_manifest_sha256':manifest_digest(configured),'changed_distribution_files':['global/wind.config'],
           'global_before_sha256':hashlib.sha256(before).hexdigest(),'global_after_sha256':sha(path),'keys':{key:{'before':original[key],'requested':value} for key,value in GLOBAL_REQUESTS.items()},
           'all_other_global_bytes_preserved':True,
           'session_requests':SESSION_REQUESTS,'synthetic_session_sha256':sha(session)}
    write_json(out/'configured-files.json',configured);write_json(out/'config-delta.json',delta)
    return delta


def prepare_plan(source,out,rea,destination,execution_authorized=False,run_id=None):
    source,out,rea,destination=[Path(path).resolve() for path in (source,out,rea,destination)]
    require(not out.exists() and not destination.exists(),'Plan/output must be new')
    require(out.is_relative_to(ROOT/'local') and destination.is_relative_to(ROOT/'local'),'Plan/output must be worktree-local')
    validate_cmd_paths(out)
    require(rea.is_file(),'Existing REA CLI missing')
    package=rea.parent.parent/'rea-agents/package.json';require(json.loads(package.read_text())['version']=='6.7.0','REA version differs')
    hashes={name:sha(source/name) for name in MODULES};require(hashes['WindTerm.exe']==base.EXPECTED,'Pinned executable differs')
    run_id=run_id or uuid.uuid4().hex;lines,payload=base.workload(run_id)
    plan={'schema':SCHEMA,'issue':133,'execution_authorized':execution_authorized,'maximum_original_runs':1,'source_base_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True,cwd=ROOT).strip(),
          'source':str(source),'out':str(out),'rea':str(rea),'run_id':run_id,'rea_capture_seconds':30,'cli_wait_timeout_seconds':45,'whole_lifecycle_bound_seconds':None,
          'module_sha256':hashes,'baseline_files_manifest_sha256':EXPECTED_FILES,'requested_session':SESSION_REQUESTS,'requested_global':GLOBAL_REQUESTS,
          'static_resource_sha256':{name:sha(source/name) for name in ('global/wind.config','terminal/configs/session.config','terminal/configs/statusbar.config')},
          'static_config_hints':{'evidence_kind':'static-only','global':patch_global((source/'global/wind.config').read_bytes())[1],'session':{key:json.loads((source/'terminal/configs/session.config').read_bytes())[key] for key in SESSION_REQUESTS}},
          'expected_configured_global_sha256':hashlib.sha256(patch_global((source/'global/wind.config').read_bytes())[0]).hexdigest(),
          'source_files_sha256':{name:sha(ROOT/name) for name in SOURCE_FILES},'composed_template_sha256':hashlib.sha256(probe_template().encode()).hexdigest(),
          'helper_prefix_sha256':hashlib.sha256(OLD_PROBE.read_text(encoding='utf-8').split('function state(){',1)[0].encode()).hexdigest(),
          'workload':{'lines':lines,'payload_hex':payload.hex(),'payload_bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest()},
          'scope':'Non-input-desktop small-cmd settings read diagnostic, not C0 visibility, history eviction, terminal timing or performance acceptance'}
    destination.parent.mkdir(parents=True,exist_ok=True);write_json(destination,plan)
    return plan


def scalar_read(record,key):
    require(isinstance(record,dict) and set(record)=={'status','type','value'},'Invalid scalar read shape')
    require(record['status'] in ('owner-unavailable','unavailable','non-scalar','read-threw','read','invalid-type','invalid-scalar'),'Unknown read status')
    require(record['type'] in ('undefined','null','unknown','object','function','number','boolean','string'),'Unknown read type')
    value=record['value']
    if record['status']!='read':require(value is None,'Unreadable value must remain null')
    elif record['type']=='number':require(key not in ('text.fontFamily1','window.autoAdjustRowsAndColumns','terminal.autoComplete.enabled') and type(value) in (int,float) and value==value and abs(value)<=10000000,'Invalid numeric setting')
    elif record['type']=='boolean':require(key in ('window.autoAdjustRowsAndColumns','terminal.autoComplete.enabled') and type(value) is bool,'Invalid boolean setting')
    elif record['type']=='string':require(key=='text.fontFamily1' and isinstance(value,str) and len(value)<=128 and not any(char in value for char in '\\/\r\n'),'Unexpected private/string setting')
    else:require(record['type']=='null' and value is None,'Unexpected scalar type')
    return copy.deepcopy(record)


def settings_snapshot(snapshot):
    terminal=base.terminal_settings(snapshot)
    metrics=snapshot['view_metrics'];require(set(metrics)=={'window_rows','window_columns'},'View metrics differ')
    selected={key:scalar_read(metrics[key],key) for key in metrics}
    reads=snapshot['settings_reads'];require(len(reads)==len(OWNERS)*len(SETTING_KEYS),'Settings read inventory differs')
    require([(row['owner'],row['key']) for row in reads]==[(owner,key) for owner in OWNERS for key in SETTING_KEYS],'Settings read identity differs')
    selected_reads=[{'owner':row['owner'],'key':row['key'],'result':scalar_read(row['result'],row['key'])} for row in reads]
    lookup=snapshot['view_lookup'];require(1<=len(lookup)<=2 and [row['method'] for row in lookup]==['activeView','view'][:len(lookup)] and all(set(row)=={'method','type'} and row['type'] in ('object','null','unavailable','threw','undefined','function','number','boolean','string') for row in lookup),'View lookup inventory differs')
    discovery=snapshot['discovery'];require([row['owner'] for row in discovery]==list(OWNERS),'Discovery owner differs')
    for row in discovery:
        value=row['result'];require(set(value)=={'status','scanned','truncated','hits'} and value['status'] in ('owner-unavailable','enumerated-names-only','enumeration-threw'),'Discovery shape differs')
        require(type(value['scanned']) is int and 0<=value['scanned']<=128 and type(value['truncated']) is bool and isinstance(value['hits'],list) and len(value['hits'])<=len(NAMES) and len(set(value['hits']))==len(value['hits']) and all(name in NAMES for name in value['hits']),'Unbounded/unlisted discovery')
    return {'producer_console':None,'view_metrics':selected,'document_screen_line_capacity':terminal['screen_line_capacity'],'widget_geometry':terminal['widget_geometry'],'widget_font':terminal['widget_font'],'view_lookup':lookup,'settings_reads':selected_reads,'discovery':discovery}


def summarize(out,plan_path):
    out,plan_path=Path(out),Path(plan_path);plan=json.loads(plan_path.read_text());read=lambda name:json.loads((out/name).read_text())
    identity,cleanup,profile=read('run.json'),read('cleanup.json'),read('profile-check.json')
    require(plan['schema']==identity['schema']==SCHEMA and plan['issue']==133 and plan['maximum_original_runs']==1 and plan['execution_authorized'] is True,'Wrong or unauthorized diagnostic identity')
    require(plan['source_files_sha256']==identity['source_files_sha256']=={name:sha(ROOT/name) for name in SOURCE_FILES},'Actual source inventory changed')
    require(identity['plan_sha256']==sha(plan_path) and identity['run_id']==plan['run_id'] and identity['sha256']==plan['module_sha256']['WindTerm.exe']==base.EXPECTED,'Run/plan/module identity differs')
    require({name:sha(out/'app'/name) for name in MODULES}==plan['module_sha256'],'Full module hash differs')
    require(identity['desktop']=='owned-non-input' and identity['profile']=='synthetic-local-only' and cleanup['run_id']==identity['run_id'] and cleanup['target_pid']==identity['pid'] and cleanup['active']==0 and profile=={'only_synthetic_session':True,'session_count':1},'Owned cleanup/profile identity differs')
    require(identity['rea_capture_seconds']==30 and identity['cli_wait_timeout_seconds']==45 and identity['whole_lifecycle_bound_seconds'] is None,'Capture lifecycle bound differs')
    from research.ui.cmd_owned_launcher import render_template
    require(identity['composed_template_sha256']==plan['composed_template_sha256']==hashlib.sha256(probe_template().encode()).hexdigest(),'Composed template identity differs')
    require((out/'probe.js').read_text(encoding='utf-8')==render_template(probe_template(),out.resolve(),plan['run_id'],identity['tid'],'TermForgeCmdSettings-'+plan['run_id']),'Exact injected source differs')
    lines,payload=base.workload(plan['run_id']);work=read('workload.json')
    require(work=={'run_id':plan['run_id'],'lines':lines,'payload_bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest()} and (out/'payload.bin').read_bytes()==payload,'Full oracle identity differs')
    require(plan['workload']=={'lines':lines,'payload_hex':payload.hex(),'payload_bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest()},'Planned oracle differs')
    delta=read('config-delta.json');configured=read('configured-files.json');baseline=read('files.json')
    require(plan['requested_session']==SESSION_REQUESTS and plan['requested_global']==GLOBAL_REQUESTS and delta['baseline_manifest_sha256']==EXPECTED_FILES,'Request/baseline metadata differs')
    require(set(configured)==set(baseline) and manifest_digest(baseline)==EXPECTED_FILES and manifest_digest(configured)==delta['configured_manifest_sha256'] and {name:sha(out/'app'/name) for name in configured}==configured,'Configured package identity differs')
    expected_global,before=patch_global((out/'global-config-before.bin').read_bytes())
    require([name for name in baseline if baseline[name]!=configured[name]]==delta['changed_distribution_files']==['global/wind.config'] and delta['session_requests']==SESSION_REQUESTS and delta['all_other_global_bytes_preserved'] is True and delta['keys']=={key:{'before':before[key],'requested':value} for key,value in GLOBAL_REQUESTS.items()},'Controlled config delta differs')
    require(sha(out/'global-config-before.bin')==delta['global_before_sha256']==plan['static_resource_sha256']['global/wind.config'] and sha(out/'app/global/wind.config')==delta['global_after_sha256']==plan['expected_configured_global_sha256'],'Config resource SHA differs')
    require((out/'app/global/wind.config').read_bytes()==expected_global,'Unrequested global bytes changed')
    require(plan['static_config_hints']=={'evidence_kind':'static-only','global':before,'session':{key:json.loads((out/'app/terminal/configs/session.config').read_bytes())[key] for key in SESSION_REQUESTS}},'Static hints differ from fixed resources')
    produced=read('producer.json');require(produced['run_id']==plan['run_id'] and produced['status']=='complete' and produced['error'] is None and produced['sha256']==work['sha256'] and produced['payload_bytes']==len(payload) and produced['consumer_done'] is None and produced['presented_done'] is None,'Producer identity differs')
    console={key:produced['console'].get(key) for key in ('vt_enable_succeeded','original_mode','columns','rows','buffer_columns','buffer_rows')};require(console['vt_enable_succeeded'] is True and all(value is None or type(value) is int and 0<=value<=0xffffffff for key,value in console.items() if key!='vt_enable_succeeded'),'Invalid producer console')
    payloads=base.evidence_payloads(identity,read('evidence.json'),sha(out/'probe.js'));finished=[row for row in payloads if row.get('kind')=='settings_complete'];require(len(finished)==1,'One complete settings snapshot required')
    snapshot=finished[0];require(snapshot['module_guards']==[{'name':name,'path_guard_passed':True} for name in MODULES],'Runtime module path guard differs')
    correctness=base.check_text(snapshot,lines);actual=settings_snapshot(snapshot);actual['producer_console']=console
    raw_names=('run.json','workload.json','payload.bin','producer.json','evidence.json','cleanup.json','profile-check.json','config-delta.json','configured-files.json','files.json','global-config-before.bin','probe.js','stderr.txt')
    return {'schema':SCHEMA,'issue':133,'status':'partial-settings-diagnostic','correctness':correctness,'requested':{'session':SESSION_REQUESTS,'global':GLOBAL_REQUESTS},'actual_settings':actual,
            'run_id':plan['run_id'],'source_base_commit':plan['source_base_commit'],'source_files_sha256':plan['source_files_sha256'],'plan_sha256':sha(plan_path),'module_sha256':plan['module_sha256'],
            'composed_template_sha256':plan['composed_template_sha256'],'static_config_hints':plan['static_config_hints'],'controlled_configuration_delta':{key:value for key,value in delta.items() if key!='synthetic_session_sha256'},'raw_records_sha256':{name:sha(out/name) for name in raw_names},
            'actual_original_runs':1,'rea_capture_seconds':30,'cli_wait_timeout_seconds':45,'whole_lifecycle_bound_seconds':None,'consumer_done':None,'presented_done':None,'c0_verified':False,'font_file_identity_verified':False,'glyph_fallback_verified':False,'history_eviction_verified':False,'performance_gate':'not_evaluated','cleanup':{'owned_active':0,'only_synthetic_session':True},
            'limitations':['Non-input desktop cannot establish visible and unobstructed C0.','Requested configuration and static hints are separate from actual readouts.','Screen capacity is not PTY rows; console/window, view and widget font remain separate.','Name enumeration caps retained entries, not internal engine time.','Producer uses run_id/payload identity; process ancestry was not independently observed.','No font installation, glyph/fallback identity, history eviction, terminal endpoints, observer calibration or performance acceptance.']}


def run_plan(plan_path):
    plan_path=Path(plan_path).resolve();plan=json.loads(plan_path.read_text())
    require(plan.get('schema')==SCHEMA and plan.get('issue')==133 and plan.get('maximum_original_runs')==1,'Unexpected diagnostic plan')
    require(plan.get('execution_authorized') is True,'Offline proposal only; execution not authorized')
    require(plan['source_base_commit']==subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip() and plan['source_files_sha256']=={name:sha(ROOT/name) for name in SOURCE_FILES},'Planned source changed')
    require(plan['requested_session']==SESSION_REQUESTS and plan['requested_global']==GLOBAL_REQUESTS and plan['rea_capture_seconds']==30 and plan['cli_wait_timeout_seconds']==45 and plan['whole_lifecycle_bound_seconds'] is None,'Planned scope differs')
    require(plan['composed_template_sha256']==hashlib.sha256(probe_template().encode()).hexdigest(),'Composed probe changed')
    out,source,rea=[Path(plan[key]) for key in ('out','source','rea')]
    validate_cmd_paths(out)
    require({name:sha(source/name) for name in MODULES}==plan['module_sha256'],'Pinned modules changed')
    require(not out.exists(),'Output must be new')
    with plan_path.with_suffix('.attempted').open('x') as marker:marker.write('One attempt; never retry.\n')
    app,_=base.prepare_run(source,out,plan['run_id']);configure_copy(out)
    from research.ui.cmd_owned_launcher import launch_owned
    launch_owned(app,out,rea,plan['run_id'],probe_template(),{'schema':SCHEMA,'source_files_sha256':plan['source_files_sha256'],'plan_sha256':sha(plan_path),'composed_template_sha256':plan['composed_template_sha256']})


if __name__=='__main__':
    parser=argparse.ArgumentParser();commands=parser.add_subparsers(dest='command',required=True)
    prepare=commands.add_parser('prepare')
    for key in ('source','out','rea','plan'):prepare.add_argument('--'+key,type=Path,required=True)
    prepare.add_argument('--authorize-execution',action='store_true',help='Only after separate explicit authorization; create a fresh plan, never edit an old proposal')
    prepare.add_argument('--run-id',help='Reuse the reviewed synthetic nonce when creating the separate authorized plan')
    run=commands.add_parser('run');run.add_argument('--plan',type=Path,required=True)
    export=commands.add_parser('summarize')
    for key in ('out','plan','destination'):export.add_argument('--'+key,type=Path,required=True)
    args=parser.parse_args()
    if args.command=='prepare':prepare_plan(args.source,args.out,args.rea,args.plan,args.authorize_execution,args.run_id);print(json.dumps({'prepared':True,'execution_authorized':args.authorize_execution}))
    elif args.command=='run':run_plan(args.plan)
    else:
        result=summarize(args.out,args.plan);args.destination.parent.mkdir(parents=True,exist_ok=True);write_json(args.destination,result);print(json.dumps({'exported':True,'status':result['status']}))
