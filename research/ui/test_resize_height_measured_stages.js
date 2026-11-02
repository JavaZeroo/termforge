// Execute the actual height controller in pure V8: no target, socket or native UI.
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const original=fs.readFileSync(process.argv[2],'utf8');
const specs={
 'height-retention-measured':{grids:[[91,20],[91,14],[91,20]],labels:['initial-body','height-shrunk-unmodified','height-restored-unmodified'],resize:[1,2]}
};
const markerLabels=['HTOP','HARD_A','HARD_B','u','v','w','LOWEND'];
function initialLines(){const a=Array(20).fill('');a[1]='HTOP';a[5]='HARD_A';a[6]='HARD_B';a[11]=' '.repeat(89)+'uvw';a[18]='LOWEND';return a;}
function fixture(caseId='height-retention-measured'){
    const spec=specs[caseId],contract={case_id:caseId,labels:spec.labels,windows:spec.grids.map(([w,h])=>[w===91?1360:1000,h===20?749:569]),
        grids:spec.grids,resize_phases:spec.resize,marker_labels:markerLabels};
    const state={now:0,window:[1360,749],origin:[113,-27],peer:{connected:true,columns:91,rows:20,phase:null,naws_history:[[91,20]]},
        terminal:{connected:true,protocol:'Telnet',capacity:20,line_count:20,lines:initialLines(),markers:[]}};
    const files=new Map(),messages=[],resizes=[],root='/owned/resize-height-measured';
    const module=name=>({name,path:root+'/app/'+name,base:{add:()=>({readU8:()=>0})}});
    const context={__RUN_DIRECTORY_JSON__:root,uiThread:1,wind:module('WindTerm.exe'),core:module('Qt5Core.dll'),gui:module('Qt5Gui.dll'),widgets:module('Qt5Widgets.dll'),
        user:{getExportByName:()=>1},Process:{getCurrentThreadId:()=>1},Date:{now:()=>state.now},send:value=>messages.push(value),setTimeout:()=>1,
        File:class{constructor(name){this.name=name;this.data='';}write(value){this.data+=value;}close(){files.set(this.name,{at:state.now,data:this.data});}},
        peerState:()=>state.peer,evalText:source=>state.textAPI?vm.runInNewContext(source,{window:{activeText:state.textAPI}},{timeout:100}):JSON.stringify(state.terminal),ready:()=>true,
        resize:(_,w,h)=>{state.window=[w,h];resizes.push({at:state.now,size:[w,h]});},
        rect:()=>[...state.origin,state.origin[0]+state.window[0]-1,state.origin[1]+state.window[1]-1],
        scan:()=>{},expandAll:()=>{},focus:()=>{},key:()=>{},main:1,tree:2};
    context.Interceptor={attach:(_,callbacks)=>{context.dispatch=callbacks.onEnter;}};
    vm.createContext(context);vm.runInContext(original.replace('__RESIZE_HEIGHT_MEASURED_CASE_JSON__',JSON.stringify(contract)),context,{timeout:1000});
    const tick=now=>{state.now=now;vm.runInContext('dispatch()',context,{timeout:100});};
    const file=name=>files.get(root+'/'+name);
    const phase=index=>{
        state.peer.case_id=caseId;state.peer.phase=index;state.peer.phase_label=spec.labels[index];state.peer.frozen_grid=contract.grids[index];
        state.peer.resize_armed=null;state.peer.records??=[];
        state.peer.records[index]={cursor_reply:[19,7],cursor_reply_private:false,response_hex:'1b5b31393b3752',frozen_grid:contract.grids[index]};
    };
    tick(0);tick(100);tick(200);tick(1000);tick(1600);assert(file('fixture-ready'));
    const observe=(index,time)=>{
        if(spec.resize.includes(index)){
            assert(file('resize-request-'+index));state.peer.resize_armed=index;tick(time);
            state.peer.columns=spec.grids[index][0];state.peer.rows=spec.grids[index][1];
            state.terminal.capacity=state.peer.rows;state.peer.naws_history.push(contract.grids[index]);
            tick(time+100);tick(time+700);assert(file('resize-ready-'+index));
            phase(index);tick(time+800);tick(time+1200);assert(file('ack-'+index));return time+1300;
        }
        phase(index);tick(time);tick(time+400);assert(file('ack-'+index));return time+500;
    };
    const through=index=>{let time=1700;for(let i=0;i<=index;i++)time=observe(i,time);return time;};
    return {state,tick,file,phase,observe,through,resizes,messages,spec,contract};
}
const cases=[];function test(name,action){cases.push({name,action});}
test('fixed measured-height schedule records all3 states including independently checked restore',()=>{
 let total=0;
 for(const caseId of Object.keys(specs)){
  const h=fixture(caseId),last=h.spec.grids.length-1,time=h.through(last);h.state.peer.complete=true;h.tick(time);
  const r=JSON.parse(h.file('resize-height-measured-result.json').data);
  assert.equal(r.status,'observed-fixed-resize-height-measured-states');assert.equal(r.observations.length,last+1);
  assert.deepEqual(r.observations.map(o=>o.frozen_grid),h.spec.grids);
  assert.deepEqual(h.resizes.map(o=>o.size),h.contract.windows);total+=r.observations.length;
 }
 assert.equal(total,3);
});
test('all changes wait for prior observation acknowledgment',()=>{
 for(const caseId of Object.keys(specs)){
  const h=fixture(caseId);let time=h.through(0);
  for(let i=1;i<h.spec.grids.length;i++){
   assert(h.file('ack-'+(i-1)));assert(h.file('resize-request-'+i));assert.equal(h.file('ack-'+i),undefined);
   assert.equal(h.resizes.length,i);time=h.observe(i,time);
  }
 }
});
test('initial missing sentinel or wrong DSR prevents first resize',()=>{
 for(const bad of ['missing','duplicate','DSR']){
  const h=fixture();h.phase(0);
  if(bad==='missing')h.state.terminal.lines[1]='';
  else if(bad==='duplicate')h.state.terminal.lines[0]='HTOP';
  else h.state.peer.records[0].cursor_reply=[18,7];
  h.tick(1700);h.tick(2100);assert.equal(h.file('ack-0'),undefined);
  assert.equal(h.file('resize-request-1'),undefined);assert.equal(JSON.parse(h.file('resize-height-measured-result.json').data).status,'failed');
 }
});
test('later loss and DSR outside 14-row viewport remain observations',()=>{
 const h=fixture();let time=h.through(0);h.state.terminal.lines=[''];h.state.terminal.line_count=1;h.observe(1,time);
 const r=h.messages.filter(x=>x.kind==='resize_height_measured_phase')[1];assert.deepEqual(r.cursor_reply,[19,7]);assert.deepEqual(r.frozen_grid,[91,14]);
});
test('height NAWS changes reset all 600ms of stability',()=>{
 const h=fixture(),time=h.through(0);h.state.peer.resize_armed=1;h.tick(time);
 h.state.peer.rows=14;h.state.terminal.capacity=14;h.state.peer.naws_history.push([91,14]);h.tick(time+100);
 h.state.peer.naws_history.push([91,17],[91,14]);h.tick(time+650);h.tick(time+700);
 assert.equal(h.file('resize-ready-1'),undefined);h.tick(time+1250);assert(h.file('resize-ready-1'));
});
test('post-input document changes restart the full 400ms stability interval',()=>{
 const h=fixture();h.phase(0);h.tick(1700);h.tick(2000);assert.equal(h.file('ack-0'),undefined);
 h.state.terminal.top=1;h.tick(2050);h.tick(2100);assert.equal(h.file('ack-0'),undefined);
 h.tick(2449);assert.equal(h.file('ack-0'),undefined);h.tick(2450);assert(h.file('ack-0'));
});
test('initial sentinels are checked after bounded observation stability',()=>{
 const h=fixture();h.phase(0);h.state.terminal.lines[1]='';h.tick(1700);assert.equal(h.file('resize-height-measured-result.json'),undefined);
 h.state.terminal.lines[1]='HTOP';h.tick(1900);h.tick(2299);assert.equal(h.file('ack-0'),undefined);
 h.tick(2300);assert(h.file('ack-0'));
});
test('fixed deadline rejects missing input without extending the schedule',()=>{
 const h=fixture();h.tick(41001);assert.equal(h.file('ack-0'),undefined);
 const result=JSON.parse(h.file('resize-height-measured-result.json').data);assert.equal(result.status,'failed');assert.match(result.error,/bounded scenario deadline/);
});
test('fixed geometry grid and phase identities do not auto-calibrate',()=>{
 for(const bad of ['grid','capacity','geometry']){
  const h=fixture(),time=h.through(0);h.state.peer.resize_armed=1;h.tick(time);
  h.state.peer.rows=bad==='grid'?13:14;h.state.terminal.capacity=bad==='capacity'?20:h.state.peer.rows;
  h.state.peer.naws_history.push([91,h.state.peer.rows]);if(bad==='geometry')h.state.window[1]++;
  h.tick(time+100);h.tick(time+700);assert.equal(h.file('resize-ready-1'),undefined);
  if(bad!=='capacity')assert.equal(JSON.parse(h.file('resize-height-measured-result.json').data).status,'failed');
  else {h.tick(42000);assert.equal(JSON.parse(h.file('resize-height-measured-result.json').data).status,'failed');}
 }
 for(const bad of ['case','label']){
  const h=fixture();h.phase(0);if(bad==='case')h.state.peer.case_id='wrong';else h.state.peer.phase_label='wrong';h.tick(1700);
  assert.equal(h.file('ack-0'),undefined);assert.equal(JSON.parse(h.file('resize-height-measured-result.json').data).status,'failed');
 }
});
test('missing final observation cannot become completion',()=>{
 const h=fixture(),time=h.through(1);h.state.peer.complete=true;h.tick(time);
 assert.equal(JSON.parse(h.file('resize-height-measured-result.json').data).status,'failed');assert.equal(h.file('ack-2'),undefined);
});
test('actual text sampler records ASCII API offsets separately from pixels',()=>{
 const h=fixture(),lines=initialLines();
 h.state.textAPI={isConnected:true,protocolName:'Telnet',visibleLineCount:20,screenLineCapacity:20,topLine:null,scrollTop:0,
  caretLine:18,caretColumn:6,caretPosition:1806,lineText:i=>lines[i],lineStart:i=>i*100,
  visibleRectFromPosition:p=>({x:(p%100)*8,y:Math.floor(p/100)*18,width:8,height:18})};
 h.through(0);const markers=h.messages.find(x=>x.kind==='resize_height_measured_phase').terminal.markers;
 const u=markers.find(x=>x.label==='u');assert.equal(u.column,89);assert.equal(u.line_start_api,1100);assert.equal(u.position,1189);
 assert.deepEqual(JSON.parse(JSON.stringify(u.rect)),{x:712,y:198,width:8,height:18});
});
const results=[];for(const entry of cases){try{entry.action();results.push({case:entry.name,status:'passed'});}
 catch(error){results.push({case:entry.name,status:'failed',error:String(error.message)});}}
const failed=results.filter(v=>v.status==='failed').length;
process.stdout.write(JSON.stringify({schema:'termforge-resize-height-measured-controller-test-v1',node_version:process.version,
 actual_source_sha256:crypto.createHash('sha256').update(original).digest('hex'),case_count:results.length,failed,
 child_processes_spawned:0,sockets_opened:0,target_runs:0,results})+'\n');process.exitCode=failed?1:0;
