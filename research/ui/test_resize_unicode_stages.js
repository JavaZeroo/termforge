// Execute the actual Unicode controller in pure V8: no target, socket or native UI.
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const original=fs.readFileSync(process.argv[2],'utf8');
const specs={
    'wide-boundary':{widths:[91,61,61,91],labels:['wide-initial','wide-shrunk-unmodified','wide-next-Q','wide-restored-after-Q'],resize:[1,3]},
    'combining-boundary':{widths:[91,61,61,91],labels:['combining-initial','combining-shrunk-unmodified','combining-next-circumflex','combining-restored-after-mark'],resize:[1,3]}
};
const markerLabels=['0123456789','ANCHOR','界','Z','Q','e','\u0301','\u0302','é','ê','ế'];
function fixture(caseId='wide-boundary'){
    const spec=specs[caseId],contract={case_id:caseId,labels:spec.labels,windows:spec.widths.map(w=>[w===91?1360:1000,749]),
        grids:spec.widths.map(w=>[w,20]),resize_phases:spec.resize,marker_labels:markerLabels};
    const state={now:0,window:[1360,749],origin:[113,-27],peer:{connected:true,columns:91,rows:20,phase:null,naws_history:[[91,20]]},
        terminal:{connected:true,protocol:'Telnet',capacity:20,line_count:3,lines:['0123456789','ANCHOR',' '.repeat(90)+'W'],markers:[]}};
    const files=new Map(),messages=[],resizes=[],root='/owned/resize-unicode';
    const module=name=>({name,path:root+'/app/'+name,base:{add:()=>({readU8:()=>0})}});
    const context={__RUN_DIRECTORY_JSON__:root,uiThread:1,wind:module('WindTerm.exe'),core:module('Qt5Core.dll'),gui:module('Qt5Gui.dll'),widgets:module('Qt5Widgets.dll'),
        user:{getExportByName:()=>1},Process:{getCurrentThreadId:()=>1},Date:{now:()=>state.now},send:value=>messages.push(value),setTimeout:()=>1,
        File:class{constructor(name){this.name=name;this.data='';}write(value){this.data+=value;}close(){files.set(this.name,{at:state.now,data:this.data});}},
        peerState:()=>state.peer,evalText:source=>state.textAPI?vm.runInNewContext(source,{window:{activeText:state.textAPI}},{timeout:100}):JSON.stringify(state.terminal),ready:()=>true,
        resize:(_,w,h)=>{state.window=[w,h];resizes.push({at:state.now,size:[w,h]});},
        rect:()=>[...state.origin,state.origin[0]+state.window[0]-1,state.origin[1]+state.window[1]-1],
        scan:()=>{},expandAll:()=>{},focus:()=>{},key:()=>{},main:1,tree:2};
    context.Interceptor={attach:(_,callbacks)=>{context.dispatch=callbacks.onEnter;}};
    vm.createContext(context);vm.runInContext(original.replace('__RESIZE_UNICODE_CASE_JSON__',JSON.stringify(contract)),context,{timeout:1000});
    const tick=now=>{state.now=now;vm.runInContext('dispatch()',context,{timeout:100});};
    const file=name=>files.get(root+'/'+name);
    const phase=index=>{
        state.peer.case_id=caseId;state.peer.phase=index;state.peer.phase_label=spec.labels[index];state.peer.frozen_grid=contract.grids[index];
        state.peer.resize_armed=null;state.peer.records??=[];
        state.peer.records[index]={cursor_reply:[8,91],cursor_reply_private:false,response_hex:'1b5b383b393152',frozen_grid:contract.grids[index]};
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
test('both immutable schedules preserve eight observations and exactly two post-initial resizes',()=>{
    let count=0;
    for(const caseId of Object.keys(specs)){
        const h=fixture(caseId),time=h.through(3);h.state.peer.complete=true;h.tick(time);
        const result=JSON.parse(h.file('resize-unicode-result.json').data);
        assert.equal(result.status,'observed-fixed-resize-unicode-states');assert.equal(result.observations.length,4);
        assert.deepEqual(result.geometry_checks.map(v=>v.phase),[0,1,2,3]);
        assert.deepEqual(result.observations.map(v=>v.frozen_grid[0]),h.spec.widths);
        assert.deepEqual(h.resizes.map(v=>v.size),[[1360,749],[1000,749],[1360,749]]);
        count+=result.observations.length;
    }
    assert.equal(count,8);
});
test('shrink-only observation precedes Q or combining action and its observation precedes restore',()=>{
    for(const caseId of Object.keys(specs)){
        const h=fixture(caseId);let time=h.through(1);assert.equal(h.resizes.length,2);
        assert.equal(h.file('ack-2'),undefined);assert.equal(h.file('resize-request-3'),undefined);
        time=h.observe(2,time);assert(h.file('resize-request-3'));assert.equal(h.resizes.length,2);
        h.observe(3,time);assert.equal(h.resizes.length,3);
    }
});
test('DSR 91 in a 61-column grid and missing text remain observable',()=>{
    const h=fixture();h.state.terminal.lines=[''];h.state.terminal.line_count=1;h.through(1);
    const event=h.messages.filter(v=>v.kind==='resize_unicode_phase')[1];
    assert.deepEqual(event.cursor_reply,[8,91]);assert.deepEqual(event.frozen_grid,[61,20]);assert(h.file('ack-1'));
});
test('shrink-only or action observations cannot be skipped',()=>{
    for(const caseId of Object.keys(specs))for(const [before,skip] of [[0,2],[1,3]]){
        const h=fixture(caseId),time=h.through(before);h.phase(skip);h.tick(time);h.tick(time+400);
        assert.equal(h.file('ack-'+skip),undefined);assert.equal(JSON.parse(h.file('resize-unicode-result.json').data).status,'failed');
    }
});
test('same-batch changing NAWS restarts all 600ms of stability',()=>{
    const h=fixture(),time=h.through(0);h.state.peer.resize_armed=1;h.tick(time);
    h.state.peer.columns=61;h.state.peer.naws_history.push([61,20]);h.tick(time+100);
    h.state.peer.naws_history.push([60,20],[61,20]);h.tick(time+650);h.tick(time+700);
    assert.equal(h.file('resize-ready-1'),undefined);h.tick(time+1250);assert(h.file('resize-ready-1'));
});
test('wrong grid case label or stationary-stage geometry fails before acknowledgment',()=>{
    const bad=fixture(),time=bad.through(0);bad.state.peer.resize_armed=1;bad.tick(time);bad.state.peer.columns=62;bad.state.peer.naws_history.push([62,20]);
    bad.tick(time+100);bad.tick(time+700);assert.equal(bad.file('resize-ready-1'),undefined);
    assert.equal(JSON.parse(bad.file('resize-unicode-result.json').data).status,'failed');
    for(const invalid of ['case','label','geometry']){
        const h=fixture(),next=h.through(1);h.phase(2);
        if(invalid==='case')h.state.peer.case_id='combining-boundary';else if(invalid==='label')h.state.peer.phase_label='wrong';else h.state.window[1]--;
        h.tick(next);assert.equal(h.file('ack-2'),undefined);assert.equal(JSON.parse(h.file('resize-unicode-result.json').data).status,'failed');
    }
});
test('completion never substitutes for missing final observations',()=>{
    const h=fixture('combining-boundary'),time=h.through(2);h.state.peer.complete=true;h.tick(time);
    assert.equal(JSON.parse(h.file('resize-unicode-result.json').data).status,'failed');assert.equal(h.file('ack-3'),undefined);
});
test('actual nested text sampler preserves UTF16 offsets original API starts and shared rectangles',()=>{
    for(const [caseId,word] of [['wide-boundary','界Z'],['combining-boundary','e\u0301Z']]){
        const h=fixture(caseId),lines=Array(8).fill('');lines[1]='0123456789';lines[2]='ANCHOR';lines[7]=' '.repeat(60)+word;
        h.state.textAPI={isConnected:true,protocolName:'Telnet',visibleLineCount:8,screenLineCapacity:20,topLine:null,scrollTop:0,
            caretLine:7,caretColumn:60,caretPosition:760,lineText:i=>lines[i],lineStart:i=>i*100,
            visibleRectFromPosition:p=>({x:p>=760?480:0,y:126,width:caseId==='wide-boundary'&&p===760?16:8,height:18})};
        h.through(0);const event=h.messages.filter(v=>v.kind==='resize_unicode_phase')[0],markers=event.terminal.markers;
        const z=markers.find(v=>v.label==='Z');assert.equal(z.column,caseId==='wide-boundary'?61:62);
        assert.equal(z.line_start_api,700);assert.equal(z.position,700+z.column);
        if(caseId==='wide-boundary')assert.equal(markers.find(v=>v.label==='界').rect.width,16);
        else assert.deepEqual(markers.find(v=>v.label==='e').rect,markers.find(v=>v.label==='\u0301').rect);
    }
});
const results=[];for(const entry of cases){try{entry.action();results.push({case:entry.name,status:'passed'});}
    catch(error){results.push({case:entry.name,status:'failed',error:String(error.message)});}}
const failed=results.filter(v=>v.status==='failed').length;
process.stdout.write(JSON.stringify({schema:'termforge-resize-unicode-controller-test-v1',node_version:process.version,
    actual_source_sha256:crypto.createHash('sha256').update(original).digest('hex'),case_count:results.length,failed,
    child_processes_spawned:0,sockets_opened:0,target_runs:0,results})+'\n');process.exitCode=failed?1:0;
