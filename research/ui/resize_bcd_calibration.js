// Two resize boundaries; observe the shrunk cursor before releasing one action.
let stage=0,busy=false,finished=false,due=0,stable=null,stableSince=0,lastPhase=-1;
let gridKey=null,gridSince=0,nawsSeen=0,lastNaws=null,requested=0,resized=0,committed=0;
const caseId=__RESIZE_BCD_CALIBRATION_JSON__;
const started=Date.now(),observations=[],geometryChecks=[];
const windows=[[1228,839],[988,509],[988,509],[1228,839]],grids=[[80,24],[60,12],[60,12],[80,24]];
for(const module of [wind,core,gui,widgets]){
    const expected=(__RUN_DIRECTORY_JSON__+'/app/'+module.name).replace(/\\/g,'/').toLowerCase();
    if(module.path.replace(/\\/g,'/').toLowerCase()!==expected)throw Error('pinned Qt module path guard');
}
function currentTerminal(){
    const raw=evalText(`JSON.stringify((function(){var t=window.activeText;if(!t)return null;function get(k){try{var v=typeof t[k]==='function'?t[k]():t[k];return v===undefined?null:v;}catch(e){return null;}}var n=get('visibleLineCount'),lines=[],markers=[],labels=[];if(typeof n!=='number'||n<0||n>200)throw Error('document line bound');for(var i=0;i<n;i++){var line=t.lineText(i);if(typeof line!=='string'||line.length>1024)throw Error('document text bound');lines.push(line);for(var j=0;j<labels.length;j++){var offset=-1;while((offset=line.indexOf(labels[j],offset+1))!==-1){if(markers.length>=256)throw Error('marker bound');var p=t.lineStart(i)+offset;markers.push({label:labels[j],document_line:i,column:offset,position:p,rect:t.visibleRectFromPosition(p)});}}}return {protocol:get('protocolName'),connected:get('isConnected'),top:get('topLine'),scroll:get('scrollTop'),line_count:n,capacity:get('screenLineCapacity'),lines:lines,markers:markers,caret_api:{caretLine:get('caretLine'),caretColumn:get('caretColumn'),caretPosition:get('caretPosition')}};})())`);
    return raw?JSON.parse(raw):null;
}
function write(name,value){const f=new File(__RUN_DIRECTORY_JSON__+'/'+name,'w');f.write(value);f.close();}
function windowGeometry(){
    const value=rect(main);
    if(!Array.isArray(value)||value.length!==4||!value.every(Number.isInteger)||value[2]<value[0]||value[3]<value[1])
        throw Error('invalid public window QRect');
    return {window_rect:value,window_size:[value[2]-value[0]+1,value[3]-value[1]+1]};
}
function checkWindow(phase){
    if(geometryChecks.length!==phase)throw Error('window geometry check order');
    const measured={phase,...windowGeometry(),expected_size:windows[phase]};geometryChecks.push(measured);
    if(JSON.stringify(measured.window_size)!==JSON.stringify(windows[phase]))throw Error('public window dimensions differ '+JSON.stringify(measured));
}
function finish(status,error){
    if(finished)return;finished=true;
    const result={status,case_id:caseId,error:error||null,observations,window_rect_format:'qt-qrect-inclusive-ltrb',geometry_checks:geometryChecks,
        full_active_grid_verified:false,local_shell_transport_verified:false,direct_telnet_protocol_display_only:true,presented_done:null};
    write('bcd-calibration-result.json',JSON.stringify(result));send({kind:'resize_address_result',...result});
}
function resetGrid(){gridKey=null;gridSince=0;}
function stableGrid(peer,terminal){
    const history=peer.naws_history;
    if(!Array.isArray(history)||history.length<nawsSeen||history.length>64)throw Error('NAWS history bound');
    for(let i=nawsSeen;i<history.length;i++){
        const event=history[i];
        if(!Array.isArray(event)||event.length!==2||event.some(n=>!Number.isInteger(n)||n<0||n>65535))throw Error('NAWS event bound');
        const value=JSON.stringify(event);if(lastNaws!==null&&lastNaws!==value)resetGrid();lastNaws=value;
    }
    nawsSeen=history.length;
    const value=JSON.stringify([peer.columns,peer.rows,terminal.capacity]);
    if(!peer.columns||peer.rows!==terminal.capacity||JSON.stringify([peer.columns,peer.rows])!==lastNaws){resetGrid();return false;}
    if(value!==gridKey){gridKey=value;gridSince=Date.now();return false;}
    return Date.now()-gridSince>=600;
}
function requestResize(phase){requested=phase;write('resize-request-'+phase,JSON.stringify({phase,window:windows[phase]}));}
send({kind:'probe_ready',station_isolated:true,scenario:'resize-address-v1',case_id:caseId});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
    try{
        if(wind.base.add(0xc29394).readU8()!==0)throw Error('first-run guard');
        if(Date.now()-started>41000)throw Error('bounded scenario deadline');
        if(stage===0){if(!ready('ui'))return;resize(main,...windows[0]);stage=1;}
        else if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);stage=2;}
        else if(stage===2){key(tree,0x1000004);stage=3;}
        else{
            const peer=peerState();if(!peer||!peer.connected){resetGrid();return;}
            if(peer.error)throw Error('peer '+peer.error);
            if(peer.complete){if(observations.length!==4)throw Error('incomplete BCD-address observations');finish('observed-four-bcd-calibration-states',null);return;}
            const terminal=currentTerminal();if(!terminal||!terminal.connected||terminal.protocol!=='Telnet'){resetGrid();return;}
            if(stage===3){
                if(!stableGrid(peer,terminal))return;
                if(peer.columns!==80||peer.rows!==24)throw Error('fixed initial grid differs');
                checkWindow(0);write('fixture-ready',JSON.stringify({grid:[peer.columns,peer.rows],event_count:peer.naws_history.length}));stage=4;return;
            }
            if(requested>committed&&peer.resize_armed===requested){
                if(resized!==requested){resize(main,...windows[requested]);resized=requested;resetGrid();return;}
                if(!stableGrid(peer,terminal))return;
                if(peer.columns!==grids[requested][0]||peer.rows!==grids[requested][1])throw Error('fixed resized grid differs');
                checkWindow(requested);
                write('resize-ready-'+requested,JSON.stringify({phase:requested,grid:[peer.columns,peer.rows],event_count:peer.naws_history.length}));committed=requested;return;
            }
            if(peer.phase===null||observations.length>peer.phase)return;
            if(peer.case_id!==caseId)throw Error('peer case differs');
            if(peer.phase!==lastPhase){lastPhase=peer.phase;stable=null;stableSince=Date.now();}
            const record=peer.records[peer.phase];if(!record.cursor_reply)return;
            if(terminal.capacity!==record.frozen_grid[1]||JSON.stringify(record.frozen_grid)!==JSON.stringify(grids[peer.phase]))throw Error('phase grid differs');
            if(peer.phase===2&&geometryChecks.length===2)checkWindow(2);
            const geometry=windowGeometry(),state=JSON.stringify([terminal,geometry]);
            if(JSON.stringify(geometry.window_size)!==JSON.stringify(windows[peer.phase]))throw Error('phase window dimensions differ');
            if(state!==stable){stable=state;stableSince=Date.now();return;}
            if(Date.now()-stableSince<400)return;
            if(observations.length!==peer.phase||observations.length>=4)throw Error('resize-address phase order');
            const observation={phase:peer.phase,label:peer.phase_label,terminal,...geometry,cursor_reply:record.cursor_reply,
                cursor_reply_private:record.cursor_reply_private,response_hex:record.response_hex,frozen_grid:record.frozen_grid};
            observations.push(observation);send({kind:'resize_address_phase',...observation});write('ack-'+peer.phase,'observed');
            if(peer.phase===0||peer.phase===2)requestResize(peer.phase+1);
        }
        due=Date.now()+75;
    }catch(error){finish('failed',String(error));}finally{busy=false;}
}});
setTimeout(()=>finish('failed','bounded observation timer'),42000);
