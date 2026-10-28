// Pure V8 tests of the actual stage-3 body; no target, socket or child process.
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const source=fs.readFileSync(process.argv[2],'utf8');
const cases=[];
function fixture(){
    const state={now:0,peer:{connected:true,columns:91,rows:25,phase:null,naws_history:[[91,25]]},
                 terminal:{connected:true,protocol:'Telnet',capacity:25,line_count:1,lines:['synthetic']}};
    const files=new Map(),messages=[],root='/owned/origin-wrap';
    const module=name=>({name,path:root+'/app/'+name,base:{add:()=>({readU8:()=>0})}});
    const context={__RUN_DIRECTORY_JSON__:root,uiThread:1,
        wind:module('WindTerm.exe'),core:module('Qt5Core.dll'),gui:module('Qt5Gui.dll'),widgets:module('Qt5Widgets.dll'),
        user:{getExportByName:()=>1},Process:{getCurrentThreadId:()=>1},
        Date:{now:()=>state.now},send:value=>messages.push(value),setTimeout:()=>1,
        File:class{constructor(name){this.name=name;this.data='';}write(data){this.data+=data;}close(){files.set(this.name,{at:state.now,data:this.data});}},
        peerState:()=>state.peer,evalText:()=>JSON.stringify(state.terminal),
        ready:()=>true,resize:()=>{},scan:()=>{},expandAll:()=>{},focus:()=>{},key:()=>{},main:1,tree:2};
    context.Interceptor={attach:(_,callbacks)=>{context.dispatch=callbacks.onEnter;}};
    vm.createContext(context);
    vm.runInContext(source,context,{timeout:1000});
    function tick(now){state.now=now;vm.runInContext('dispatch()',context,{timeout:100});}
    function ready(){return files.get(root+'/fixture-ready');}
    function noReady(){assert.equal(ready(),undefined,'continuous stable interval has not elapsed');}
    tick(0);tick(100);tick(200); // Drive the real startup transitions, without writing stage.
    assert.equal(vm.runInContext('stage',context),3);
    return {state,tick,ready,noReady,messages};
}
function test(name,run){cases.push({name,run});}
test('continuous matching grid waits exactly 600ms',()=>{
    const h=fixture();h.tick(1000);h.tick(1599);h.noReady();h.tick(1600);
    assert.equal(h.ready().at,1600);
});
test('identical duplicate NAWS does not restart stable wait',()=>{
    const h=fixture();h.tick(1000);h.state.peer.naws_history.push([91,25]);h.tick(1550);h.tick(1600);
    assert.equal(h.ready().at,1600);
});
test('transient NAWS change and return restarts on observation',()=>{
    const h=fixture();h.tick(1000);h.state.peer.naws_history.push([92,25],[91,25]);h.tick(1550);
    h.tick(1600);h.noReady();h.tick(2149);h.noReady();h.tick(2150);
    assert.equal(h.ready().at,2150);
});
test('batched NAWS change first seen at old deadline restarts wait',()=>{
    const h=fixture();h.tick(1000);h.state.peer.naws_history.push([92,25],[91,25]);
    h.tick(1600);h.noReady();h.tick(2199);h.noReady();h.tick(2200);
    assert.equal(h.ready().at,2200);
});
for(const [name,invalidate,restore] of [
    ['capacity mismatch',s=>{s.terminal.capacity=26;},s=>{s.terminal.capacity=25;}],
    ['terminal disconnected',s=>{s.terminal.connected=false;},s=>{s.terminal.connected=true;}],
    ['peer disconnected',s=>{s.peer.connected=false;},s=>{s.peer.connected=true;}],
    ['peer temporarily missing',s=>{s.savedPeer=s.peer;s.peer=null;},s=>{s.peer=s.savedPeer;}],
    ['terminal temporarily missing',s=>{s.savedTerminal=s.terminal;s.terminal=null;},s=>{s.terminal=s.savedTerminal;}],
    ['terminal protocol mismatch',s=>{s.terminal.protocol='not-Telnet';},s=>{s.terminal.protocol='Telnet';}],
])test(name+' clears prior stable interval',()=>{
    const h=fixture();h.tick(1000);invalidate(h.state);h.tick(1550);restore(h.state);h.tick(1600);
    h.noReady();h.tick(2199);h.noReady();h.tick(2200);assert.equal(h.ready().at,2200);
});
const results=[];
for(const entry of cases){try{entry.run();results.push({case:entry.name,status:'passed'});}
    catch(error){results.push({case:entry.name,status:'failed',error:String(error.message)});}}
const failed=results.filter(row=>row.status==='failed').length;
process.stdout.write(JSON.stringify({schema:'termforge-origin-wrap-stage3-test-v1',node_version:process.version,
    node_arch:process.arch,node_platform:process.platform,actual_source_sha256:crypto.createHash('sha256').update(source).digest('hex'),
    actual_stage3_executed:true,child_processes_spawned:0,sockets_opened:0,target_runs:0,
    case_count:results.length,failed,results})+'\n');
process.exitCode=failed?1:0;
