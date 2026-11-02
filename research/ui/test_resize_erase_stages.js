// Pure V8 execution of the new controller; no Qt, sockets or target process.
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const original=fs.readFileSync(process.argv[2],'utf8'),source=original;
function fixture(caseId='erase-right'){
    const count=5,last=4;
    const state={now:0,window:[1360,749],origin:[113,-27],peer:{connected:true,columns:91,rows:20,phase:null,naws_history:[[91,20]]},
        terminal:{connected:true,protocol:'Telnet',capacity:20,line_count:3,lines:['0123456789','ANCHOR','L'+' '.repeat(89)+'v'],markers:[]}};
    const files=new Map(),messages=[],resizes=[],root='/owned/resize-erase';
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
    vm.createContext(context);vm.runInContext(source.replace('__RESIZE_ERASE_CASE_JSON__',JSON.stringify(caseId)),context,{timeout:1000});
    const tick=now=>{state.now=now;vm.runInContext('dispatch()',context,{timeout:100});};
    const file=name=>files.get(root+'/'+name);
    const phase=(index,grid)=>{
        state.peer.case_id=caseId;state.peer.phase=index;
        state.peer.phase_label='synthetic-stage-'+index;
        state.peer.frozen_grid=grid;state.peer.resize_armed=null;state.peer.records??=[];
        state.peer.records[index]={cursor_reply:index===2?[8,61]:[11,2],cursor_reply_private:false,response_hex:'1b5b31313b3252',frozen_grid:grid};
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
    const prepared=()=>{
        shrink();
        state.peer.frozen_grids.push([61,20]);phase(2,[61,20]);tick(3600);tick(4000);assert(file('ack-2'));
    };
    return {state,tick,file,phase,initial,shrink,prepared,resizes,messages,count,last};
}
const cases=[];function test(name,action){cases.push({name,action});}

test('shrink waits for armed boundary and continuous 600ms NAWS stability',()=>{
    const h=fixture();h.initial();h.tick(2200);assert.equal(h.resizes.length,1);
    h.state.peer.resize_armed=1;h.tick(2300);h.state.peer.columns=61;h.state.peer.naws_history.push([61,20]);
    h.tick(2400);h.tick(2999);assert.equal(h.file('resize-ready-1'),undefined);h.tick(3000);
    assert.deepEqual(JSON.parse(h.file('resize-ready-1').data),{phase:1,grid:[61,20],event_count:2});
});
test('shrunk baseline and preparation are distinct observations before any edit',()=>{
    const h=fixture();h.prepared();
    assert.equal(h.messages.filter(v=>v.kind==='resize_erase_phase').length,3);
    for(const n of [2,3,4,5])assert.equal(h.file('resize-request-'+n),undefined);
    assert.equal(h.resizes.length,2);
});
test('both cases preserve 10 observations and exactly two resize boundaries',()=>{
    let observations=0;
    for(const name of ['erase-right','erase-whole-line']){
        const h=fixture(name);h.prepared();let time=4100;
        for(let phase=3;phase<h.last;phase++){
            h.state.peer.frozen_grids.push([61,20]);h.phase(phase,[61,20]);h.tick(time);h.tick(time+400);
            assert(h.file('ack-'+phase));time+=500;
        }
        assert(h.file('resize-request-'+h.last));assert.equal(h.resizes.length,2);
        h.state.peer.resize_armed=h.last;h.tick(time);h.state.peer.columns=91;h.state.peer.naws_history.push([91,20]);
        h.tick(time+100);h.tick(time+700);assert(h.file('resize-ready-'+h.last));
        h.state.peer.frozen_grids.push([91,20]);h.phase(h.last,[91,20]);h.tick(time+800);h.tick(time+1200);
        h.state.peer.complete=true;h.tick(time+1300);
        const result=JSON.parse(h.file('resize-erase-result.json').data);
        assert.equal(result.status,'observed-fixed-resize-erase-states');assert.equal(result.observations.length,h.count);
        assert.deepEqual(result.geometry_checks.map(v=>v.phase),Array.from({length:h.count},(_,i)=>i));
        assert.deepEqual(h.resizes.map(v=>v.size),[[1360,749],[1000,749],[1360,749]]);
        observations+=result.observations.length;
    }
    assert.equal(observations,10);
});
test('failed preparation is retained as the third observation but never acknowledged',()=>{
    for(const invalid of ['tail','dsr','prefix']){
        const h=fixture('erase-right');h.shrink();h.phase(2,[61,20]);
        if(invalid==='tail')h.state.terminal.lines[2]='L';
        else if(invalid==='prefix')h.state.terminal.lines[2]=' '.repeat(90)+'v';
        else{h.state.terminal.lines[2]='L'+' '.repeat(89)+'v';h.state.peer.records[2].cursor_reply=[8,60];}
        h.tick(3600);h.tick(4000);assert.equal(h.file('ack-2'),undefined);
        const result=JSON.parse(h.file('resize-erase-result.json').data);
        assert.equal(result.status,'failed');assert.equal(result.observations.length,3);assert.match(result.error,/preparation/);
    }
});
test('wrong resized grid fails without substituting another width',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);h.state.peer.columns=62;h.state.peer.naws_history.push([62,20]);
    h.tick(2400);h.tick(3000);assert.equal(h.file('resize-ready-1'),undefined);
    assert.match(JSON.parse(h.file('resize-erase-result.json').data).error,/fixed resized grid/);
});
test('same-batch changing NAWS restarts the stability interval',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);h.state.peer.columns=61;h.state.peer.naws_history.push([61,20]);
    h.tick(2400);h.state.peer.naws_history.push([60,20],[61,20]);h.tick(2950);h.tick(3000);
    assert.equal(h.file('resize-ready-1'),undefined);h.tick(3550);assert(h.file('resize-ready-1'));
});
test('wrong case or intermediate edit window cannot acknowledge the action',()=>{
    for(const invalid of ['case','geometry']){
        const h=fixture();h.prepared();h.phase(3,[61,20]);
        if(invalid==='case')h.state.peer.case_id='erase-whole-line';else h.state.window[1]--;
        h.tick(4100);assert.equal(h.file('ack-3'),undefined);
        assert.equal(JSON.parse(h.file('resize-erase-result.json').data).status,'failed');
    }
});
test('EL cannot skip the CUP-only observation and completion cannot omit final stages',()=>{
    const skipped=fixture();skipped.shrink();skipped.phase(3,[61,20]);skipped.tick(3600);skipped.tick(4000);
    assert.equal(skipped.file('ack-3'),undefined);assert.equal(JSON.parse(skipped.file('resize-erase-result.json').data).status,'failed');
    const early=fixture();early.prepared();early.state.peer.complete=true;early.tick(4100);
    assert.equal(JSON.parse(early.file('resize-erase-result.json').data).status,'failed');
});
test('CUP-only preparation accepts an already clipped shrunk baseline without repairing L or v',()=>{
    const h=fixture();h.initial();h.state.peer.resize_armed=1;h.tick(2300);h.state.peer.columns=61;h.state.peer.naws_history.push([61,20]);
    h.tick(2400);h.tick(3000);assert(h.file('resize-ready-1'));
    h.state.terminal.lines[2]='L'+' '.repeat(60);h.phase(1,[61,20]);h.tick(3100);h.tick(3500);assert(h.file('ack-1'));
    h.phase(2,[61,20]);h.tick(3600);h.tick(4000);assert(h.file('ack-2'));
    const phases=h.messages.filter(v=>v.kind==='resize_erase_phase');assert.equal(phases.length,3);
    assert.deepEqual(phases[1].terminal.lines,phases[2].terminal.lines);assert.equal(h.resizes.length,2);
});
const results=[];for(const entry of cases){try{entry.action();results.push({case:entry.name,status:'passed'});}
    catch(error){results.push({case:entry.name,status:'failed',error:String(error.message)});}}
const failed=results.filter(v=>v.status==='failed').length;
process.stdout.write(JSON.stringify({schema:'termforge-resize-erase-controller-test-v1',node_version:process.version,
    actual_source_sha256:crypto.createHash('sha256').update(original).digest('hex'),case_count:results.length,failed,
    child_processes_spawned:0,sockets_opened:0,target_runs:0,results})+'\n');process.exitCode=failed?1:0;
