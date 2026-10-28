// Five-stage, one-grab-per-stage controlled raster; raw ROI samples stay local.
let stage=0,busy=false,finished=false,due=0,lastPhase=-1,stable=null,stableSince=0,readyGrid=null,readySince=0;
const started=Date.now(),captures=[],grabs=[],targets=[{label:'M0 H H',offsets:[3,5]},{label:'A0 H',offsets:[3]},{label:'A1 H',offsets:[3]},{label:'A2 H',offsets:[3]},{label:'M1 H',offsets:[3]}];
function modulePathMatches(actual,directory,name){return actual.replace(/\\/g,'/').toLowerCase()===(directory+'/app/'+name).replace(/\\/g,'/').toLowerCase();}
const moduleGuards=[];
for(const module of [wind,core,gui,widgets]){
    if(!modulePathMatches(module.path,__RUN_DIRECTORY_JSON__,module.name))throw Error('pinned-module-path');
    moduleGuards.push({name:module.name,path_guard_passed:true});
}
function api(module,name,rva,result,args){const address=module.getExportByName(name);if(address.sub(module.base).toInt32()!==rva)throw Error('pinned-export-rva');return new NativeFunction(address,result,args);}
const grab=api(widgets,'?grab@QWidget@@QEAA?AVQPixmap@@AEBVQRect@@@Z',0x39520,'pointer',['pointer','pointer','pointer']);
const toImage=api(gui,'?toImage@QPixmap@@QEBA?AVQImage@@XZ',0xad6f0,'pointer',['pointer','pointer']);
const pixel=api(gui,'?pixel@QImage@@QEBAIHH@Z',0x8e560,'uint32',['pointer','int','int']);
const imageWidth=api(gui,'?width@QImage@@QEBAHXZ',0x94770,'int',['pointer']);
const imageHeight=api(gui,'?height@QImage@@QEBAHXZ',0x8d1a0,'int',['pointer']);
const imageNull=api(gui,'?isNull@QImage@@QEBA_NXZ',0x8da30,'bool',['pointer']);
const pixmapDpr=api(gui,'?devicePixelRatio@QPixmap@@QEBANXZ',0xabe80,'double',['pointer']);
const imageDpr=api(gui,'?devicePixelRatio@QImage@@QEBANXZ',0x8c2c0,'double',['pointer']);
const destroyImage=api(gui,'??1QImage@@UEAA@XZ',0x86350,'void',['pointer']);
const destroyPixmap=api(gui,'??1QPixmap@@UEAA@XZ',0xab020,'void',['pointer']);
function must(condition,code){if(!condition){const error=new Error(code);error.code=code;throw error;}}
function same(a,b){return JSON.stringify(a)===JSON.stringify(b);}
function rgba(value){return [(value>>>16)&255,(value>>>8)&255,value&255,(value>>>24)&255];}
function terminalState(index){
    const descriptor=targets[index]||{label:'',offsets:[]};
    const source=`JSON.stringify((function(){var t=window.activeText;if(!t)return null;var label=${JSON.stringify(descriptor.label)},offsets=${JSON.stringify(descriptor.offsets)};function get(k){try{var v=typeof t[k]==='function'?t[k]():t[k];return v===undefined?null:v;}catch(e){return null;}}var count=get('visibleLineCount'),lines=[],found=[];if(typeof count==='number'&&count>=0&&count<=200)for(var i=0;i<count;i++){var line=t.lineText(i);lines.push(line);if(label){var at=line.indexOf(label);if(at>=0){for(var j=0;j<offsets.length;j++){var col=at+offsets[j],p=t.lineStart(i)+col;found.push({label:label,ordinal:j,document_line:i,column:col,position:p,h_rect:t.visibleRectFromPosition(p),space_rect:t.visibleRectFromPosition(p-1),preceding_character:line.charAt(col-1),character:line.charAt(col)});}}}}var caret=get('caretPosition');return {protocol:get('protocolName'),connected:get('isConnected'),capacity:get('screenLineCapacity'),line_count:count,lines:lines,scroll:get('scrollTop'),markers:found,is_alt:typeof t.isAltScreen==='function'?t.isAltScreen():null,is_main:typeof t.isMainScreen==='function'?t.isMainScreen():null,selection:get('selectionText'),caret_position:caret,caret_rect:typeof caret==='number'?t.visibleRectFromPosition(caret):null};})())`;
    const result=evalText(source);return result?JSON.parse(result):null;
}
function overlap(a,b){return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;}
function physicalRect(rect,dpr,width,height){
    must(rect&&[rect.x,rect.y,rect.width,rect.height].every(x=>Number.isFinite(x)),'invalid-marker-rectangle');
    const values=[rect.x*dpr,rect.y*dpr,rect.width*dpr,rect.height*dpr];
    must(values.every(Number.isInteger),'noninteger-roi-boundary');
    const [x,y,w,h]=values;must(w>0&&h>0&&w*h<=8192&&x>=0&&y>=0&&x+w<=width&&y+h<=height,'roi-out-of-bounds');return [x,y,w,h];
}
function readRoi(image,rectangle){const [x,y,w,h]=rectangle,pixels=[];for(let row=y;row<y+h;row++)for(let col=x;col<x+w;col++)pixels.push(pixel(image,col,row)>>>0);return pixels;}
function colors(h,space){
    const background=space[0];must(space.length>0&&space.every(value=>value===background),'space-background-ambiguous');
    const counts=new Map();for(const value of h)if(value!==background)counts.set(value,(counts.get(value)||0)+1);
    must(counts.size>0,'no-marker-ink');
    const ordered=Array.from(counts).sort((a,b)=>b[1]-a[1]||a[0]-b[0]);
    must(ordered.length===1||ordered[0][1]>ordered[1][1],'dominant-ink-ambiguous');
    return {background_rgba:rgba(background),dominant_ink_rgba:rgba(ordered[0][0])};
}
function sampleViewport(index,terminal,cursor){
    const capture={phase:index,label:targets[index].label,cursor_reply:cursor,terminal,grab_attempted:false,module_guards:moduleGuards,image:null,rois:[],constructed:[],destroyed:[],calibration:null,reason:null};
    const pixmap=Memory.alloc(32),image=Memory.alloc(32);let pixmapReady=false,imageReady=false;
    try{
        scan();const candidates=cached.filter(row=>row.name==='qt_scrollarea_viewport'&&row.parent===editor.toString());must(candidates.length===1,'owned-viewport-ambiguous');
        const viewport=ptr(candidates[0].ptr),g=rect(viewport),width=g[2]-g[0]+1,height=g[3]-g[1]+1;
        must(width>0&&height>0&&width<=4096&&height<=4096,'viewport-dimensions');
        must(typeof terminal.is_alt==='boolean'&&typeof terminal.is_main==='boolean'&&terminal.is_alt!==terminal.is_main,'mode-getters-unavailable');
        must(terminal.selection==='','selection-overlaps-observation');
        must(terminal.markers.length===targets[index].offsets.length,'marker-binding-ambiguous');
        const region=Memory.alloc(16);region.writeS32(0);region.add(4).writeS32(0);region.add(8).writeS32(width-1);region.add(12).writeS32(height-1);
        must(!grabs.includes(index),'extra-grab-for-phase');grabs.push(index);capture.grab_attempted=true;
        const returnedPixmap=grab(viewport,pixmap,region);pixmapReady=true;capture.constructed.push('pixmap');must(returnedPixmap.equals(pixmap),'pixmap-return-address');
        const returnedImage=toImage(pixmap,image);imageReady=true;capture.constructed.push('image');must(returnedImage.equals(image),'image-return-address');
        const pdpr=pixmapDpr(pixmap),idpr=imageDpr(image),iw=imageWidth(image),ih=imageHeight(image),empty=imageNull(image);
        capture.image={viewport_width:width,viewport_height:height,grab_rect:[0,0,width-1,height-1],pixmap_dpr:pdpr,image_dpr:idpr,width:iw,height:ih,is_null:empty};
        must(!empty&&iw>0&&ih>0,'empty-image');
        must(Number.isFinite(pdpr)&&pdpr>0&&pdpr<=4&&pdpr===idpr,'dpr-inconsistent');
        must(Number.isInteger(width*pdpr)&&Number.isInteger(height*pdpr)&&iw===width*pdpr&&ih===height*pdpr,'image-viewport-dpr-inconsistent');
        must(terminal.caret_rect&&[terminal.caret_rect.x,terminal.caret_rect.y,terminal.caret_rect.width,terminal.caret_rect.height].every(Number.isFinite),'caret-location-unknown');
        for(const marker of terminal.markers){
            must(marker.character==='H'&&marker.preceding_character===' ','marker-text-mismatch');
            must(!overlap(marker.h_rect,terminal.caret_rect)&&!overlap(marker.space_rect,terminal.caret_rect),'caret-overlaps-roi');
            const hRect=physicalRect(marker.h_rect,pdpr,iw,ih),spaceRect=physicalRect(marker.space_rect,pdpr,iw,ih);
            must(hRect[2]===spaceRect[2]&&hRect[3]===spaceRect[3],'h-space-size-inconsistent');
            const hPixels=readRoi(image,hRect),spacePixels=readRoi(image,spaceRect);
            const record={ordinal:marker.ordinal,h_rect:hRect,space_rect:spaceRect,h_pixels:hPixels,space_pixels:spacePixels,colors:null};capture.rois.push(record);
            record.colors=colors(hPixels,spacePixels);
        }
        if(index===0)must(same(capture.rois[0].colors,capture.rois[1].colors),'calibration-H-colors-differ');
        if(index===2)must(!same(capture.rois[0].colors,captures[0].rois[0].colors),'reference-styles-not-distinguishable');
        capture.calibration=true;
    }catch(error){capture.calibration=false;capture.reason=error.code||'native-or-observation-error';capture.private_error=String(error);}
    finally{
        for(const object of [{ready:imageReady,name:'image',value:image,destroy:destroyImage},{ready:pixmapReady,name:'pixmap',value:pixmap,destroy:destroyPixmap}]){
            if(object.ready)try{object.destroy(object.value);capture.destroyed.push(object.name);}
            catch(error){capture.calibration=false;capture.reason='object-destruction-failed';capture.private_error=String(error);}
        }
    }
    return capture;
}
function finish(status,reason){
    if(finished)return;finished=true;
    const result={status,reason:reason||null,captures,grab_phases:grabs,module_guards:moduleGuards,presented_done:null};
    const file=new File(__RUN_DIRECTORY_JSON__+'/raster-result.json','w');file.write(JSON.stringify(result));file.close();send({kind:'raster_result',...result});
}
send({kind:'probe_ready',station_isolated:true,scenario:'five-stage-raster-style',module_guards:moduleGuards});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
    try{
        must(wind.base.add(0xc29394).readU8()===0,'first-run-guard');must(Date.now()-started<=41000,'scenario-deadline');
        if(stage===0){if(!ready('ui'))return;resize(main,1360,860);stage=1;}
        else if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);stage=2;}
        else if(stage===2){key(tree,0x1000004);stage=3;}
        else{
            const peer=peerState();if(!peer||!peer.connected)return;must(!peer.error,'peer-failed');
            if(peer.complete){finish('observed-five-raster-phases',null);return;}
            const index=peer.phase===null?0:peer.phase,terminal=terminalState(index);if(!terminal||!terminal.connected||terminal.protocol!=='Telnet')return;
            if(stage===3){const grid=JSON.stringify([peer.columns,peer.rows,terminal.capacity]);if(!peer.columns||peer.rows!==terminal.capacity)return;if(grid!==readyGrid){readyGrid=grid;readySince=Date.now();return;}if(Date.now()-readySince<600)return;const flag=new File(__RUN_DIRECTORY_JSON__+'/fixture-ready','w');flag.write('stable-NAWS-capacity');flag.close();stage=4;return;}
            if(peer.phase===null)return;
            if(peer.phase!==lastPhase){lastPhase=peer.phase;stable=null;stableSince=Date.now();}
            const record=peer.records[index];if(!record.cursor_reply)return;
            const state=JSON.stringify(terminal);if(state!==stable){stable=state;stableSince=Date.now();return;}if(Date.now()-stableSince<450)return;
            const capture=sampleViewport(index,terminal,record.cursor_reply);captures.push(capture);send({kind:'raster_phase',...capture});
            if(!capture.calibration){finish('partial-raster-observation',capture.reason);return;}
            const ack=new File(__RUN_DIRECTORY_JSON__+'/ack-'+index,'w');ack.write('observed');ack.close();stableSince=Date.now()+100000;
        }
        due=Date.now()+75;
    }catch(error){finish('partial-raster-observation',error.code||'driver-observation-error');}finally{busy=false;}
}});
setTimeout(()=>finish('partial-raster-observation','bounded-observation-timer'),42000);
