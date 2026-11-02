// Pure V8 execution of the new controller; no Qt, sockets or target process.
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const original=fs.readFileSync(process.argv[2],'utf8'),source=original.replace('__RESIZE_ADDRESS_CASE_JSON__','"next-char"');
function fixture(){
    const state={now:0,window:[1360,749],origin:[113,-27],peer:{connected:true,columns:91,rows:20,phase:null,naws_history:[[91,20]]},
        terminal:{connected:true,protocol:'Telnet',capacity:20,line_count:2,lines:['0123456789','ANCHOR'],markers:[]}};
    const files=new Map(),messages=[],resizes=[],root='/owned/resize-address';
    const module=name=>({name,path:root+'/app/'+name,base:{add:()=>({readU8:()=>0})}});
    const context={__RUN_DIRECTORY_JSON__:root,uiThread:1,
        wind:module('WindTerm.exe'),core:module('Qt5Core.dll'),gui:module('Qt5Gui.dll'),widgets:module('Qt5Widgets.dll'),
        user:{getExportByName:()=>1},Process:{getCurrentThreadId:()=>1},Date:{now:()=>state.now},send:value=>messages.push(value),setTimeout:()=>1,
        File:class{constructor(name){this.name=name;this.data='';}write(value){this.data+=value;}close(){files.set(this.name,{at:state.now,data:this.data});}},
        peerState:()=>state.peer,evalText:()=>JSON.stringify(state.terminal),ready:()=>true,
        resize:(_,w,h)=>{state.window=[w,h];resizes.push({at:state.now,size:[w,h]});},
        rect:()=>[...state.origin,state.origin[0]+state.window[0]-1,state.origin[1]+state.window[1]-1],
        scan:()=>{},expandAll:()=>{},focus:()=>{},key:()=>{},main:1,tree:2};
    context.Interceptor={attach:(_,callbacks)=>{context.dispatch=callbacks.onEnter;}};
    vm.createContext(context);vm.runInContext(source,context,{timeout:1000});
    const tick=now=>{state.now=now;vm.runInContext('dispatch()',context,{timeout:100});};
    const file=name=>files.get(root+'/'+name);
    const phase=(index,grid)=>{
        state.peer.case_id='next-char';state.peer.phase=index;
        state.peer.phase_label=['initial-body','after-shrink-before-action','after-next-char','after-restore-after-action'][index];
        state.peer.frozen_grid=grid;state.peer.resize_armed=null;state.peer.records??=[];
        state.peer.records[index]={cursor_reply:[11,2],cursor_reply_private:false,response_hex:'1b5b31313b3252',frozen_grid:grid};
    };
    tick(0);tick(100);tick(200);
    const initial=()=>{
        tick(1000);tick(1600);assert(file('fixture-ready'));state.peer.frozen_grids=[[91,20]];
        phase(0,[91,20]);tick(1700);tick(2100);assert(file('ack-0'));assert(file('resize-request-1'));
    };
    const shrink=()=>{
        initial();state.peer.resize_armed=1;tick(2300);state.peer.columns=61;state.peer.naws_history.push([61,20]);
        tick(2400);tick(3000);assert(file('resize-ready-1'));
        state.peer.frozen_grids.push([61,20]);phase(1,[61,20]);tick(3100);tick(3500);assert(file('ack-1'));
    };
    return {state,tick,file,phase,initial,shrink,resizes,messages};
}
const cases=[];function test(name,action){cases.push({name,action});}
test('shrink waits for armed boundary and full 600ms NAWS stability',()=>{
    const h=fixture();h.initial();h.tick(2200);assert.equal(h.resizes.length,1);
    h.state.peer.resize_armed=1;h.tick(2300);h.state.peer.columns=61;h.state.peer.naws_history.push([61,20]);
    h.tick(2400);h.tick(2999);assert.equal(h.file('resize-ready-1'),undefined);h.tick(3000);
    assert.deepEqual(JSON.parse(h.file('resize-ready-1').data),{phase:1,grid:[61,20],event_count:2});
});
test('pre-action baseline is observed and acknowledged without a restore request',()=>{
    const h=fixture();h.shrink();assert.equal(h.file('resize-request-2'),undefined);assert.equal(h.file('resize-request-3'),undefined);
    assert.equal(h.messages.filter(v=>v.kind==='resize_address_phase').length,2);
    h.tick(3600);assert.equal(h.resizes.length,2);
});
test('action remains same geometry and only then allows restore plus fourth observation',()=>{
    const h=fixture();h.shrink();h.state.peer.frozen_grids.push([61,20]);h.phase(2,[61,20]);h.tick(3600);h.tick(4000);
    assert(h.file('ack-2'));assert(h.file('resize-request-3'));assert.equal(h.resizes.length,2);
    h.state.peer.resize_armed=3;h.tick(4100);h.state.peer.columns=91;h.state.peer.naws_history.push([91,20]);
    h.tick(4200);h.tick(4800);assert(h.file('resize-ready-3'));h.state.peer.frozen_grids.push([91,20]);h.phase(3,[91,20]);
    h.tick(4900);h.tick(5300);h.state.peer.complete=true;h.tick(5400);
    const result=JSON.parse(h.file('resize-address-result.json').data);
    assert.equal(result.status,'observed-four-resize-address-states');assert.equal(result.observations.length,4);
    assert.deepEqual(result.geometry_checks.map(v=>v.phase),[0,1,2,3]);
    assert.deepEqual(h.resizes.map(v=>v.size),[[1360,749],[1000,749],[1360,749]]);
});
test('wrong shrunk grid is rejected without substituting another width',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);h.state.peer.columns=62;h.state.peer.naws_history.push([62,20]);
    h.tick(2400);h.tick(3000);assert.equal(h.file('resize-ready-1'),undefined);
    assert.match(JSON.parse(h.file('resize-address-result.json').data).error,/fixed resized grid/);
});
test('same-batch changing NAWS restarts the stability interval',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);h.state.peer.columns=61;h.state.peer.naws_history.push([61,20]);
    h.tick(2400);h.state.peer.naws_history.push([60,20],[61,20]);h.tick(2950);h.tick(3000);
    assert.equal(h.file('resize-ready-1'),undefined);h.tick(3550);assert(h.file('resize-ready-1'));
});
test('unexpected case and action geometry cannot acknowledge a phase',()=>{
    for(const invalid of ['case','geometry']){
        const h=fixture();h.shrink();h.phase(2,[61,20]);
        if(invalid==='case')h.state.peer.case_id='line-feed';else h.state.window[1]--;
        h.tick(3600);assert.equal(h.file('ack-2'),undefined);
        assert.equal(JSON.parse(h.file('resize-address-result.json').data).status,'failed');
    }
});
const results=[];for(const entry of cases){try{entry.action();results.push({case:entry.name,status:'passed'});}
    catch(error){results.push({case:entry.name,status:'failed',error:String(error.message)});}}
const failed=results.filter(v=>v.status==='failed').length;
process.stdout.write(JSON.stringify({schema:'termforge-resize-address-controller-test-v1',node_version:process.version,
    actual_source_sha256:crypto.createHash('sha256').update(original).digest('hex'),case_count:results.length,failed,
    child_processes_spawned:0,sockets_opened:0,target_runs:0,results})+'\n');process.exitCode=failed?1:0;
