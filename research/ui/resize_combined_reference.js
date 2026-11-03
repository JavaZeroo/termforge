// Every fixed single-axis resize is observed before the next; no input after the ASCII body except DSR.
let stage=0,busy=false,finished=false,due=0,stable=null,stableSince=0,lastPhase=-1;
let gridKey=null,gridSince=0,nawsSeen=0,lastNaws=null,requested=0,resized=0,committed=0;
const contract=__RESIZE_COMBINED_CASE_JSON__,caseId=contract.case_id;
const counts={'width-height-retention':5};
const phaseCount=counts[caseId],lastStage=phaseCount-1;
const windows=contract.windows,grids=contract.grids,resizePhases=contract.resize_phases;
if(!phaseCount||windows.length!==phaseCount||grids.length!==phaseCount||contract.labels.length!==phaseCount||resizePhases.length!==phaseCount-1||!Array.isArray(contract.marker_labels))throw Error('fixed combined contract');
const started=Date.now(),observations=[],geometryChecks=[];
for(const module of [wind,core,gui,widgets]){
    const expected=(__RUN_DIRECTORY_JSON__+'/app/'+module.name).replace(/\\/g,'/').toLowerCase();
    if(module.path.replace(/\\/g,'/').toLowerCase()!==expected)throw Error('pinned Qt module path guard');
}
function currentTerminal(){
    const raw=evalText(`JSON.stringify((function(){var t=window.activeText;if(!t)return null;function get(k){try{var v=typeof t[k]==='function'?t[k]():t[k];return v===undefined?null:v;}catch(e){return null;}}var n=get('visibleLineCount'),lines=[],markers=[],labels=${JSON.stringify(contract.marker_labels)};if(typeof n!=='number'||n<0||n>200)throw Error('document line bound');for(var i=0;i<n;i++){var line=t.lineText(i);if(typeof line!=='string'||line.length>1024)throw Error('document text bound');lines.push(line);for(var j=0;j<labels.length;j++){var offset=-1;while((offset=line.indexOf(labels[j],offset+1))!==-1){if(markers.length>=256)throw Error('marker bound');var start=t.lineStart(i),p=start+offset;markers.push({label:labels[j],document_line:i,column:offset,line_start_api:start,position:p,rect:t.visibleRectFromPosition(p)});}}}return {protocol:get('protocolName'),connected:get('isConnected'),top:get('topLine'),scroll:get('scrollTop'),line_count:n,capacity:get('screenLineCapacity'),lines:lines,markers:markers,caret_api:{caretLine:get('caretLine'),caretColumn:get('caretColumn'),caretPosition:get('caretPosition')}};})())`);
    return raw?JSON.parse(raw):null;
}
function write(name,value){const f=new File(__RUN_DIRECTORY_JSON__+'/'+name,'w');f.write(value);f.close();}
function windowGeometry(){
    const value=rect(main);
    if(!Array.isArray(value)||value.length!==4||!value.every(Number.isInteger)||value[2]<value[0]||value[3]<value[1])
        throw Error('invalid public window QRect');
    return {window_rect:value,window_size:[value[2]-value[0]+1,value[3]-value[1]+1]};
}
function geometrySample(phase,peer,terminal){
    const scalar=value=>Number.isInteger(value)?value:null;
    return {phase,requested_window:windows[phase],...windowGeometry(),
        observed_grid:peer?[scalar(peer.columns),scalar(peer.rows)]:null,
        terminal_capacity:terminal?scalar(terminal.capacity):null,
        event_count:peer&&Array.isArray(peer.naws_history)?peer.naws_history.length:null};
}
function checkGeometry(phase,peer,terminal){
    if(geometryChecks.length!==phase)throw Error('window geometry check order');
    const sample=geometrySample(phase,peer,terminal);
    const measured={phase,window_rect:sample.window_rect,window_size:sample.window_size,expected_size:windows[phase],
        observed_grid:sample.observed_grid,terminal_capacity:sample.terminal_capacity,event_count:sample.event_count};
    // Persist all measured scalars before any prediction can reject this endpoint.
    geometryChecks.push(measured);
    if(JSON.stringify(measured.window_size)!==JSON.stringify(windows[phase]))throw Error('public window dimensions differ');
    if(JSON.stringify(measured.observed_grid)!==JSON.stringify(grids[phase]))throw Error('fixed measured grid differs');
    if(measured.terminal_capacity!==grids[phase][1])throw Error('fixed measured capacity differs');
}
function finish(status,error){
    if(finished)return;finished=true;
    const result={status,case_id:caseId,error:error||null,observations,window_rect_format:'qt-qrect-inclusive-ltrb',geometry_checks:geometryChecks,
        full_active_grid_verified:false,local_shell_transport_verified:false,presented_done:null};
    if(status==='failed'){
        result.failure_geometry=null;
        const phase=Math.min(requested||observations.length,lastStage);
        try{result.failure_geometry=geometrySample(phase,null,null);}catch(_){}
        // Independent window evidence survives a failing terminal text binding.
        if(result.failure_geometry){
            try{
                const peer=peerState(),scalar=value=>Number.isInteger(value)?value:null;
                if(peer){result.failure_geometry.observed_grid=[scalar(peer.columns),scalar(peer.rows)];
                    result.failure_geometry.event_count=Array.isArray(peer.naws_history)?peer.naws_history.length:null;}
            }catch(_){}
            try{const terminal=currentTerminal();result.failure_geometry.terminal_capacity=terminal&&Number.isInteger(terminal.capacity)?terminal.capacity:null;}catch(_){}
        }
    }
    write('resize-combined-result.json',JSON.stringify(result));send({kind:'resize_combined_result',...result});
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
send({kind:'probe_ready',station_isolated:true,scenario:'resize-combined-v1',case_id:caseId});
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
            if(peer.complete){if(observations.length!==phaseCount)throw Error('incomplete resize-combined observations');finish('observed-fixed-resize-combined-states',null);return;}
            const terminal=currentTerminal();if(!terminal||!terminal.connected||terminal.protocol!=='Telnet'){resetGrid();return;}
            if(stage===3){
                if(!stableGrid(peer,terminal))return;
                checkGeometry(0,peer,terminal);write('fixture-ready',JSON.stringify({grid:[peer.columns,peer.rows],event_count:peer.naws_history.length}));stage=4;return;
            }
            if(requested>committed&&peer.resize_armed===requested){
                if(resized!==requested){resize(main,...windows[requested]);resized=requested;resetGrid();return;}
                if(!stableGrid(peer,terminal))return;
                checkGeometry(requested,peer,terminal);
                write('resize-ready-'+requested,JSON.stringify({phase:requested,grid:[peer.columns,peer.rows],event_count:peer.naws_history.length}));committed=requested;return;
            }
            if(peer.phase===null||observations.length>peer.phase)return;
            if(peer.case_id!==caseId)throw Error('peer case differs');
            if(peer.phase_label!==contract.labels[peer.phase])throw Error('peer phase label differs');
            if(peer.phase!==lastPhase){lastPhase=peer.phase;stable=null;stableSince=Date.now();}
            const record=peer.records[peer.phase];if(!record.cursor_reply)return;
            if(terminal.capacity!==record.frozen_grid[1]||JSON.stringify(record.frozen_grid)!==JSON.stringify(grids[peer.phase]))throw Error('phase grid differs');
            if(geometryChecks.length===peer.phase)checkGeometry(peer.phase,peer,terminal);
            const geometry=windowGeometry(),state=JSON.stringify([terminal,geometry]);
            if(JSON.stringify(geometry.window_size)!==JSON.stringify(windows[peer.phase]))throw Error('phase window dimensions differ');
            if(state!==stable){stable=state;stableSince=Date.now();return;}
            if(Date.now()-stableSince<400)return;
            if(peer.phase===0){
                if(JSON.stringify(record.cursor_reply)!==JSON.stringify([19,7]))throw Error('initial fixture DSR differs');
                if(!Array.isArray(terminal.lines))throw Error('initial fixture text missing');
                for(const label of contract.marker_labels){
                    const count=terminal.lines.reduce((total,line)=>total+line.split(label).length-1,0);
                    if(count!==1)throw Error('initial fixture sentinel missing or repeated '+label);
                }
            }
            if(observations.length!==peer.phase||observations.length>=phaseCount)throw Error('resize-combined phase order');
            const observation={phase:peer.phase,label:peer.phase_label,terminal,...geometry,cursor_reply:record.cursor_reply,
                cursor_reply_private:record.cursor_reply_private,response_hex:record.response_hex,frozen_grid:record.frozen_grid};
            observations.push(observation);send({kind:'resize_combined_phase',...observation});
            write('ack-'+peer.phase,'observed');
            if(resizePhases.includes(peer.phase+1))requestResize(peer.phase+1);
        }
        due=Date.now()+75;
    }catch(error){finish('failed',String(error));}finally{busy=false;}
}});
setTimeout(()=>finish('failed','bounded observation timer'),42000);
