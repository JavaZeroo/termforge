'use strict';
// Synthetic Qt is the external boundary; no original process or real image is used.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const source=fs.readFileSync(path.join(__dirname,'raster_raw_reference.js'),'utf8');
function equal(a,b){assert.strictEqual(JSON.stringify(a),JSON.stringify(b));}
function observe(failure){
    const allocations=[],events=[];
    function pointer(){const result={data:[],equals(other){return result===other;},writeS32(value){result.data[0]=value;},add(offset){return {writeS32(value){result.data[offset/4]=value;}};}};return result;}
    const editor={toString(){return 'owned-editor';}},viewport={};
    const markers=[0,1,2,3].map(ordinal=>({ordinal,label:ordinal<2?'D0 H H':'M0 H H',group:ordinal<2?'default':'main',sample:ordinal%2,character:'H',preceding_character:' ',h_rect:{x:ordinal*4+2,y:0,width:2,height:2},space_rect:{x:ordinal*4+(failure==='adjacency'?1:0),y:0,width:2,height:2}}));
    const descriptors=markers.map(marker=>({label:marker.label,group:marker.group,sample:marker.sample}));
    if(failure==='identity')markers[1]={...markers[0]};
    const context=vm.createContext({Number,Map,JSON,Error,targets:[{label:'D0/M0 H references',markers:descriptors}],moduleGuards:[],captures:[],grabs:[],editor,
        scan(){},cached:[{name:'qt_scrollarea_viewport',parent:'owned-editor',ptr:'owned-viewport'}],ptr(){return viewport;},rect(){return [0,0,15,1];},
        Memory:{alloc(size){allocations.push(size);return pointer();}},
        grab(widget,result,region){events.push('grab');equal(region.data,[0,0,15,1]);if(failure==='grab')throw Error('synthetic grab failure');return result;},
        toImage(pixmap,result){events.push('toImage');if(failure==='toImage')throw Error('synthetic toImage failure');return result;},
        imageWidth(){return 16;},imageHeight(){return 2;},imageNull(){return failure==='empty'?1:0;},pixmapDpr(){return 1;},imageDpr(){return failure==='dpr'?2:1;},
        // Every H has a tied two-color histogram; all four H/space pairs must survive.
        pixel(image,x,y){events.push('pixel');if(failure==='pixel')throw Error('synthetic pixel failure');if(x%4<2)return 0xff101010;return y===0?0xff112233:0xff445566;},
        destroyImage(){events.push('destroy-image');if(failure==='destroy')throw Error('synthetic destructor failure');},destroyPixmap(){events.push('destroy-pixmap');}});
    vm.runInContext(source.slice(source.indexOf('function modulePathMatches'),source.indexOf('const moduleGuards')),context);
    vm.runInContext(source.slice(source.indexOf('function must'),source.indexOf('function terminalState')),context);
    vm.runInContext(source.slice(source.indexOf('function overlap'),source.indexOf('function finish')),context);
    const capture=context.sampleViewport(0,{is_alt:false,is_main:true,selection_caret:failure==='unknown'?null:9,selection_anchor:failure==='unknown'?null:failure==='active'?10:9,selection_text_type:'string',selection_text_length:10,markers,caret_rect:{x:failure==='caret'?0:16,y:0,width:0,height:2}},[4,9]);
    return {capture,events,allocations};
}
const tied=observe();
assert.strictEqual(tied.capture.rois.length,4,'collect the whole phase before any color classification');
assert.strictEqual(tied.capture.capture_valid,true,'color ties cannot stop structural capture');
for(const roi of tied.capture.rois){assert.strictEqual(roi.h_pixels.length,4);assert.strictEqual(roi.space_pixels.length,4);assert(!('colors' in roi));}
assert.strictEqual(tied.events.filter(event=>event==='pixel').length,32);
equal(tied.capture.destroyed,['image','pixmap']);equal(tied.allocations,[32,32,16]);
for(const failure of ['unknown','active','identity','caret','adjacency','empty','dpr','grab','toImage','pixel','destroy']){
    const result=observe(failure);assert.strictEqual(result.capture.capture_valid,false,failure+' remains a stop');
    if(failure==='unknown'||failure==='active'||failure==='identity')assert(!result.events.includes('grab'));
    if(failure==='grab')equal(result.capture.constructed,[]);
    if(failure==='toImage')equal(result.capture.destroyed,['pixmap']);
    if(failure==='pixel')equal(result.capture.destroyed,['image','pixmap']);
    if(failure==='destroy')equal(result.capture.destroyed,['pixmap']);
}
process.stdout.write('all tied H/space ROIs captured; structural/endpoint/caret/DPR guards still stop\n');
