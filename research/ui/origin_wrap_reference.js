// Direct original DSR and document observations; no clipboard, paint or private settings.
let stage=0,busy=false,finished=false,due=0,lastPhase=-1,stable=null,stableSince=0;
let readyGrid=null,readySince=0,readyNawsCount=0,readyNawsGrid=null;
const started=Date.now(),observations=[];
for(const module of [wind,core,gui,widgets]){
    const expected=(__RUN_DIRECTORY_JSON__+'/app/'+module.name).replace(/\\/g,'/').toLowerCase();
    if(module.path.replace(/\\/g,'/').toLowerCase()!==expected)throw Error('pinned Qt module path guard');
}
function currentTerminal(){
    const raw=evalText(`JSON.stringify((function(){var t=window.activeText;if(!t)return null;function get(k){try{var v=typeof t[k]==='function'?t[k]():t[k];return v===undefined?null:v;}catch(e){return null;}}var n=get('visibleLineCount'),lines=[];if(typeof n!=='number'||n<0||n>200)throw Error('document line bound');for(var i=0;i<n;i++){var line=t.lineText(i);if(typeof line!=='string'||line.length>1024)throw Error('document text bound');lines.push(line);}return {protocol:get('protocolName'),connected:get('isConnected'),top:get('topLine'),scroll:get('scrollTop'),line_count:n,capacity:get('screenLineCapacity'),lines:lines,caret_api:{caretLine:get('caretLine'),caretColumn:get('caretColumn'),caretPosition:get('caretPosition')},cell_styles:null,current_style:null};})())`);
    return raw?JSON.parse(raw):null;
}
function finish(status,error){
    if(finished)return;finished=true;
    const result={status,error:error||null,observations,full_active_grid_verified:false,terminal_style_values_verified:false,local_shell_transport_verified:false,presented_done:null};
    const file=new File(__RUN_DIRECTORY_JSON__+'/origin-wrap-result.json','w');file.write(JSON.stringify(result));file.close();
    send({kind:'origin_wrap_result',...result});
}
function resetReadiness(){readyGrid=null;readySince=0;}
function observeReadinessNaws(peer){
    const history=peer.naws_history;
    if(!Array.isArray(history)||history.length<readyNawsCount||history.length>64)throw Error('NAWS readiness history bound');
    for(let i=readyNawsCount;i<history.length;i++){
        const event=history[i];
        if(!Array.isArray(event)||event.length!==2||event.some(n=>!Number.isInteger(n)||n<0||n>65535))throw Error('NAWS readiness event bound');
        const grid=JSON.stringify(event);
        if(readyNawsGrid!==null&&grid!==readyNawsGrid)resetReadiness();
        readyNawsGrid=grid;
    }
    readyNawsCount=history.length;
}
send({kind:'probe_ready',station_isolated:true,scenario:'origin-wrap-margin-v1'});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
    try{
        if(wind.base.add(0xc29394).readU8()!==0)throw Error('first-run guard');
        if(Date.now()-started>41000)throw Error('bounded scenario deadline');
        if(stage===0){if(!ready('ui'))return;resize(main,1360,860);stage=1;}
        else if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);stage=2;}
        else if(stage===2){key(tree,0x1000004);stage=3;}
        else{
            const peer=peerState();if(!peer||!peer.connected){if(stage===3)resetReadiness();return;}
            if(peer.error)throw Error('peer '+peer.error);
            if(peer.complete){if(observations.length!==25)throw Error('incomplete observation sequence');finish('observed-all-origin-wrap-phases',null);return;}
            const terminal=currentTerminal();if(!terminal||!terminal.connected||terminal.protocol!=='Telnet'){if(stage===3)resetReadiness();return;}
            if(stage===3){
                observeReadinessNaws(peer);
                const grid=JSON.stringify([peer.columns,peer.rows,terminal.capacity]);
                if(!peer.columns||peer.rows!==terminal.capacity||JSON.stringify([peer.columns,peer.rows])!==readyNawsGrid){resetReadiness();return;}
                if(grid!==readyGrid){readyGrid=grid;readySince=Date.now();return;}
                if(Date.now()-readySince<600)return;
                const flag=new File(__RUN_DIRECTORY_JSON__+'/fixture-ready','w');flag.write('stable-NAWS-capacity');flag.close();
                stage=4;return;
            }
            if(peer.phase===null)return;
            if(terminal.capacity!==peer.frozen_grid[1])throw Error('terminal capacity changed after freeze');
            if(peer.phase!==lastPhase){lastPhase=peer.phase;stable=null;stableSince=Date.now();}
            const record=peer.records[peer.phase];if(!record.cursor_reply)return;
            const state=JSON.stringify(terminal);
            if(state!==stable){stable=state;stableSince=Date.now();return;}
            if(Date.now()-stableSince<400)return;
            if(observations.length!==peer.phase||observations.length>=25)throw Error('phase sequence bound');
            const observation={phase:peer.phase,label:peer.phase_label,terminal,cursor_reply:record.cursor_reply,cursor_reply_private:record.cursor_reply_private,response_hex:record.response_hex,frozen_grid:peer.frozen_grid};
            observations.push(observation);send({kind:'origin_wrap_phase',...observation});
            const ack=new File(__RUN_DIRECTORY_JSON__+'/ack-'+peer.phase,'w');ack.write('observed');ack.close();
            stableSince=Date.now()+100000;
        }
        due=Date.now()+75;
    }catch(e){finish('failed',String(e));}finally{busy=false;}
}});
setTimeout(()=>finish('failed','bounded observation timer'),42000);
