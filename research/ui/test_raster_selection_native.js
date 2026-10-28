'use strict';
// Run the production helper bodies with synthetic Qt calls, without Frida/Qt.
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync(require('path').join(__dirname,'raster_selection_reference.js'),'utf8');
function helpers(extra={}){
    const context=vm.createContext({Number,Map,JSON,Error,...extra});
    vm.runInContext(source.slice(source.indexOf('function modulePathMatches'),source.indexOf('const moduleGuards')),context);
    vm.runInContext(source.slice(source.indexOf('function must'),source.indexOf('function terminalState')),context);
    vm.runInContext(source.slice(source.indexOf('function overlap'),source.indexOf('function finish')),context);
    return context;
}
function equal(a,b){assert.strictEqual(JSON.stringify(a),JSON.stringify(b));}
const pure=helpers();
assert(pure.modulePathMatches('D:\\OWNED\\app\\Qt5Gui.dll','d:/owned','Qt5Gui.dll'));
assert(!pure.modulePathMatches('D:/other/app/Qt5Gui.dll','d:/owned','Qt5Gui.dll'));
equal(pure.rgba(0xff123456),[0x12,0x34,0x56,0xff]);
assert.throws(()=>pure.physicalRect({x:.5,y:0,width:1,height:1},1,8,2),/noninteger/);
assert.throws(()=>pure.physicalRect({x:7,y:0,width:2,height:1},1,8,2),/out-of-bounds/);
assert.throws(()=>pure.colors([1,1],[1,1]),/no-marker-ink/);
assert.throws(()=>pure.colors([2,3],[1,1]),/dominant-ink-ambiguous/);
assert.throws(()=>pure.colors([2,2],[1,3]),/space-background-ambiguous/);
assert(pure.requireCollapsedSelection(97,97));
assert.throws(()=>pure.requireCollapsedSelection(97,98),/selection-not-collapsed/);
for(const value of [null,undefined,NaN,Infinity,-1,.5,'97',true,9007199254740992])assert.throws(()=>pure.requireCollapsedSelection(value,value),/selection-endpoints-unavailable/);
equal(pure.completeTokenIndices('M1S1 H','M1 H'),[]);
equal(pure.completeTokenIndices(' M1 H M1S1 H','M1 H'),[1]);
equal(pure.completeTokenIndices('XM1 H','M1 H'),[]);
assert(pure.overlap({x:0,y:0,width:2,height:2},pure.caretOccupancy({x:0,y:0,width:0,height:2})));
assert(!pure.overlap({x:0,y:0,width:2,height:2},pure.caretOccupancy({x:2,y:0,width:0,height:2})));
const replay=JSON.parse(fs.readFileSync(require('path').join(__dirname,'results/2026-10-28-raster-selection-proposal/replay.json'),'utf8'));
for(const row of replay.records){let allowed=true,reason=null;try{pure.requireCollapsedSelection(row.selection_caret,row.selection_anchor);}catch(error){allowed=false;reason=error.code;}assert.strictEqual(allowed,row.selection_gate_allowed);assert.strictEqual(reason,row.reason);}
function lifecycle(failure){
    const calls=[],allocations=[];
    function pointer(){const result={data:[],equals(other){return result===other;},writeS32(value){result.data[0]=value;},add(offset){return {writeS32(value){result.data[offset/4]=value;}};}};return result;}
    const editor={toString(){return 'owned-editor';}},viewport={};
    const markers=[0,1,2,3].map(ordinal=>({ordinal,group:ordinal<2?'default':'main',sample:ordinal%2,character:'H',preceding_character:' ',h_rect:{x:ordinal*4+2,y:0,width:2,height:2},space_rect:{x:ordinal*4+(failure==='adjacency'?1:0),y:0,width:2,height:2}}));
    const context=helpers({targets:[{label:'D0/M0 H references',markers:[{},{},{},{}]}],moduleGuards:[],captures:[],grabs:[],editor,
        scan(){},cached:[{name:'qt_scrollarea_viewport',parent:'owned-editor',ptr:'owned-viewport'}],ptr(){return viewport;},rect(){return [0,0,15,1];},
        Memory:{alloc(size){allocations.push(size);return pointer();}},
        grab(widget,result,region){calls.push('grab');assert.strictEqual(widget,viewport);equal(region.data,[0,0,15,1]);if(failure==='grab')throw Error('synthetic grab failure');return result;},
        toImage(pixmap,result){calls.push('toImage');if(failure==='toImage')throw Error('synthetic toImage failure');return result;},
        imageWidth(){return 16;},imageHeight(){return 2;},imageNull(){return failure==='empty';},pixmapDpr(){return 1;},imageDpr(){return failure==='dpr'?2:1;},
        pixel(image,x,y){if(failure==='pixel')throw Error('synthetic pixel failure');const foreground=x<8?0xffc0c0c0:0xffaa0000,background=x<8?0xff101010:0xff0000aa;return y===0&&x%4===2?foreground:background;},
        destroyImage(){calls.push('destroy-image');if(failure==='destroy')throw Error('synthetic destructor failure');},destroyPixmap(){calls.push('destroy-pixmap');}});
    const capture=context.sampleViewport(0,{is_alt:false,is_main:true,selection_caret:failure==='unknown'?null:9,selection_anchor:failure==='unknown'?null:failure==='active'?10:9,selection_text_type:'string',selection_text_length:10,markers,caret_rect:{x:failure==='caret'?0:16,y:0,width:0,height:2}},[4,9]);
    equal(allocations,capture.grab_attempted?[32,32,16]:[32,32]);
    if(failure==='unknown'||failure==='active'){equal(calls,[]);equal(capture.constructed,[]);equal(capture.destroyed,[]);}
    else if(failure==='grab'){equal(calls,['grab']);equal(capture.constructed,[]);equal(capture.destroyed,[]);}
    else if(failure==='toImage'){equal(calls,['grab','toImage','destroy-pixmap']);equal(capture.constructed,['pixmap']);equal(capture.destroyed,['pixmap']);}
    else{equal(calls,['grab','toImage','destroy-image','destroy-pixmap']);equal(capture.constructed,['pixmap','image']);}
    if(!failure){assert(capture.calibration);equal(capture.destroyed,['image','pixmap']);}
    else assert.strictEqual(capture.calibration,false);
    equal(context.grabs,capture.grab_attempted?[0]:[]);
}
for(const failure of [null,'unknown','active','caret','adjacency','grab','toImage','empty','dpr','pixel','destroy'])lifecycle(failure);
process.stdout.write('endpoint-only replay, full-token labels, caret overlap, QRgb, D0/M0 and native lifecycle boundaries passed\n');
