// Exact known-marker paint observations on the owned original Qt viewport only.
let stage=0,busy=false,finished=false,due=0,lastPhase=-1,stable=null,stableSince=0;
let paintPhase=-1,paintBusy=false,paintCount=0,requested=false,readyGrid=null,readySince=0;
const started=Date.now(),observations=[],draws=[],fills=[],knownMarkers=['TFM0','TFM1','TFM2','TFA0','TFA1','TFA2','TFA3','TFA4','TFC0','TFC1','TFC2','TFC3'];
for(const module of [wind,core,gui,widgets]){
    const expected=(__RUN_DIRECTORY_JSON__+'/app/'+module.name).replace(/\\/g,'/').toLowerCase();
    if(module.path.replace(/\\/g,'/').toLowerCase()!==expected)throw Error('pinned Qt module path guard');
}
const painterDevice=fn(gui,'?device@QPainter@@QEBAPEAVQPaintDevice@@XZ','pointer',['pointer']);
const painterPen=fn(gui,'?pen@QPainter@@QEBAAEBVQPen@@XZ','pointer',['pointer']);
const penColor=fn(gui,'?color@QPen@@QEBA?AVQColor@@XZ','pointer',['pointer','pointer']);
const brushColor=fn(gui,'?color@QBrush@@QEBAAEBVQColor@@XZ','pointer',['pointer']);
const colorParts=['red','green','blue','alpha'].map(name=>fn(gui,'?'+name+'@QColor@@QEBAHXZ','int',['pointer']));
const stringSize=fn(core,'?size@QString@@QEBAHXZ','int',['pointer']);
const updateWidget=fn(widgets,'?update@QWidget@@QEAAXXZ','void',['pointer']);
let paintDevices=new Map();
function rgba(color){const value=colorParts.map(read=>read(color));if(value.some(x=>x<0||x>255))throw Error('QColor component bound');return value;}
function currentTerminal(){
    const raw=evalText(`JSON.stringify((function(){var t=window.activeText;if(!t)return null;function get(k){try{var v=typeof t[k]==='function'?t[k]():t[k];return v===undefined?null:v;}catch(e){return null;}}function mode(k){var type=typeof t[k];if(type!=='function')return {callable:false,value:null,error:'not-callable'};try{return {callable:true,value:t[k](),error:null};}catch(e){return {callable:true,value:null,error:String(e)};}}var n=get('visibleLineCount');var lines=[],markers=[];if(typeof n==='number'&&n>=0&&n<=200){for(var i=0;i<n;i++){var line=t.lineText(i);lines.push(line);var re=/TF[MAC][0-4]/g,m;while((m=re.exec(line))!==null){var position=t.lineStart(i)+m.index;markers.push({marker:m[0],document_line:i,column:m.index,position:position,rect:t.visibleRectFromPosition(position)});}}}return {protocol:get('protocolName'),connected:get('isConnected'),scroll:get('scrollTop'),line_count:n,capacity:get('screenLineCapacity'),lines:lines,markers:markers,is_alt:mode('isAltScreen'),is_main:mode('isMainScreen')};})())`);
    return raw?JSON.parse(raw):null;
}
function finish(status,error){
    if(finished)return;finished=true;
    const result={status,error:error||null,observations,draws,fills,paint_calls_seen:paintCount,paint_observation:'known-marker-QPainter-calls',raster_image_verified:false,full_active_grid_verified:false,presented_done:null};
    const file=new File(__RUN_DIRECTORY_JSON__+'/style-probe-result.json','w');file.write(JSON.stringify(result));file.close();send({kind:'style_reference_result',...result});
}
function deviceRole(painter){return paintDevices.get(painterDevice(painter).toString())||null;}
const drawName='?drawText@QPainter@@QEAAXAEBVQPointF@@AEBVQString@@@Z';
Interceptor.attach(gui.getExportByName(drawName),{onEnter(args){
    if(finished||paintBusy||paintPhase<0||Process.getCurrentThreadId()!==uiThread)return;
    paintBusy=true;
    try{
        const role=deviceRole(args[0]);if(!role)return;paintCount++;
        const size=stringSize(args[2]);if(size<1||size>64)return;
        const text=constData(args[2]).readUtf16String(size),markers=knownMarkers.filter(marker=>text.indexOf(marker)>=0);
        if(!markers.length||! /^(?:TF[MAC][0-4]| )+$/.test(text))return;
        const point=[args[1].readDouble(),args[1].add(8).readDouble()];
        if(point.some(x=>!Number.isFinite(x)||Math.abs(x)>4096))throw Error('text coordinate bound');
        const color=Memory.alloc(16);penColor(painterPen(args[0]),color);
        const record={phase:paintPhase,role,api:'QPainter::drawText(QPointF,QString)',text,markers,point,pen_rgba:rgba(color)};
        const key=JSON.stringify(record);if(!draws.some(row=>JSON.stringify(row)===key)){if(draws.length>=256)throw Error('draw record bound');draws.push(record);send({kind:'known_marker_draw',...record});}
    }catch(e){finish('failed',String(e));}finally{paintBusy=false;}
}});
for(const floating of [false,true])for(const brush of [false,true]){
    const rectType=floating?'QRectF':'QRect',colorType=brush?'QBrush':'QColor';
    const api='?fillRect@QPainter@@QEAAXAEBV'+rectType+'@@AEBV'+colorType+'@@@Z';
    Interceptor.attach(gui.getExportByName(api),{onEnter(args){
        if(finished||paintBusy||paintPhase<0||Process.getCurrentThreadId()!==uiThread)return;
        paintBusy=true;
        try{
            const role=deviceRole(args[0]);if(!role)return;
            let rectangle;if(floating)rectangle=[0,8,16,24].map(offset=>args[1].add(offset).readDouble());
            else{const r=[0,4,8,12].map(offset=>args[1].add(offset).readS32());rectangle=[r[0],r[1],r[2]-r[0]+1,r[3]-r[1]+1];}
            if(rectangle.some(x=>!Number.isFinite(x)||Math.abs(x)>4096))throw Error('fill coordinate bound');
            const record={phase:paintPhase,role,api:'QPainter::fillRect('+rectType+','+colorType+')',rectangle,rgba:rgba(brush?brushColor(args[2]):args[2])};
            const key=JSON.stringify(record);if(!fills.some(row=>JSON.stringify(row)===key)){if(fills.length>=1024)throw Error('fill record bound');fills.push(record);}
        }catch(e){finish('failed',String(e));}finally{paintBusy=false;}
    }});
}
send({kind:'probe_ready',station_isolated:true,scenario:'alternate-style-reference',paint_contract:'verified-Qt5-export-calls'});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
    try{
        if(wind.base.add(0xc29394).readU8()!==0)throw Error('first-run guard');
        if(Date.now()-started>41000)throw Error('bounded scenario deadline');
        if(stage===0){if(!ready('ui'))return;resize(main,1360,860);stage=1;}
        else if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);stage=2;}
        else if(stage===2){key(tree,0x1000004);stage=3;}
        else{
            const peer=peerState();if(!peer||!peer.connected)return;
            if(peer.error)throw Error('peer '+peer.error);
            if(peer.complete){finish('observed-all-style-phases',null);return;}
            const terminal=currentTerminal();if(!terminal||!terminal.connected||terminal.protocol!=='Telnet')return;
            if(stage===3){
                const grid=JSON.stringify([peer.columns,peer.rows,terminal.capacity]);
                if(!peer.columns||peer.rows!==terminal.capacity)return;
                if(grid!==readyGrid){readyGrid=grid;readySince=Date.now();return;}
                if(Date.now()-readySince<600)return;
                scan();const viewport=viewPort();paintDevices=new Map([[viewport.add(0x10).toString(),'owned-terminal-viewport'],[editor.add(0x10).toString(),'owned-terminal-editor']]);
                const flag=new File(__RUN_DIRECTORY_JSON__+'/fixture-ready','w');flag.write('stable-NAWS-capacity');flag.close();stage=4;return;
            }
            if(peer.phase===null)return;
            if(peer.phase!==lastPhase){lastPhase=peer.phase;stable=null;stableSince=Date.now();requested=false;}
            const record=peer.records[peer.phase];if(!record.cursor_reply)return;
            const state=JSON.stringify(terminal);
            if(state!==stable){stable=state;stableSince=Date.now();requested=false;return;}
            if(Date.now()-stableSince<400)return;
            if(!requested){paintPhase=peer.phase;updateWidget(viewPort());requested=true;stableSince=Date.now();return;}
            if(Date.now()-stableSince<450)return;
            const observation={phase:peer.phase,label:peer.phase_label,terminal,cursor_reply:record.cursor_reply,paint_requested:true,matching_marker_draws:draws.filter(row=>row.phase===peer.phase).length};
            observations.push(observation);send({kind:'style_phase',...observation});
            const ack=new File(__RUN_DIRECTORY_JSON__+'/ack-'+peer.phase,'w');ack.write('observed');ack.close();
            stableSince=Date.now()+100000;
        }
        due=Date.now()+75;
    }catch(e){finish('failed',String(e));}finally{busy=false;}
}});
setTimeout(()=>finish('failed','bounded observation timer'),42000);
