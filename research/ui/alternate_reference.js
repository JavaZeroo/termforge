// Original UI-state observation only. No clipboard, user settings or physical input.
let stage=0,busy=false,finished=false,due=0,lastPhase=-1,stable=null,stableSince=0;
const started=Date.now(),observations=[];
function currentTerminal(){
    const raw=evalText(`JSON.stringify((function(){var t=window.activeText;if(!t)return null;function get(k){try{var v=typeof t[k]==='function'?t[k]():t[k];return v===undefined?null:v;}catch(e){return null;}}var n=get('visibleLineCount');var lines=[];if(typeof n==='number'&&n>=0&&n<=200){for(var i=0;i<n;i++)lines.push(t.lineText(i));}return {protocol:get('protocolName'),connected:get('isConnected'),top:get('topLine'),scroll:get('scrollTop'),line_count:n,capacity:get('screenLineCapacity'),lines:lines,caret_api:{caretLine:get('caretLine'),caretColumn:get('caretColumn'),caretPosition:get('caretPosition'),selectionCaretP:get('selectionCaretP')},cell_styles:null,current_style:null,api_names:Object.keys(t).filter(function(k){return /style|colou?r|attribute|caret|cursor|line.*count|screen/i.test(k);}).slice(0,180)};})())`);
    return raw?JSON.parse(raw):null;
}
function finish(status,error){
    if(finished)return;finished=true;
    const result={status,error:error||null,observations,terminal_style_values_verified:false,local_shell_transport_verified:false,presented_done:null};
    const file=new File(__RUN_DIRECTORY_JSON__+'/probe-result.json','w');file.write(JSON.stringify(result));file.close();send({kind:'alternate_reference_result',...result});
}
send({kind:'probe_ready',station_isolated:true,scenario:'alternate-reference-loopback'});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
    try{
        if(wind.base.add(0xc29394).readU8()!==0)throw Error('first-run guard');
        if(Date.now()-started>41000)throw Error('bounded scenario deadline');
        if(stage===0){if(!ready('ui'))return;resize(main,1360,860);stage=1;}
        else if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);stage=2;}
        else if(stage===2){key(tree,0x1000004);stage=3;}
        else{
            const peer=peerState();if(!peer||!peer.connected||peer.phase===null)return;
            if(peer.complete){finish('observed-all-phases',null);return;}
            if(peer.error)throw Error('peer '+peer.error);
            if(peer.phase!==lastPhase){lastPhase=peer.phase;stable=null;stableSince=Date.now();}
            const terminal=currentTerminal();if(!terminal||!terminal.connected||terminal.protocol!=='Telnet')return;
            const state=JSON.stringify(terminal);
            if(state!==stable){stable=state;stableSince=Date.now();return;}
            const record=peer.records[peer.phase];
            if(Date.now()-stableSince<450||(!record.cursor_reply&&Date.now()-stableSince<900))return;
            const observation={phase:peer.phase,label:peer.phase_label,terminal,cursor_reply:record.cursor_reply};
            observations.push(observation);send({kind:'alternate_phase',...observation});
            const ack=new File(__RUN_DIRECTORY_JSON__+'/ack-'+peer.phase,'w');ack.write('observed');ack.close();
            stableSince=Date.now()+100000;
        }
        due=Date.now()+75;
    }catch(e){finish('failed',String(e));}finally{busy=false;}
}});
setTimeout(()=>finish('failed','bounded observation timer'),42000);
