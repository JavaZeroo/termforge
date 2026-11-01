// Pure V8 execution of the actual probe controller; no native/target/socket API.
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const source=fs.readFileSync(process.argv[2],'utf8'),cases=[];
function fixture(){
    const state={now:0,window:[1360,749],origin:[113,-27],peer:{connected:true,columns:91,rows:20,phase:null,naws_history:[[91,20]]},
        terminal:{connected:true,protocol:'Telnet',capacity:20,line_count:2,lines:['0123456789','ANCHOR'],markers:[]}};
    const files=new Map(),messages=[],resizes=[],root='/owned/resize-state';
    const module=name=>({name,path:root+'/app/'+name,base:{add:()=>({readU8:()=>0})}});
    const context={__RUN_DIRECTORY_JSON__:root,uiThread:1,
        wind:module('WindTerm.exe'),core:module('Qt5Core.dll'),gui:module('Qt5Gui.dll'),widgets:module('Qt5Widgets.dll'),
        user:{getExportByName:()=>1},Process:{getCurrentThreadId:()=>1},Date:{now:()=>state.now},
        send:value=>messages.push(value),setTimeout:()=>1,
        File:class{constructor(name){this.name=name;this.data='';}write(value){this.data+=value;}close(){files.set(this.name,{at:state.now,data:this.data});}},
        peerState:()=>state.peer,evalText:()=>JSON.stringify(state.terminal),ready:()=>true,
        resize:(_,w,h)=>{state.window=[w,h];resizes.push({at:state.now,size:[w,h]});},
        // The protected helper reads QRect's x1/y1/x2/y2, not x/y/width/height.
        rect:()=>[...state.origin,state.origin[0]+state.window[0]-1,state.origin[1]+state.window[1]-1],
        scan:()=>{},expandAll:()=>{},focus:()=>{},key:()=>{},main:1,tree:2};
    context.Interceptor={attach:(_,callbacks)=>{context.dispatch=callbacks.onEnter;}};
    vm.createContext(context);vm.runInContext(source,context,{timeout:1000});
    const tick=now=>{state.now=now;vm.runInContext('dispatch()',context,{timeout:100});};
    const file=name=>files.get(root+'/'+name);
    const phase=(index,grid)=>{
        state.peer.phase=index;state.peer.phase_label=['initial-body','after-width-shrink','after-width-restore'][index];
        state.peer.frozen_grid=grid;state.peer.resize_armed=null;
        state.peer.records??=[];state.peer.records[index]={cursor_reply:[11,2],cursor_reply_private:false,
            response_hex:'1b5b31313b3202',frozen_grid:grid};
    };
    tick(0);tick(100);tick(200);
    const initial=()=>{
        tick(1000);tick(1600);assert.deepEqual(JSON.parse(file('fixture-ready').data),{grid:[91,20],event_count:1});
        state.peer.frozen_grids=[[91,20]];phase(0,[91,20]);tick(1700);tick(2100);
        assert(file('ack-0'));assert(file('resize-request-1'));
    };
    return {state,tick,file,phase,initial,resizes,messages};
}
function test(name,action){cases.push({name,action});}
test('public shrink waits for peer arm and continuous 600ms NAWS',()=>{
    const h=fixture();h.initial();h.tick(2200);assert.equal(h.resizes.length,1);
    h.state.peer.resize_armed=1;h.tick(2300);assert.deepEqual(h.resizes[1].size,[1000,749]);
    h.state.peer.columns=66;h.state.peer.naws_history.push([66,20]);h.tick(2400);h.tick(2999);
    assert.equal(h.file('resize-ready-1'),undefined);h.tick(3000);
    assert.deepEqual(JSON.parse(h.file('resize-ready-1').data),{phase:1,grid:[66,20],event_count:2});
    h.tick(3100);assert.equal(h.resizes.length,2,'one request must call public resize only once');
});
test('same-batch transient resize restarts stable interval',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);
    h.state.peer.columns=66;h.state.peer.naws_history.push([66,20]);h.tick(2400);
    h.state.peer.naws_history.push([65,20],[66,20]);h.tick(2950);h.tick(3000);
    assert.equal(h.file('resize-ready-1'),undefined);h.tick(3549);assert.equal(h.file('resize-ready-1'),undefined);
    h.tick(3550);assert.equal(h.file('resize-ready-1').at,3550);
});
test('capacity mismatch cancels old resize stability',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);
    h.state.peer.columns=66;h.state.peer.naws_history.push([66,20]);h.tick(2400);
    h.state.terminal.capacity=21;h.tick(2950);h.state.terminal.capacity=20;h.tick(3000);
    h.tick(3599);assert.equal(h.file('resize-ready-1'),undefined);h.tick(3600);assert(h.file('resize-ready-1'));
});
test('row-count change fails instead of silently accepting another scenario',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);
    h.state.peer.columns=66;h.state.peer.rows=21;h.state.terminal.capacity=21;
    h.state.peer.naws_history.push([66,21]);h.tick(2400);h.tick(3000);
    const result=JSON.parse(h.file('resize-state-result.json').data);
    assert.equal(result.status,'failed');assert.match(result.error,/row count/);
    assert.equal(h.file('resize-ready-1'),undefined);
});
test('three states preserve post-resize cursor observations before any input',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);
    h.state.peer.columns=66;h.state.peer.naws_history.push([66,20]);h.tick(2400);h.tick(3000);
    h.state.peer.frozen_grids.push([66,20]);h.phase(1,[66,20]);h.tick(3100);h.tick(3500);
    assert(h.file('ack-1'));assert(h.file('resize-request-2'));assert.equal(h.resizes.length,2);
    h.state.peer.resize_armed=2;h.tick(3600);assert.deepEqual(h.resizes[2].size,[1360,749]);
    h.state.peer.columns=91;h.state.peer.naws_history.push([91,20]);h.tick(3700);h.tick(4300);
    assert(h.file('resize-ready-2'));h.state.peer.frozen_grids.push([91,20]);h.phase(2,[91,20]);
    h.tick(4400);h.tick(4800);assert(h.file('ack-2'));h.state.peer.complete=true;h.tick(4900);
    const result=JSON.parse(h.file('resize-state-result.json').data);
    assert.equal(result.status,'observed-three-resize-states');assert.equal(result.observations.length,3);
    assert.deepEqual(result.observations.map(v=>v.cursor_reply),[[11,2],[11,2],[11,2]]);
    assert.deepEqual(h.resizes.map(v=>v.size),[[1360,749],[1000,749],[1360,749]]);
});
test('initial transient NAWS does not inherit an old stability window',()=>{
    const h=fixture();h.tick(1000);h.state.peer.naws_history.push([92,20],[91,20]);h.tick(1599);
    h.tick(1600);assert.equal(h.file('fixture-ready'),undefined);h.tick(2199);assert(h.file('fixture-ready'));
});
test('zero-origin QRect still uses inclusive right and bottom',()=>{
    const h=fixture();h.state.origin=[0,0];h.initial();
    assert(h.file('fixture-ready'));
    assert.deepEqual(h.messages.find(v=>v.kind==='resize_state_phase').window_rect,[0,0,1359,748]);
    assert.deepEqual(Array.from(h.messages.find(v=>v.kind==='resize_state_phase').window_size),[1360,749]);
});
test('one-pixel initial width error fails and retains measured geometry',()=>{
    const h=fixture();h.state.window[0]++;h.tick(1000);h.tick(1600);
    const result=JSON.parse(h.file('resize-state-result.json').data);
    assert.equal(result.status,'failed');assert.match(result.error,/initial public window dimensions differ/);
    assert.equal(h.file('fixture-ready'),undefined);
    assert.deepEqual(result.geometry_checks,[{phase:0,window_rect:[113,-27,1473,721],window_size:[1361,749],expected_size:[1360,749]}]);
});
test('one-pixel shrink height error cannot advance the phase',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);
    h.state.window[1]--;h.state.peer.columns=66;h.state.peer.naws_history.push([66,20]);h.tick(2400);h.tick(3000);
    const result=JSON.parse(h.file('resize-state-result.json').data);
    assert.equal(result.status,'failed');assert.match(result.error,/public window dimensions differ/);
    assert.equal(h.file('resize-ready-1'),undefined);
    assert.deepEqual(result.geometry_checks[1],{phase:1,window_rect:[113,-27,1112,720],window_size:[1000,748],expected_size:[1000,749]});
});
const results=[];
for(const entry of cases){try{entry.action();results.push({case:entry.name,status:'passed'});}
    catch(error){results.push({case:entry.name,status:'failed',error:String(error.message)});}}
const failed=results.filter(v=>v.status==='failed').length;
process.stdout.write(JSON.stringify({schema:'termforge-resize-state-controller-test-v1',node_version:process.version,
    actual_source_sha256:crypto.createHash('sha256').update(source).digest('hex'),case_count:results.length,failed,
    child_processes_spawned:0,sockets_opened:0,target_runs:0,results})+'\n');
process.exitCode=failed?1:0;
