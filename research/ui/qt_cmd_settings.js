// New #133 driver appended to the fixed #119 helper prefix; never patch its globals.
function scalar(object,key){
    try{if(object===null||object===undefined)return {status:'owner-unavailable',type:'undefined',value:null};var value=object[key],type=value===null?'null':typeof value;
        if(type==='undefined')return {status:'unavailable',type:type,value:null};
        var expected=key==='text.fontFamily1'?'string':key==='window.autoAdjustRowsAndColumns'||key==='terminal.autoComplete.enabled'?'boolean':'number';
        if(type==='null')return {status:'read',type:type,value:null};
        if(type==='number'||type==='boolean'||type==='string'){
            if(type!==expected)return {status:'invalid-type',type:type,value:null};
            if(type==='number'&&(!isFinite(value)||Math.abs(value)>10000000))return {status:'invalid-scalar',type:type,value:null};
            if(type==='string'&&(value.length>128||/[\\/\r\n]/.test(value)))return {status:'invalid-scalar',type:type,value:null};
            return {status:'read',type:type,value:value};
        }
        return {status:'non-scalar',type:type,value:null};
    }catch(error){return {status:'read-threw',type:'unknown',value:null};}
}
function discover(object){
    var whitelist=['windowRows','windowColumns','screenLineCapacity','settings','config','configuration','getSetting','setting','value','scrollbackLimit','fontFamily1','fontSize','autoCompleteEnabled'];
    var hits=[],count=0,truncated=false;
    try{if(object===null||object===undefined)return {status:'owner-unavailable',scanned:0,truncated:false,hits:[]};
        for(var key in object){if(++count>128){truncated=true;count=128;break;}if(whitelist.indexOf(key)>=0)hits.push(key);}
        return {status:'enumerated-names-only',scanned:count,truncated:truncated,hits:hits};
    }catch(error){return {status:'enumeration-threw',scanned:count,truncated:truncated,hits:hits};}
}
function state(){
    const value=evaluate(`JSON.stringify((function(){var t=window.activeText;if(!t)return null;var n=t.visibleLineCount;if(typeof n==='function')n=n.call(t);if(typeof n!=='number'||n<0||n>128)throw Error('logical line count bound');var rows=[];for(var i=0;i<n;i++)rows.push(t.lineText(i));
        ${scalar.toString()}${discover.toString()}
        var root=typeof terminal==='undefined'?null:terminal,view=null,lookup=[];
        for(var i=0;i<2;i++){var method=i===0?'activeView':'view';try{if(root&&typeof root[method]==='function'){var candidate=root[method]();lookup.push({method:method,type:candidate===null?'null':typeof candidate});if(candidate!==null&&typeof candidate==='object'){view=candidate;break;}}else lookup.push({method:method,type:'unavailable'});}catch(error){lookup.push({method:method,type:'threw'});}}
        var globalSettings=typeof settings==='undefined'?null:settings,owners=[['active_text',t],['active_view',view],['terminal',root],['global_settings',globalSettings]],reads=[],discovery=[];
        var keys=['window.autoAdjustRowsAndColumns','window.columns','window.rows','window.scrollbackLimit','text.fontFamily1','text.fontSize','terminal.autoComplete.enabled'];
        for(var j=0;j<owners.length;j++){var name=owners[j][0],object=owners[j][1];discovery.push({owner:name,result:discover(object)});for(var k=0;k<keys.length;k++)reads.push({owner:name,key:keys[k],result:scalar(object,keys[k])});}
        return {line_count:n,lines:rows,screen_line_capacity:t.screenLineCapacity,view_lookup:lookup,view_metrics:{window_rows:scalar(view,'windowRows'),window_columns:scalar(view,'windowColumns')},settings_reads:reads,discovery:discovery};})())`);
    return value===null||value==='null'?null:JSON.parse(value);
}
function modulePathMatches(actual,name){return actual.replace(/\\/g,'/').toLowerCase()===(directory+'/app/'+name).toLowerCase();}
const moduleGuards=[];
for(const module of [wind,core,gui,widgets,qml]){if(!modulePathMatches(module.path,module.name))throw Error('pinned module path mismatch');moduleGuards.push({name:module.name,path_guard_passed:true});}
let stage=0,busy=false,finished=false,due=0,modelReady=0;const started=Date.now();
const postThreadMessage=fn(user,'PostThreadMessageW','bool',['uint32','uint32','uint64','int64']);
setInterval(()=>{if(!finished)postThreadMessage(uiThread,0,0,0);},100);
emit({kind:'probe_ready',desktop_verified:true,clipboard_access:false,module_guards:moduleGuards});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
    try{
        if(wind.base.add(0xc29394).readU8()!==0)throw Error('first-run import guard');
        if(Date.now()-started>timeoutMs)throw Error('bounded diagnostic timeout');
        if(stage===0){scan();resize(main,1360,860);stage=1;}
        else if(stage===1){const invalid=Memory.alloc(32);indexCtor(invalid);const ready=engine!==null&&!treeModel(tree).isNull()&&hasIndex(treeModel(tree),0,0,invalid);modelReady=ready?modelReady+1:0;if(modelReady>=2){expandAll(tree);focus(tree,7);key(tree,0x1000011);stage=2;}}
        else if(stage===2){key(tree,0x1000004);emit({kind:'local_session_open_requested'});stage=3;due=Date.now()+1500;return;}
        else if(stage===3){scan();if(editor!==null)stage=4;}
        else if(stage===4){
            let producer=null;try{producer=JSON.parse(File.readAllText(directory+'/producer.json'));}catch(error){due=Date.now()+200;return;}
            if(producer.run_id!==runId||producer.status!=='complete')throw Error('producer identity or completion');
            const snapshot=state();if(snapshot===null||!Array.isArray(snapshot.lines)||!snapshot.lines.includes('TF-CMD-END '+runId)){due=Date.now()+200;return;}
            const rectangle=geometry(editor),face=font(editor);
            emit({kind:'settings_complete',...snapshot,module_guards:moduleGuards,widget_geometry:[rectangle.readS32(),rectangle.add(4).readS32(),rectangle.add(8).readS32(),rectangle.add(12).readS32()],widget_font:{family:str(fontFamily,face),point_size:pointSize(face),pixel_size:pixelSize(face)}});finished=true;
        }
        due=Date.now()+250;
    }catch(error){finished=true;emit({error:'bounded-settings-observation-failed',stage});}finally{busy=false;}
}});
