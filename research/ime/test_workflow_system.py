"""Pure independent E3 byte-contract checks; never invoke clipboard or IME."""
import copy
import hashlib
import unittest

from research.ime.workflow_system_checks import (
    COPY_TEXT, COPY_UTF16_UNITS, PASTE_PREFIX, COMMITTED_INPUT, FINAL_INPUT,
    SELECTION_END_COLUMN, validate_system_input_snapshot, validate_workflow_ime, validate_workflow_system_manifest, validate_workflow_system_progress, validate_system_record, validate_workflow_operations, SYSTEM_PHASES,
)


def snapshot(raw=b''):
    result = {}
    for name in ('A','B','C','F1','F2','F3','F4','F5'):
        value = raw if name == 'B' else b''
        result[name] = dict(raw_bytes=len(value), raw_hex=value.hex(),
                            raw_sha256=hashlib.sha256(value).hexdigest())
    return result



def manifest_fixture():
    names=('C','A','B','F1','F2','F3','F4','F5')
    slots=[dict(name=n,session_id=i,generation=i,window_creation_serial=i,window_token=str(100+i)) for i,n in enumerate(names,1)]
    ids={s['name']:s for s in slots}
    search=dict(find_query_bytes=0,find_query_capacity=15,result_count=0,result_capacity=0,retained_metadata_rows=0,pending_navigation_count=0)
    def runtime(role):
        return dict(schema='termforge-runtime-snapshot-v1',session_id=ids[role]['session_id'],
                    window_creation_serial=ids[role]['window_creation_serial'],search=copy.deepcopy(search),
                    input=dict(queued_bytes=0,high_water_bytes=0,in_flight_bytes=0,failed=False,rejected=0),
                    output=dict(queued_bytes=0,high_water_bytes=0),resize=dict(failed=False),
                    history_rows=0,history_limit=100,rows=7,retained_rows=7)
    hit=dict(line_id=110,version=1,column=0,end_column=14)
    found=copy.deepcopy(slots)
    next(s for s in found if s['name']=='A').update(current_find=hit,visible_lines=[dict(id=111,version=1,text='TF_R108_A_0111 中文 e\u0301')])
    sessions=dict(data_only=False,system_clipboard=True,ime=True,base_observations=235,operation_observations=2,
                  checkpoints=[dict(phase='eight-ready',cycle=0,slots=slots),dict(phase='find-complete',cycle=0,slots=found),
                               dict(phase='cycle-eight',cycle=5,slots=copy.deepcopy(slots))])
    data=dict(schema='termforge-workflow-system-v1',mode='ui-workflow-system',workspace_window_token='90',
              data_only=False,system_clipboard=True,real_ime=True,shortcut_batches=4,
              expected_copy_utf8_hex=COPY_TEXT.encode().hex(),expected_prefix_utf8_hex=PASTE_PREFIX.hex(),
              expected_committed_utf8_hex=COMMITTED_INPUT.hex(),expected_final_utf8_hex=FINAL_INPUT.hex(),
              input_desktop=dict(station='WinSta0',query_only=True,input_name='Default',thread_name='Default'),
              provider=dict(clsid='{81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E}',profile='{FA550B04-5AD7-411F-A5AC-CA038EC515D7}',langid=2052,language_hresult=0,activation_hresult=0),
              key_batches=9,guarded_key_batches=9,snapshots=[])
    for index,phase in enumerate(SYSTEM_PHASES):
        raw=b'' if index<4 else PASTE_PREFIX if index<6 else COMMITTED_INPUT
        targets=[dict(s,received_hex=raw.hex() if s['name']=='B' else '') for s in slots]
        point=dict(seq=index+1,phase=phase,elapsed_ms=100*(index+1),active_sessions=8,key_batches=(0,0,0,0,0,5,6,8,9,9,9,9,9)[index],targets=targets,
                   A=dict(runtime=runtime('A')),B=dict(runtime=runtime('B')))
        if phase.startswith('large-'):point['C']=dict(runtime=runtime('C'))
        if index<2:
            point['A'].update(selected_text_hex=COPY_TEXT.encode().hex(),selection_status='ok',current_find=hit)
            point['A']['runtime']['selection']=dict(range=dict(begin=dict(line_id=110,version=1,column=0),end=dict(line_id=111,version=1,column=21)))
            text='TF_R109_COPY_SENTINEL' if index==0 else COPY_TEXT
            point['clipboard']=dict(sequence=10+index,owner_window_token='90' if index==0 else ids['A']['window_token'],utf16_hex=text.encode('utf-16-le').hex(),text_utf8_hex=text.encode().hex())
        if phase.endswith('requested'):
            point['B' if phase=='paste-requested' else 'C']['runtime']['clipboard']=dict(state='running',error_present=False,native_error=0)
        if phase=='paste-confirmation' or phase.endswith('ready'):
            point['B' if phase=='paste-confirmation' else 'C']['runtime']['last_terminal_clipboard']=dict(state='completed',input_units=42 if phase=='paste-confirmation' else 1000000,cleanup_complete=True,error_present=False,native_error=0)
        if phase=='paste-confirmation':
            point['prompt']=dict(present=True,visible=True,confirm_button=True,owner_window_token='90',window_token='900',target_window_token=ids['B']['window_token'],target_session=3,title='Paste to session 3')
            point['B']['runtime'].update(pending_prepared_paste=True,paste_prompt_present=True)
        if phase=='paste-complete':
            point['B']['runtime'].update(visible=False,last_terminal_paste=dict(target=3,state='completed',total_bytes=51,source_units=42,accepted_bytes=51,sent_bytes=51,in_flight_bytes=0,retained_capacity_bytes=15,error_present=False))
            point['A']['runtime']['visible']=True
        if phase in ('commit-preedit','committed','cancel-preedit','cancelled'):
            preedit=phase.endswith('preedit');committed=phase!='commit-preedit'
            point['ime']=dict(name=phase,session='B',generation=3,received_hex=raw.hex(),committed='你好' if committed else '',result_events=int(committed),composing=preedit,preedit='ni' if preedit else '',ime_error='',character_units=[],composition_events=index,caret_updates=index,caret_positioned=True,caret_client=[10,10,22,36],caret_screen=[110,110,122,136],cell_pixels=[12,26],display_dpi=96,zoom_percent=100,os_dpi=96,monitor_work=[0,0,1920,1080],candidates=[dict(instance=1,**{'class':'CandidateWindow'},rect=[110,136,310,236])] if preedit else [])
        data['snapshots'].append(point)
    obs=sum(1+len(p['targets'])+sum(role in p for role in ('A','B','C'))+sum(p.get(f) is not None for f in ('clipboard','prompt','ime')) for p in data['snapshots'])+15
    data.update(observations=obs,combined_observations=obs+237);sessions.update(system_observations=obs,observations=obs+237)
    return data,sessions


class WorkflowSystemContentTest(unittest.TestCase):
    def test_scroll_endpoints_need_actual_line_roundtrip_and_qpc(self):
        _,sessions=manifest_fixture();identity=sessions['checkpoints'][0]['slots'][1]
        line110=dict(id=110,version=2,text='TF_R108_A_0110 中文 e\u0301')
        line109=dict(id=109,version=3,text='TF_R108_A_0109 中文 e\u0301')
        sessions['checkpoints'].append(dict(phase='reading-before-append',cycle=0,slots=[dict(identity,visible_lines=[line110])]))
        sessions['operation_records']=[];sessions['operation_context']=[]
        marks=[dict(phase='load-started',cycle=0,qpc_frequency=1000,qpc_counter=100),
               dict(phase='background-complete',cycle=0,qpc_frequency=1000,qpc_counter=1000)]
        for i in (1,2):
            fields=dict(operation_id=i,session_id=2,generation=2,window_creation_serial=2)
            sessions['operation_records'].append(dict(fields,schema='termforge-scroll-operation-v1',clock='QPC-ticks',
                scroll_command=i-1,qpc_frequency=1000,received_qpc=i*100+10,state_qpc=i*100+20,submitted_qpc=i*100+30,
                published_revision=i+10,reason='submitted',presented_done=None))
            sessions['operation_context'].append(dict(fields,window_token='102',
                before=dict(published_revision=i+9,top_line=line110 if i==1 else line109),
                after=dict(published_revision=i+10,top_line=line109 if i==1 else line110)))
        self.assertEqual(validate_workflow_operations(sessions,marks)['operations_verified'],2)
        mutations=[lambda s:s['operation_records'][0].update(session_id=3),
                   lambda s:s['operation_records'][0].update(generation=3),
                   lambda s:s['operation_records'][0].update(scroll_command=1),
                   lambda s:s['operation_records'][0].update(qpc_frequency=100),
                   lambda s:s['operation_records'][0].update(state_qpc=99),
                   lambda s:s['operation_records'][1].update(submitted_qpc=None),
                   lambda s:s['operation_records'][1].update(published_revision=11),
                   lambda s:s['operation_records'][1].update(reason='superseded'),
                   lambda s:s['operation_records'][1].update(presented_done=230),
                   lambda s:s['operation_context'][0]['after']['top_line'].update(text='TF_R108_A_0108 中文 e\u0301'),
                   lambda s:s['operation_context'][1]['after'].update(top_line=dict(line110,version=4))]
        for index,mutate in enumerate(mutations):
            wrong=copy.deepcopy(sessions);mutate(wrong)
            with self.subTest(index=index):
                with self.assertRaises(ValueError):validate_workflow_operations(wrong,marks)

    def test_real_hosted_record_cannot_be_relabelled_as_private_desktop(self):
        record=dict(mode='ui-workflow-system',status='collected',assigned=True,gate_released=True,process_reaped=True,
                    timed_out=False,returncode=0,forced_job_termination=False,cleanup_errors=[],evidence_errors=[],error=None,
                    outer_job_before_cleanup=dict(active=0,total=2,terminated=0),outer_job_after=dict(active=0,total=2,terminated=0),
                    lifecycle=[dict(stage=s) for s in ('starting','awaiting-go','job-assigned','releasing-go','finished')])
        validate_system_record(record)
        for field,value in (('assigned',False),('desktop_gone',True),('desktop_gone',False),('gate_released',False),
                            ('returncode',True),('forced_job_termination',True),('cleanup_errors',['failed'])):
            wrong=copy.deepcopy(record);wrong[field]=value
            with self.subTest(field=field):
                with self.assertRaises(ValueError):validate_system_record(wrong)
        record['lifecycle'][2],record['lifecycle'][3]=record['lifecycle'][3],record['lifecycle'][2]
        with self.assertRaises(ValueError):validate_system_record(record)

    def test_ordered_actions_cannot_be_replaced_by_green_snapshots(self):
        data,sessions=manifest_fixture()
        points={p['phase']:p for p in data['snapshots']}
        ids={s['name']:s for s in sessions['checkpoints'][0]['slots']}
        rows=[]
        def emit(kind,code=None,role='B'):
            row=dict(schema='termforge-workflow-system-progress-v1',seq=len(rows)+1,kind=kind,
                     elapsed_ms=(len(rows)+1)*100)
            if kind=='checkpoint':
                point=copy.deepcopy(points[code]);point['elapsed_ms']=row['elapsed_ms']
                points[code]['elapsed_ms']=row['elapsed_ms'];row['snapshot']=point
            elif kind=='completed':row.update(checkpoints=13,key_batches=9,shortcuts=4)
            else:
                row.update(role=role,session_id=ids[role]['session_id'],generation=ids[role]['generation'],window_token=ids[role]['window_token'])
                if kind=='shortcut':row.update(code=code,source='native-WM_KEYDOWN')
                elif kind=='ime-key':row.update(code=code,actual_generation=ids[role]['generation'],guarded=True)
                else:row.update(source='native-BM_CLICK',button=1,dialog_window_token='900')
            rows.append(row)
        emit('checkpoint','selected');emit('shortcut','CtrlShiftC','A');emit('checkpoint','copied')
        emit('shortcut','CtrlShiftV');emit('checkpoint','paste-requested');emit('checkpoint','paste-confirmation')
        emit('confirm');emit('checkpoint','paste-complete')
        for code in ('N','I','H','A','O'):emit('ime-key',code)
        emit('checkpoint','commit-preedit');emit('ime-key','SPACE');emit('checkpoint','committed')
        emit('ime-key','N');emit('ime-key','I');emit('checkpoint','cancel-preedit');emit('ime-key','ESC');emit('checkpoint','cancelled')
        for phase in ('large-cancel','large-close'):
            emit('shortcut','CtrlShiftV','C');emit('checkpoint',phase+'-requested');emit('checkpoint',phase+'-ready')
        emit('completed')
        self.assertEqual(validate_workflow_system_progress(rows,data,sessions)['ordered_events'],28)
        for mutate in (lambda r:r[8].update(guarded=False), lambda r:r[8].update(actual_generation=4),
                       lambda r:r[6].update(dialog_window_token='901'), lambda r:r[1].update(role='B'),
                       lambda r:r[14].update(code='SPACE2'), lambda r:r[13]['snapshot'].update(key_batches=4),
                       lambda r:r.pop(17),lambda r:r.insert(14,copy.deepcopy(r[14]))):
            wrong=copy.deepcopy(rows);mutate(wrong)
            with self.assertRaises(ValueError):validate_workflow_system_progress(wrong,data,sessions)

    def test_companion_rejects_order_target_and_actual_worker_failures(self):
        data,sessions=manifest_fixture()
        self.assertTrue(validate_workflow_system_manifest(data,sessions)['manifest_verified'])
        mutations=[lambda d:d.update(data_only=True),lambda d:d.update(system_clipboard=False),
                   lambda d:d.update(real_ime=False),lambda d:d.update(shortcut_batches=0),
                   lambda d:d['snapshots'].reverse(),lambda d:d['snapshots'][6]['targets'][2].update(received_hex=PASTE_PREFIX.hex()),
                   lambda d:d['snapshots'][1]['clipboard'].update(sequence=10),lambda d:d['snapshots'][1]['clipboard'].update(text_utf8_hex=b'fake'.hex()),
                   lambda d:d['snapshots'][3]['prompt'].update(owner_window_token='103'),
                   lambda d:d['snapshots'][3]['prompt'].update(target_session=2),
                   lambda d:d['snapshots'][10]['C']['runtime']['last_terminal_clipboard'].update(cleanup_complete=False),
                   lambda d:d['snapshots'][12]['targets'][7].update(generation=999),
                   lambda d:d['snapshots'][5].update(key_batches=6),lambda d:d.update(guarded_key_batches=8),
                   lambda d:d['snapshots'][2]['B']['runtime']['input'].update(high_water_bytes=10**9),
                   lambda d:d.update(observations=d['observations']-1)]
        for index,mutate in enumerate(mutations):
            wrong=copy.deepcopy(data);mutate(wrong)
            with self.subTest(index=index):
                with self.assertRaises(ValueError):validate_workflow_system_manifest(wrong,sessions)
        sessions['data_only']=True
        with self.assertRaises(ValueError):validate_workflow_system_manifest(data,sessions)

    def test_actual_ime_geometry_and_exact_commit_are_required(self):
        row = dict(name='commit-preedit', session='B', generation=3,
                   received_hex=PASTE_PREFIX.hex(), committed='', result_events=0,
                   composing=True, preedit='nihao', ime_error='', character_units=[],
                   composition_events=1, caret_updates=1, caret_positioned=True,
                   caret_client=[10,10,22,36], caret_screen=[110,110,122,136],
                   cell_pixels=[12,26], display_dpi=96, zoom_percent=100,
                   os_dpi=96, monitor_work=[0,0,1920,1080],
                   candidates=[dict(instance=1, **{'class':'CandidateWindow'}, rect=[110,136,310,236])])
        validate_workflow_ime(row, 'commit-preedit', 3)
        for field,value in (('received_hex',COMMITTED_INPUT.hex()), ('generation',4),
                            ('result_events',1), ('character_units',[ord('n')]),
                            ('candidates',[]), ('cell_pixels',[13,26]), ('ime_error','error')):
            bad=copy.deepcopy(row);bad[field]=value
            with self.subTest(field=field):
                with self.assertRaises(ValueError):validate_workflow_ime(bad,'commit-preedit',3)
        bad=copy.deepcopy(row);bad['candidates'][0]['rect']=[610,636,810,736]
        with self.assertRaises(ValueError):validate_workflow_ime(bad,'commit-preedit',3)
        row.update(name='cancelled',received_hex=COMMITTED_INPUT.hex(),committed='你好',result_events=1,
                   composing=False,preedit='',composition_events=3,candidates=[])
        validate_workflow_ime(row,'cancelled',3)
        row['candidates']=[dict(instance=999, **{'class':'CandidateWindow'}, rect=[110,136,310,236])]
        with self.assertRaises(ValueError):validate_workflow_ime(row,'cancelled',3)

    def test_independent_literals_and_unicode_boundaries(self):
        self.assertEqual(len(COPY_TEXT.encode('utf-8')), 52)
        self.assertEqual(len(COPY_TEXT.encode('utf-16-le')) // 2, COPY_UTF16_UNITS)
        self.assertEqual(SELECTION_END_COLUMN, 14 + 1 + 2 + 2 + 1 + 1)
        self.assertEqual(len(PASTE_PREFIX), 51)
        self.assertEqual(len(COMMITTED_INPUT), 57)
        self.assertEqual(len(FINAL_INPUT), 58)
        self.assertEqual(PASTE_PREFIX.count(b'\r'), 1)
        self.assertNotIn(b'\n', PASTE_PREFIX)
        self.assertTrue(PASTE_PREFIX.endswith(b'e\xcc\x81'))

    def test_exactly_once_commit_and_no_preedit_or_cancel_leak(self):
        for phase, raw in (('copied', b''), ('paste-complete', PASTE_PREFIX),
                           ('commit-preedit', PASTE_PREFIX), ('committed', COMMITTED_INPUT),
                           ('cancel-preedit', COMMITTED_INPUT), ('cancelled', COMMITTED_INPUT),
                           ('after-paste-close', FINAL_INPUT)):
            validate_system_input_snapshot(snapshot(raw), phase)
        for phase, raw in (
            ('copied', PASTE_PREFIX), ('paste-complete', COPY_TEXT.encode('utf-8')),
            ('paste-complete', PASTE_PREFIX+b'\r'), ('paste-complete', PASTE_PREFIX[:-1]),
            ('commit-preedit', COMMITTED_INPUT), ('committed', PASTE_PREFIX),
            ('committed', COMMITTED_INPUT+'你好'.encode()),
            ('cancelled', COMMITTED_INPUT+b'ni'), ('cancelled', FINAL_INPUT),
            ('after-paste-close', COMMITTED_INPUT)):
            with self.subTest(phase=phase, raw=raw):
                with self.assertRaises(ValueError): validate_system_input_snapshot(snapshot(raw), phase)
        wrong = snapshot(COMMITTED_INPUT)
        wrong['A'], wrong['B'] = wrong['B'], wrong['A']
        with self.assertRaises(ValueError): validate_system_input_snapshot(wrong, 'committed')
        missing = snapshot(COMMITTED_INPUT); del missing['F5']
        with self.assertRaises(ValueError): validate_system_input_snapshot(missing, 'committed')
        forged = copy.deepcopy(snapshot(COMMITTED_INPUT)); forged['B']['raw_bytes'] = True
        with self.assertRaises(ValueError): validate_system_input_snapshot(forged, 'committed')


if __name__ == '__main__':
    unittest.main()
