// Geometry-only investigation. No terminal payload, DSR, splitter edit or adaptive request.
let stage=0,busy=false,finished=false,due=0,phase=0,stableKey=null,stableSince=0,lastEvents=-1;
let requestSent=false,requestApplied=false,finalSent=false;
const contract=__RESIZE_GEOMETRY_CASE_JSON__,caseId=contract.case_id,started=Date.now();
const snapshots=[],observations=[];
if(caseId!=='height-geometry'||JSON.stringify(contract.windows)!=='[[1360,749],[1360,569]]')throw Error('fixed geometry contract');
for(const module of [wind,core,gui,widgets]){
 const expected=(__RUN_DIRECTORY_JSON__+'/app/'+module.name).replace(/\\/g,'/').toLowerCase();
 if(module.path.replace(/\\/g,'/').toLowerCase()!==expected)throw Error('pinned Qt module path guard');
}
function write(name,value){const f=new File(__RUN_DIRECTORY_JSON__+'/'+name,'w');f.write(value);f.close();}
function currentWindow(){try{
 const r=rect(main);if(!Array.isArray(r)||r.length!==4||r.some(v=>!Number.isInteger(v)||Math.abs(v)>1000000))throw Error();
 const size=[r[2]-r[0]+1,r[3]-r[1]+1];if(size.some(v=>v<1||v>4096))throw Error();
 return {rect:r,size,error:null};
}catch(_){return {rect:null,size:null,error:'window_read_failed'};}}
function currentTerminal(){try{
 const raw=evalText(`JSON.stringify((function(){var t=window.activeText;if(!t)return null;function get(k){try{return typeof t[k]==='function'?t[k]():t[k];}catch(e){return null;}}return {connected:get('isConnected'),protocol:get('protocolName'),capacity:get('screenLineCapacity'),line_count:get('visibleLineCount')};})())`);
 const t=raw&&JSON.parse(raw);
 if(!t||typeof t.connected!=='boolean'||t.protocol!=='Telnet'||!Number.isInteger(t.capacity)||t.capacity<0||t.capacity>200||!Number.isInteger(t.line_count)||t.line_count<0||t.line_count>200)throw Error();
 return {...t,error:null};
}catch(_){return {connected:null,protocol:null,capacity:null,line_count:null,error:'terminal_read_failed'};}}
function unavailable(code){return {status:'unavailable',error:code,nodes:[],roles:{terminal:null,editor:null,viewport:null}};}
function widgetGeometry(){
 let rows;try{scan();rows=cached;}catch(_){return unavailable('widget_enumeration_failed');}
 try{
  const byPtr=new Map(rows.map(r=>[r.ptr,r]));
  function ancestors(row){const out=[],seen=new Set();while(row){if(out.length>=32||seen.has(row.ptr))throw Error('parent_chain_bound');seen.add(row.ptr);out.push(row);if(row.parent==='0x0')break;row=byPtr.get(row.parent);if(!row)throw Error('widget_enumeration_failed');}return out;}
  const terminals=rows.filter(r=>r.class==='TerminalView');if(terminals.length!==1)return unavailable('terminal_not_unique');
  const terminal=terminals[0];
  const editors=rows.filter(r=>r.class==='ScintillaEditBase'&&ancestors(r).some(p=>p.ptr===terminal.ptr));
  if(editors.length!==1)return unavailable('editor_not_unique');const editor=editors[0];
  const ports=rows.filter(r=>r.name==='qt_scrollarea_viewport'&&r.parent===editor.ptr);
  if(ports.length!==1)return unavailable('viewport_not_unique');const viewport=ports[0];
  const picked=new Map();for(const role of [terminal,editor,viewport])for(const row of ancestors(role))picked.set(row.ptr,row);
  const splitters=[...picked.values()].filter(r=>isA(ptr(r.ptr),'QSplitter'));
  for(const splitter of splitters)for(const row of rows)if(row.parent===splitter.ptr)picked.set(row.ptr,row);
  if(picked.size>64)return unavailable('widget_node_bound');
  const list=[...picked.values()],ids=new Map(list.map((r,i)=>[r.ptr,i]));
  const nodes=list.map((r,i)=>{
   const p=ptr(r.ptr),box=rect(p),size=[box[2]-box[0]+1,box[3]-box[1]+1];
   if(box.some(n=>!Number.isInteger(n)||Math.abs(n)>1000000)||size.some(n=>n<1||n>4096))throw Error('widget_geometry_failed');
   const input=Memory.alloc(8),output=Memory.alloc(16);input.writeS32(0);input.add(4).writeS32(0);mapGlobal(p,output,input);
   const origin=[output.readS32(),output.add(4).readS32()];if(origin.some(n=>Math.abs(n)>1000000))throw Error('widget_geometry_failed');
   return {id:i,parent_id:ids.has(r.parent)?ids.get(r.parent):null,
    role:r.ptr===terminal.ptr?'terminal':r.ptr===editor.ptr?'editor':r.ptr===viewport.ptr?'viewport':splitters.some(s=>s.ptr===r.parent)?'splitter-child':'ancestor',
    rect:box,size,global_origin:origin,is_splitter:!!isA(p,'QSplitter'),is_handle:!!isA(p,'QSplitterHandle'),visible:!!visible(p)};
  });
  return {status:'observed',error:null,nodes,roles:{terminal:ids.get(terminal.ptr),editor:ids.get(editor.ptr),viewport:ids.get(viewport.ptr)}};
 }catch(error){const code=String(error.message);return unavailable(contract.widget_errors.includes(code)?code:'widget_geometry_failed');}
}
function currentPeer(peer){return {grid:Number.isInteger(peer?.columns)&&Number.isInteger(peer?.rows)?[peer.columns,peer.rows]:null,
 event_count:Array.isArray(peer?.naws_history)?peer.naws_history.length:0,resize_armed:peer?.resize_armed===true,
 error:['OSError','RuntimeError','ValueError'].includes(peer?.error)?peer.error:null};}
function capture(reason,peer,window,terminal){
 if(snapshots.length>=contract.maximum_snapshots)throw Error('snapshot_bound');
 const value={sequence:snapshots.length,phase,reason,elapsed_ms:Math.min(42000,Math.max(0,Date.now()-started)),
 requested_window:contract.windows[phase],window:window||currentWindow(),peer:currentPeer(peer),terminal:terminal||currentTerminal(),widgets:widgetGeometry()};
 snapshots.push(value);send({kind:'resize_geometry_snapshot',...value});return value;
}
function finish(status,error){if(finished)return;finished=true;
 const result={schema:'termforge-resize-geometry-result-v1',status,error:error||null,case_id:caseId,semantic_acceptance:false,
 requested_windows:contract.windows,terminal_input_bytes:0,observations,snapshots,
 widget_geometry_complete:observations.length===2&&observations.every(v=>v.widgets.status==='observed')};
 write('resize-geometry-result.json',JSON.stringify(result));send({kind:'resize_geometry_result',...result});
}
function resetStable(){stableKey=null;stableSince=Date.now();}
send({kind:'probe_ready',station_isolated:true,scenario:'resize-geometry-v1',case_id:caseId});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
 if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
 let peer=null,w=null,t=null;
 try{
  if(wind.base.add(0xc29394).readU8()!==0)throw Error('controller_fault');
  if(Date.now()-started>41000)throw Error('deadline');
  if(stage===0){if(!ready('ui'))return;resize(main,...contract.windows[0]);stage=1;return;}
  if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);stage=2;return;}
  if(stage===2){key(tree,0x1000004);stage=3;return;}
  peer=peerState();w=currentWindow();t=currentTerminal();
  const events=peer?.naws_history;
  if(Array.isArray(events)&&events.length!==lastEvents){capture('naws-change',peer,w,t);lastEvents=events.length;resetStable();}
  if(peer?.error)throw Error('peer_failed');
  if(!peer?.connected){resetStable();return;}
  if(Array.isArray(events)&&events.length===0){resetStable();return;}
  if(!Array.isArray(events)||events.length>64||events.some(v=>!Array.isArray(v)||v.length!==2||v.some(n=>!Number.isInteger(n)||n<0||n>65535)))throw Error('naws_bound');
  if(w.error)throw Error('window_read_failed');
  if(t.error||!t.connected){resetStable();return;}
  if(finalSent){if(peer.complete===true)finish('observation',null);return;}
  if(requestSent&&!requestApplied){
   if(peer.resize_armed!==true)return;
   phase=1;resize(main,...contract.windows[1]);requestApplied=true;resetStable();
   capture('request-returned',peer,currentWindow(),currentTerminal());return;
  }
  if(phase===0&&observations.length===1)return;
  if(phase===1&&peer.columns!==91)throw Error('unexpected_axis');
  if(peer.columns<1||peer.columns>500||peer.rows<1||peer.rows>200)throw Error('naws_bound');
  const signature=JSON.stringify([w,peer.columns,peer.rows,t.capacity,t.connected,t.protocol,events.length]);
  if(t.capacity!==peer.rows||JSON.stringify(events[events.length-1])!==JSON.stringify([peer.columns,peer.rows])){resetStable();return;}
  if(signature!==stableKey){stableKey=signature;stableSince=Date.now();return;}
  if(Date.now()-stableSince<600)return;
  const sample=capture(phase===0?'initial-stable':'final-stable',peer,w,t);
  if(phase===0){
   if(JSON.stringify(w.size)!=='[1360,749]'||peer.columns!==91||peer.rows!==20)throw Error('initial_geometry_differs');
   observations.push(sample);write('fixture-ready',JSON.stringify({grid:[91,20],event_count:events.length}));write('ack-0','observed');
   write('resize-request-1',JSON.stringify({phase:1,window:contract.windows[1]}));requestSent=true;
  }else{
   observations.push(sample);write('ack-1','observed');write('geometry-ready-1',JSON.stringify({phase:1,grid:[peer.columns,peer.rows],event_count:events.length}));finalSent=true;
  }
  due=Date.now()+75;
 }catch(error){
  const raw=String(error.message),code=contract.error_codes.includes(raw)?raw:'controller_fault';
  try{if(peer===null)peer=peerState();capture('fault',peer,w,t);}catch(_){}
  finish('failed',code);
 }finally{busy=false;}
}});
setTimeout(()=>finish('failed','deadline'),42000);
