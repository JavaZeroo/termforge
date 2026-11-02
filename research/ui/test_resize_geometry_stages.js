// Pure V8 evaluation of the actual controller and safe existing-ABI widget bindings.
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const source=fs.readFileSync(process.argv[2],'utf8');
const widgetErrors=['widget_enumeration_failed','terminal_not_unique','editor_not_unique','viewport_not_unique','parent_chain_bound','widget_geometry_failed','widget_node_bound'];
const errorCodes=['window_read_failed','terminal_read_failed','initial_geometry_differs','peer_failed','unexpected_axis','unarmed_naws','naws_bound','snapshot_bound','deadline','controller_fault','geometry_not_ready','phase_order'];
function memory(data=[],offset=0){return {writeS32:v=>{data[offset]=v;},readS32:()=>data[offset]||0,add:n=>memory(data,offset+n)};}
function fixture(options={}){
 const contract={case_id:'height-geometry',windows:[[1360,749],[1360,569]],initial_grid:[91,20],maximum_snapshots:96,widget_errors:widgetErrors,error_codes:errorCodes};
 const state={now:0,window:[1360,749],peer:{connected:true,columns:91,rows:20,naws_history:[[91,20]],resize_armed:false},
  terminal:{connected:true,protocol:'Telnet',capacity:20,line_count:0},
  rows:[{ptr:'main',parent:'0x0',class:'MainWindow',name:''},{ptr:'split',parent:'main',class:'QSplitter',name:''},
   {ptr:'term',parent:'split',class:'TerminalView',name:''},{ptr:'editor',parent:'term',class:'ScintillaEditBase',name:''},
   {ptr:'port',parent:'editor',class:'QWidget',name:'qt_scrollarea_viewport'},{ptr:'sibling',parent:'split',class:'QWidget',name:'PRIVATE'}]};
 const files=new Map(),messages=[],resizes=[],root='/owned/geometry';
 const module=name=>({name,path:root+'/app/'+name,base:{add:()=>({readU8:()=>0})}});
 const context={__RUN_DIRECTORY_JSON__:root,uiThread:1,wind:module('WindTerm.exe'),core:module('Qt5Core.dll'),gui:module('Qt5Gui.dll'),widgets:module('Qt5Widgets.dll'),
  user:{getExportByName:()=>1},Process:{getCurrentThreadId:()=>1},Date:{now:()=>state.now},send:v=>messages.push(v),setTimeout:()=>1,
  File:class{constructor(name){this.name=name;this.data='';}write(v){this.data+=v;}close(){files.set(this.name,{at:state.now,data:this.data});}},
  peerState:()=>state.peer,evalText:code=>state.textAPI?vm.runInNewContext(code,{window:{activeText:state.textAPI}},{timeout:100}):JSON.stringify(state.terminal),
  ready:()=>true,resize:(_,w,h)=>{state.window=[w,resizes.length===0?(options.initialHeight??h):(options.actualHeight??h)];resizes.push({at:state.now,requested:[w,h]});},
  rect:p=>{if(state.failWindow&&p==='main')throw Error('PRIVATE');return p==='main'?[102,102,101+state.window[0],101+state.window[1]]:[0,0,799,359];},
  scan:()=>{if(state.failScan&&state.now>=1000)throw Error('PRIVATE');context.cached=state.rows;},cached:state.rows,main:'main',tree:'tree',
  ptr:v=>v,Memory:{alloc:()=>memory()},mapGlobal:(p,out)=>{if(state.failGlobal)throw Error('PRIVATE');out.writeS32(102);out.add(4).writeS32(102);},
  isA:(p,name)=>state.rows.find(r=>r.ptr===p)?.class===name,visible:()=>true,
  expandAll:()=>{},focus:()=>{},key:()=>{}};
 context.Interceptor={attach:(_,callbacks)=>{context.dispatch=callbacks.onEnter;}};
 vm.createContext(context);vm.runInContext(source.replace('__RESIZE_GEOMETRY_CASE_JSON__',JSON.stringify(contract)),context,{timeout:1000});
 const tick=time=>{state.now=time;vm.runInContext('dispatch()',context,{timeout:100});};
 const file=name=>files.get(root+'/'+name);
 const result=()=>JSON.parse(file('resize-geometry-result.json').data);
 const initial=()=>{tick(0);tick(100);tick(200);tick(1000);tick(1600);};
 const request=()=>{assert(file('ack-0'));state.peer.resize_armed=true;tick(1700);};
 const endpoint=(rows=14,columns=91)=>{state.peer.rows=rows;state.peer.columns=columns;state.terminal.capacity=rows;state.peer.naws_history.push([columns,rows]);};
 const complete=(rows=14)=>{initial();request();endpoint(rows);tick(1800);tick(2400);assert(file('geometry-ready-1'));state.peer.complete=true;state.peer.resize_armed=false;tick(2500);return result();};
 return {state,tick,file,result,initial,request,endpoint,complete,messages,resizes};
}
const cases=[];const test=(name,fn)=>cases.push({name,fn});
test('actual rows14 are recorded without a predicted target or terminal payload',()=>{
 const h=fixture(),r=h.complete();assert.equal(r.status,'observation');assert.equal(r.semantic_acceptance,false);assert.equal(r.terminal_input_bytes,0);
 assert.deepEqual(r.observations.map(v=>v.peer.grid),[[91,20],[91,14]]);assert.deepEqual(h.resizes.map(v=>v.requested),[[1360,749],[1360,569]]);
 assert.equal(r.widget_geometry_complete,true);assert.equal(r.observations[1].widgets.nodes.length,6);
 assert(!JSON.stringify(r).includes('PRIVATE'));assert(!JSON.stringify(r).includes('ptr'));
});
test('clamped actual height is preserved independently of requested569',()=>{
 const h=fixture({actualHeight:641}),r=h.complete();assert.equal(r.status,'observation');
 assert.deepEqual(r.observations[1].requested_window,[1360,569]);assert.deepEqual(r.observations[1].window.size,[1360,641]);
 assert.deepEqual(r.snapshots.find(v=>v.reason==='request-returned').window.size,[1360,641]);
});
test('optional missing duplicate or unsafe widget bindings do not discard window observations',()=>{
 for(const bad of ['missing','duplicate','cycle','scan','global']){
  const h=fixture();
  if(bad==='missing')h.state.rows=h.state.rows.filter(r=>r.ptr!=='port');
  if(bad==='duplicate')h.state.rows.push({ptr:'term2',parent:'split',class:'TerminalView',name:''});
  if(bad==='cycle')h.state.rows.find(r=>r.ptr==='split').parent='term';
  if(bad==='scan')h.state.failScan=true;if(bad==='global')h.state.failGlobal=true;
  const r=h.complete();assert.equal(r.status,'observation');assert.equal(r.widget_geometry_complete,false);
  assert.deepEqual(r.observations[1].peer.grid,[91,14]);assert.equal(r.observations[1].widgets.status,'unavailable');assert.deepEqual(r.observations[1].widgets.nodes,[]);
 }
});
test('baseline mismatch is captured before stopping with no second request',()=>{
 const h=fixture({initialHeight:740});h.initial();const r=h.result();assert.equal(r.status,'failed');assert.equal(r.error,'initial_geometry_differs');
 assert.deepEqual(r.snapshots.find(v=>v.reason==='initial-stable').window.size,[1360,740]);assert.equal(h.resizes.length,1);
});
test('wrong width event is captured before single-axis refusal',()=>{
 const h=fixture();h.initial();h.request();h.endpoint(14,90);h.tick(1800);const r=h.result();assert.equal(r.status,'failed');assert.equal(r.error,'unexpected_axis');
 assert.deepEqual(r.snapshots.find(v=>v.reason==='naws-change'&&v.phase===1).peer.grid,[90,14]);assert.deepEqual(r.snapshots.at(-1).window.size,[1360,569]);
});
test('window read failure preserves earlier and peer measurements',()=>{
 const h=fixture();h.initial();h.request();h.endpoint();h.state.failWindow=true;h.tick(1800);const r=h.result();
 assert.equal(r.error,'window_read_failed');assert.equal(r.observations.length,1);assert.equal(r.snapshots.at(-1).window.rect,null);assert.deepEqual(r.snapshots.at(-1).peer.grid,[91,14]);
});
test('window NAWS and capacity must each stabilize for a full600ms',()=>{
 const h=fixture();h.initial();h.request();h.endpoint();h.tick(1800);h.tick(2399);assert.equal(h.file('ack-1'),undefined);
 h.state.window[1]=570;h.tick(2400);h.tick(2999);assert.equal(h.file('ack-1'),undefined);
 h.state.terminal.capacity=13;h.tick(3000);h.state.terminal.capacity=14;h.tick(3100);h.tick(3699);assert.equal(h.file('ack-1'),undefined);
 h.tick(3700);assert(h.file('ack-1'));
});
test('coalesced NAWS events reset stability even if the endpoint is unchanged',()=>{
 const h=fixture();h.initial();h.request();h.endpoint();h.tick(1800);
 h.state.peer.naws_history.push([91,17],[91,14]);h.tick(2300);h.tick(2400);assert.equal(h.file('ack-1'),undefined);
 h.tick(2900);assert(h.file('ack-1'));
});
test('unchanged NAWS is recorded with actual resized window',()=>{
 const h=fixture();h.initial();h.request();h.tick(1800);h.tick(2400);assert(h.file('ack-1'));
 h.state.peer.complete=true;h.tick(2500);assert.deepEqual(h.result().observations[1].peer.grid,[91,20]);
});
test('deadline keeps measurements and never adds a calibration request',()=>{
 const h=fixture();h.initial();h.request();h.endpoint();h.state.terminal.capacity=20;h.tick(1800);h.tick(41001);
 const r=h.result();assert.equal(r.status,'failed');assert.equal(r.error,'deadline');assert.deepEqual(r.snapshots.at(-1).peer.grid,[91,14]);assert.equal(h.resizes.length,2);
});
test('actual nested terminal getters expose only bounded geometry fields',()=>{
 const h=fixture();h.state.textAPI={isConnected:()=>true,protocolName:()=> 'Telnet',screenLineCapacity:()=>h.state.terminal.capacity,visibleLineCount:()=>0,privateText:'SECRET'};
 const r=h.complete();assert.deepEqual(r.observations[1].terminal,{connected:true,protocol:'Telnet',capacity:14,line_count:0,error:null});assert(!JSON.stringify(r).includes('SECRET'));
});
const results=[];for(const t of cases){try{t.fn();results.push({case:t.name,status:'passed'});}catch(e){results.push({case:t.name,status:'failed',error:e.message});}}
const failed=results.filter(v=>v.status==='failed').length;process.stdout.write(JSON.stringify({schema:'termforge-resize-geometry-controller-test-v1',node_version:process.version,
 actual_source_sha256:crypto.createHash('sha256').update(source).digest('hex'),case_count:results.length,failed,child_processes_spawned:0,sockets_opened:0,target_runs:0,results})+'\n');process.exitCode=failed?1:0;
