// Execute the derived UI state machine with inert objects; no native/runtime target.
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const source=fs.readFileSync(process.argv[2],'utf8'),results=[];
function fixture(){
    const root='/owned/reentry',files=new Map(),messages=[];
    const state={now:0,peer:{case:'alternate-origin-reentry',connected:true,columns:91,rows:25,phase:null,
                           frozen_grid:[91,25],naws_history:[[91,25]],records:[]},
                 terminal:{connected:true,protocol:'Telnet',capacity:25,line_count:1,lines:['synthetic']}};
    const module=name=>({name,path:root+'/app/'+name,base:{add:()=>({readU8:()=>0})}});
    const context={__RUN_DIRECTORY_JSON__:root,uiThread:1,
        wind:module('WindTerm.exe'),core:module('Qt5Core.dll'),gui:module('Qt5Gui.dll'),widgets:module('Qt5Widgets.dll'),
        user:{getExportByName:()=>1},Process:{getCurrentThreadId:()=>1},Date:{now:()=>state.now},
        send:value=>messages.push(value),setTimeout:()=>1,peerState:()=>state.peer,evalText:()=>JSON.stringify(state.terminal),
        File:class{constructor(name){this.name=name;this.data='';}write(data){this.data+=data;}close(){files.set(this.name,{at:state.now,data:this.data});}},
        ready:()=>true,resize:()=>{},scan:()=>{},expandAll:()=>{},focus:()=>{},key:()=>{},main:1,tree:2};
    context.Interceptor={attach:(_,callbacks)=>{context.dispatch=callbacks.onEnter;}};
    vm.createContext(context);vm.runInContext(source,context,{timeout:1000});
    function tick(now){state.now=now;vm.runInContext('dispatch()',context,{timeout:100});}
    tick(0);tick(100);tick(200);
    return {state,tick,messages,files,ready:()=>files.get(root+'/fixture-ready'),
            result:()=>JSON.parse(files.get(root+'/mode-reentry-result.json').data)};
}
function test(name,run){try{run();results.push({case:name,status:'passed'});}catch(error){results.push({case:name,status:'failed',error:String(error.message)});}}
test('stable grid waits 600ms',()=>{
    const h=fixture();h.tick(1000);h.tick(1599);assert.equal(h.ready(),undefined);h.tick(1600);assert.equal(h.ready().at,1600);
});
test('transient NAWS change restarts readiness',()=>{
    const h=fixture();h.tick(1000);h.state.peer.naws_history.push([92,25],[91,25]);h.tick(1550);
    h.tick(1600);assert.equal(h.ready(),undefined);h.tick(2150);assert.equal(h.ready().at,2150);
});
test('wrong case fails before source release',()=>{
    const h=fixture();h.state.peer.case='main-on-alt-off';h.tick(1000);
    assert.equal(h.ready(),undefined);assert.equal(h.result().status,'failed');
});
test('partial recording cannot become complete',()=>{
    const h=fixture();h.tick(1000);h.tick(1600);h.state.peer.complete=true;h.tick(1700);
    assert.equal(h.result().status,'failed');assert.match(h.result().error,/incomplete/);
});
test('all fourteen stages retain reentry case and result identity',()=>{
    const h=fixture();h.tick(1000);h.tick(1600);let now=1700;
    for(let phase=0;phase<14;phase++){
        h.state.peer.phase=phase;h.state.peer.phase_label='synthetic-'+phase;
        h.state.peer.records.push({cursor_reply:[phase+1,1],cursor_reply_private:false,response_hex:'synthetic'});
        h.tick(now);h.tick(now+400);now+=500;
    }
    h.state.peer.complete=true;h.tick(now);
    assert.equal(h.result().status,'observed-all-mode-reentry-phases');
    assert.equal(h.result().case,'alternate-origin-reentry');assert.equal(h.result().observations.length,14);
    assert.equal(h.messages.filter(row=>row.kind==='mode_reentry_phase').length,14);
    assert.equal(h.messages.at(-1).kind,'mode_reentry_result');
});
const failed=results.filter(row=>row.status==='failed').length;
process.stdout.write(JSON.stringify({schema:'termforge-mode-reentry-stage3-test-v1',node_version:process.version,
    actual_source_sha256:crypto.createHash('sha256').update(source).digest('hex'),actual_stage3_executed:true,
    child_processes_spawned:0,sockets_opened:0,target_runs:0,case_count:results.length,failed,results})+'\n');
process.exitCode=failed?1:0;
