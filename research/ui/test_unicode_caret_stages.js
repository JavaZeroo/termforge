// Execute the actual Unicode controller in pure V8: no target, socket or native UI.
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const original=fs.readFileSync(process.argv[2],'utf8');
const specs={
    'dsr-only-q':{widths:[91,91,91,91],labels:['initial','dsr-only','append-mark','next-Q'],resize:[]},
    'cup-position-q':{widths:[91,91,91,91],labels:['initial','explicit-CUP','append-mark','next-Q'],resize:[]},
    'resize-mark-q':{widths:[91,61,61,61],labels:['initial','shrunk-unmodified','append-mark','next-Q'],resize:[1]}
};
const markerLabels=['0123456789','ANCHOR','Z','Q','e','\u0301','\u0302','é','ê','ế'];
const initialLines=['','0123456789','ANCHOR','','','','',' '.repeat(60)+'e\u0301Z'];
function fixture(caseId='resize-mark-q'){
    const spec=specs[caseId],contract={case_id:caseId,labels:spec.labels,windows:spec.widths.map(w=>[w===91?1360:1000,749]),
        grids:spec.widths.map(w=>[w,20]),resize_phases:spec.resize,marker_labels:markerLabels,initial_lines:initialLines,initial_cursor_reply:[8,63]};
    const state={now:0,window:[1360,749],origin:[113,-27],peer:{connected:true,columns:91,rows:20,phase:null,naws_history:[[91,20]]},
        terminal:{connected:true,protocol:'Telnet',capacity:20,line_count:8,lines:initialLines,markers:[]}};
    const files=new Map(),messages=[],resizes=[],root='/owned/unicode-caret';
    const module=name=>({name,path:root+'/app/'+name,base:{add:()=>({readU8:()=>0})}});
    const context={__RUN_DIRECTORY_JSON__:root,uiThread:1,wind:module('WindTerm.exe'),core:module('Qt5Core.dll'),gui:module('Qt5Gui.dll'),widgets:module('Qt5Widgets.dll'),
        user:{getExportByName:()=>1},Process:{getCurrentThreadId:()=>1},Date:{now:()=>state.now},send:value=>messages.push(value),setTimeout:()=>1,
        File:class{constructor(name){this.name=name;this.data='';}write(value){this.data+=value;}close(){files.set(this.name,{at:state.now,data:this.data});}},
        peerState:()=>state.peer,evalText:source=>state.textAPI?vm.runInNewContext(source,{window:{activeText:state.textAPI}},{timeout:100}):JSON.stringify(state.terminal),ready:()=>true,
        resize:(_,w,h)=>{state.window=[w,h];resizes.push({at:state.now,size:[w,h]});},
        rect:()=>[...state.origin,state.origin[0]+state.window[0]-1,state.origin[1]+state.window[1]-1],
        scan:()=>{},expandAll:()=>{},focus:()=>{},key:()=>{},main:1,tree:2};
    context.Interceptor={attach:(_,callbacks)=>{context.dispatch=callbacks.onEnter;}};
    vm.createContext(context);vm.runInContext(original.replace('__UNICODE_CARET_CASE_JSON__',JSON.stringify(contract)),context,{timeout:1000});
    const tick=now=>{state.now=now;vm.runInContext('dispatch()',context,{timeout:100});};
    const file=name=>files.get(root+'/'+name);
    const phase=index=>{
        state.peer.case_id=caseId;state.peer.phase=index;state.peer.phase_label=spec.labels[index];state.peer.frozen_grid=contract.grids[index];
        state.peer.resize_armed=null;state.peer.records??=[];
        state.peer.records[index]={cursor_reply:index===0?[8,63]:[8,61],cursor_reply_private:false,response_hex:index===0?'1b5b383b363352':'1b5b383b363152',frozen_grid:contract.grids[index]};
    };
    tick(0);tick(100);tick(200);tick(1000);tick(1600);assert(file('fixture-ready'));
    const observe=(index,time)=>{
        if(spec.resize.includes(index)){
            assert(file('resize-request-'+index));state.peer.resize_armed=index;tick(time);
            state.peer.columns=spec.widths[index];state.peer.naws_history.push(contract.grids[index]);
            tick(time+100);tick(time+700);assert(file('resize-ready-'+index));
            phase(index);tick(time+800);tick(time+1200);assert(file('ack-'+index));return time+1300;
        }
        phase(index);tick(time);tick(time+400);assert(file('ack-'+index));return time+500;
    };
    const through=index=>{let time=1700;for(let i=0;i<=index;i++)time=observe(i,time);return time;};
    return {state,tick,file,phase,observe,through,resizes,messages,spec,contract};
}
const cases=[];function test(name,action){cases.push({name,action});}
test('three schedules preserve twelve observations with zero or one declared resize',()=>{
    let count=0;
    for(const caseId of Object.keys(specs)){
        const h=fixture(caseId),time=h.through(3);h.state.peer.complete=true;h.tick(time);
        const result=JSON.parse(h.file('unicode-caret-result.json').data);
        assert.equal(result.status,'observed-fixed-unicode-caret-states');assert.equal(result.observations.length,4);
        assert.deepEqual(result.geometry_checks.map(v=>v.phase),[0,1,2,3]);
        assert.deepEqual(h.resizes.map(v=>v.size),caseId==='resize-mark-q'?[[1360,749],[1000,749]]:[[1360,749]]);
        count+=result.observations.length;
    }
    assert.equal(count,12);
});
test('each control and mark observation precedes Q independently',()=>{
    for(const caseId of Object.keys(specs)){
        const h=fixture(caseId);let time=h.through(1);assert.equal(h.file('ack-2'),undefined);
        time=h.observe(2,time);assert.equal(h.file('ack-3'),undefined);h.observe(3,time);assert(h.file('ack-3'));
    }
});
test('known initial body and DSR are gated before further input',()=>{
    for(const changed of ['body','dsr']){
        const h=fixture();h.phase(0);
        if(changed==='body')h.state.terminal.lines=[''];else h.state.peer.records[0].cursor_reply=[8,64];
        h.tick(1700);assert.equal(h.file('ack-0'),undefined);assert.equal(JSON.parse(h.file('unicode-caret-result.json').data).status,'failed');
    }
});
test('later unknown text remains observable without a predicted policy',()=>{
    const h=fixture(),time=h.through(0);h.state.terminal.lines=[''];h.state.terminal.line_count=1;
    h.observe(1,time);const event=h.messages.filter(v=>v.kind==='unicode_caret_phase')[1];
    assert.deepEqual(event.frozen_grid,[61,20]);assert(h.file('ack-1'));
});
test('pre-action and before-Q observations cannot be skipped',()=>{
    for(const caseId of Object.keys(specs))for(const [before,skip] of [[0,2],[1,3]]){
        const h=fixture(caseId),time=h.through(before);h.phase(skip);h.tick(time);h.tick(time+400);
        assert.equal(h.file('ack-'+skip),undefined);assert.equal(JSON.parse(h.file('unicode-caret-result.json').data).status,'failed');
    }
});
test('same-batch NAWS changes restart the entire stability window',()=>{
    const h=fixture(),time=h.through(0);h.state.peer.resize_armed=1;h.tick(time);
    h.state.peer.columns=61;h.state.peer.naws_history.push([61,20]);h.tick(time+100);
    h.state.peer.naws_history.push([60,20],[61,20]);h.tick(time+650);h.tick(time+700);
    assert.equal(h.file('resize-ready-1'),undefined);h.tick(time+1250);assert(h.file('resize-ready-1'));
});
test('wrong resized grid or stationary geometry fails before acknowledgment',()=>{
    const bad=fixture(),time=bad.through(0);bad.state.peer.resize_armed=1;bad.tick(time);bad.state.peer.columns=62;bad.state.peer.naws_history.push([62,20]);
    bad.tick(time+100);bad.tick(time+700);assert.equal(bad.file('resize-ready-1'),undefined);
    assert.equal(JSON.parse(bad.file('unicode-caret-result.json').data).status,'failed');
    const h=fixture('dsr-only-q'),next=h.through(0);h.phase(1);h.state.window[0]=1000;h.tick(next);
    assert.equal(h.file('ack-1'),undefined);assert.equal(JSON.parse(h.file('unicode-caret-result.json').data).status,'failed');
});
test('completion never replaces the final Q observation',()=>{
    const h=fixture('cup-position-q'),time=h.through(2);h.state.peer.complete=true;h.tick(time);
    assert.equal(JSON.parse(h.file('unicode-caret-result.json').data).status,'failed');assert.equal(h.file('ack-3'),undefined);
});
test('actual nested sampler queries both byte and text starts/ends and original caretPosition',()=>{
    const h=fixture('dsr-only-q');
    h.state.textAPI={isConnected:true,protocolName:'Telnet',visibleLineCount:8,screenLineCapacity:20,topLine:null,scrollTop:0,
        caretLine:7,caretColumn:0,caretPosition:764,lineText:i=>initialLines[i],lineStart:i=>i*100,
        visibleRectFromPosition:p=>({x:p,y:126,width:p===762?16:8,height:18})};
    h.through(0);const event=h.messages.filter(v=>v.kind==='unicode_caret_phase')[0];
    const z=event.terminal.markers.find(v=>v.label==='Z'),queries=Object.fromEntries(z.queries.map(q=>[q.basis,q]));
    assert.equal(z.line_start_api,700);assert.equal(queries['utf16-start'].position,762);assert.equal(queries['utf8-start'].position,763);
    assert.equal(queries['scalar-start'].position,762);assert.equal(queries['utf16-end'].position,763);assert.equal(queries['utf8-end'].position,764);
    assert.equal(queries['utf16-start'].rect.width,16);assert.equal(event.terminal.caret_query.position,764);assert.equal(event.terminal.caret_query.rect.x,764);
});
const results=[];for(const entry of cases){try{entry.action();results.push({case:entry.name,status:'passed'});}
    catch(error){results.push({case:entry.name,status:'failed',error:String(error.message)});}}
const failed=results.filter(v=>v.status==='failed').length;
process.stdout.write(JSON.stringify({schema:'termforge-unicode-caret-controller-test-v1',node_version:process.version,
    actual_source_sha256:crypto.createHash('sha256').update(original).digest('hex'),case_count:results.length,failed,
    child_processes_spawned:0,sockets_opened:0,target_runs:0,results})+'\n');process.exitCode=failed?1:0;
