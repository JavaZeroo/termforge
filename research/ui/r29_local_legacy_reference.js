// Read WindTerm's local-shell document after each owned native fixture stage.
let stage=0,phase=0,busy=false,finished=false,due=0,stable=null,stableSince=0,sent=false,control=null;
let lastAdjustment=0;
const started=Date.now(),observations=[];
const windows=[[1228,839],[988,509]],grids=[[80,24],[60,12]],adjustments=[];
const directory=__RUN_DIRECTORY_JSON__;
const rename=fn(kernel,'MoveFileExW','bool',['pointer','pointer','uint32']);
function read(name){
    try{return JSON.parse(File.readAllText(directory+'/'+name));}catch(error){return null;}
}
function write(name,value){
    const destination=directory+'/'+name,temporary=destination+'.upload';
    const file=new File(temporary,'w');file.write(JSON.stringify(value));file.close();
    if(!rename(Memory.allocUtf16String(temporary),Memory.allocUtf16String(destination),0))
        throw Error('publish fixture phase '+name);
}
function document(){
    const text=evalText("JSON.stringify((function(){\n        var t=window.activeText;if(!t)return null;\n        function get(k){var v=typeof t[k]==='function'?t[k]():t[k];return v===undefined?null:v;}\n        var n=get('visibleLineCount'),lines=[],markers=[];\n        if(typeof n!=='number'||n<0||n>200)throw Error('document bound');\n        for(var i=0;i<n;i++){\n            var line=t.lineText(i);\n            if(typeof line!=='string'||line.length>256)throw Error('line bound');\n            lines.push(line);\n            var re=/RESIZED|[BCD]/g,m;\n            while((m=re.exec(line))!==null){\n                if(markers.length>=100)throw Error('marker bound');\n                markers.push({word:m[0],row:i,column:m.index,rect:t.visibleRectFromPosition(t.lineStart(i)+m.index)});\n            }\n        }\n        return {capacity:get('screenLineCapacity'),scroll:get('scrollTop'),line_count:n,lines:lines,markers:markers};\n    })())");
    return text?JSON.parse(text):null;
}
function finish(status,error){
    if(finished)return;finished=true;
    const result={status,error,observations,control,adjustments};write('local-legacy-result.json',result);
    send({kind:'local_legacy_result',...result});
}
function assertWinpty(){
    const module=Process.findModuleByName('winpty.dll');
    if(!module)return false;
    const normalized=p=>p.replace(/\\/g,'/').toLowerCase();
    if(normalized(module.path)!==normalized(directory+'/app/winpty.dll'))
        throw Error('WindTerm local transport differs');
    return true;
}
for(const module of [wind,core,gui,widgets]){
    const normalized=p=>p.replace(/\\/g,'/').toLowerCase();
    if(normalized(module.path)!==normalized(directory+'/app/'+module.name))
        throw Error('pinned Qt module path guard');
}
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(finished||busy||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;
    busy=true;due=Date.now()+75;
    try{
        if(wind.base.add(0xc29394).readU8()!==0)throw Error('first-run guard');
        if(Date.now()-started>29000)throw Error('bounded scenario deadline');
        if(stage===0){if(!ready('ui'))return;resize(main,...windows[0]);stage++;return;}
        if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);stage++;return;}
        if(stage===2){key(tree,0x1000004);stage++;return;}
        const childReady=!!read('ready.json'),winptyModule=assertWinpty(),nativeGrid=read('ui-native-grid.json')?.grid;
        const terminal=document(), target=phase===5?1:0;
        const window=rect(main),size=[window[2]-window[0]+1,window[3]-window[1]+1];
        control={phase,child_ready:childReady,winpty_module_verified:winptyModule,window:size,capacity:terminal?.capacity??null,native_grid:nativeGrid??null};
        if(!nativeGrid||!winptyModule)return;
        if(JSON.stringify(nativeGrid)!==JSON.stringify(grids[target])){
            if(Date.now()-lastAdjustment<600)return;
            const next=[size[0]+8*(grids[target][0]-nativeGrid[0]),size[1]+18*(grids[target][1]-nativeGrid[1])];
            if(adjustments.length>=8||next[0]<600||next[0]>1500||next[1]<400||next[1]>1100)
                throw Error('owned local geometry calibration exhausted');
            adjustments.push({phase,window:size,native_grid:nativeGrid,requested:next});
            lastAdjustment=Date.now();
            resize(main,...next);
            return;
        }
        if(!childReady)return;
        if(!sent){
            write('phase-'+phase+'-request.json',{phase});sent=true;return;
        }
        if(!terminal)return;
        const native=read('phase-'+phase+'.json');
        if(!native||!terminal.lines.some(line=>line.startsWith('S'+(phase+1))))return;
        const [l,t,r,b]=native.window;
        if(r-l+1!==grids[target][0]||b-t+1!==grids[target][1])throw Error('native viewport differs');
        const value=JSON.stringify(terminal);
        if(value!==stable){stable=value;stableSince=Date.now();return;}
        if(Date.now()-stableSince<300)return;
        observations.push({phase,window:size,native_window:native.window,terminal});
        phase++;sent=false;stable=null;
        if(phase===5){lastAdjustment=Date.now();resize(main,...windows[1]);}
        if(phase===6){write('ui-release.json',{observed:6});finish('observed',null);}
    }catch(error){finish('failed',String(error));}finally{busy=false;}
}});
setTimeout(()=>finish('failed','bounded observation timer'),30000);
