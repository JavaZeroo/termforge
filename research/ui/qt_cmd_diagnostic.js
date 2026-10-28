// Original fixed-Qt5/MSVC probe. Reads only the new synthetic local document.
// Controlled tree-key replay and bounded text reads are correctness diagnostics.
'use strict';
const uiThread = __UI_THREAD_ID__, runId = __RUN_ID_JSON__, directory = __RUN_DIRECTORY_JSON__;
const desktopName = __DESKTOP_JSON__, timeoutMs = __TIMEOUT_MS__;
const widgets = Process.getModuleByName('Qt5Widgets.dll'), gui = Process.getModuleByName('Qt5Gui.dll');
const core = Process.getModuleByName('Qt5Core.dll'), qml = Process.getModuleByName('Qt5Qml.dll');
const wind = Process.getModuleByName('WindTerm.exe'), user = Process.getModuleByName('user32.dll');
const fn = (m,n,r,a) => new NativeFunction(m.getExportByName(n),r,a);
const currentDesktop = fn(user,'GetThreadDesktop','pointer',['uint32'])(uiThread);
const desktopBuffer = Memory.alloc(512), needed = Memory.alloc(4);
if(!fn(user,'GetUserObjectInformationW','bool',['pointer','int','pointer','uint32','pointer'])(currentDesktop,2,desktopBuffer,512,needed) || desktopBuffer.readUtf16String()!==desktopName)
    throw Error('owned non-input desktop identity mismatch');
const all = fn(widgets,'?allWidgets@QApplication@@SA?AV?$QList@PEAVQWidget@@@@XZ','pointer',['pointer']);
const geometry = fn(widgets,'?geometry@QWidget@@QEBAAEBVQRect@@XZ','pointer',['pointer']);
const parent = fn(widgets,'?parentWidget@QWidget@@QEBAPEAV1@XZ','pointer',['pointer']);
const visible = fn(widgets,'?isVisible@QWidget@@QEBA_NXZ','bool',['pointer']);
const className = fn(core,'?className@QMetaObject@@QEBAPEBDXZ','pointer',['pointer']);
const constData = fn(core,'?constData@QString@@QEBAPEBVQChar@@XZ','pointer',['pointer']);
const destroyString = fn(core,'??1QString@@QEAA@XZ','void',['pointer']);
const stringCtor = fn(core,'??0QString@@QEAA@PEBVQChar@@H@Z','pointer',['pointer','pointer','int']);
const resize = fn(widgets,'?resize@QWidget@@QEAAXHH@Z','void',['pointer','int','int']);
const expandAll = fn(widgets,'?expandAll@QTreeView@@QEAAXXZ','void',['pointer']);
const focus = fn(widgets,'?setFocus@QWidget@@QEAAXW4FocusReason@Qt@@@Z','void',['pointer','int']);
const keyCtor = fn(gui,'??0QKeyEvent@@QEAA@W4Type@QEvent@@HV?$QFlags@W4KeyboardModifier@Qt@@@@AEBVQString@@_NG@Z','pointer',['pointer','int','int','int','pointer','bool','uint16']);
const keyDestroy = fn(gui,'??1QKeyEvent@@UEAA@XZ','void',['pointer']);
const sendEvent = fn(core,'?sendEvent@QCoreApplication@@SA_NPEAVQObject@@PEAVQEvent@@@Z','bool',['pointer','pointer']);
const treeModel = fn(widgets,'?model@QAbstractItemView@@QEBAPEAVQAbstractItemModel@@XZ','pointer',['pointer']);
const indexCtor = fn(core,'??0QModelIndex@@QEAA@XZ','pointer',['pointer']);
const hasIndex = fn(core,'?hasIndex@QAbstractItemModel@@QEBA_NHHAEBVQModelIndex@@@Z','bool',['pointer','int','int','pointer']);
const font = fn(widgets,'?font@QWidget@@QEBAAEBVQFont@@XZ','pointer',['pointer']);
const fontFamily = fn(gui,'?family@QFont@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
const pointSize = fn(gui,'?pointSizeF@QFont@@QEBANXZ','double',['pointer']);
const pixelSize = fn(gui,'?pixelSize@QFont@@QEBAHXZ','int',['pointer']);
const jsEngine = fn(qml,'?engine@QJSValue@@QEBAPEAVQJSEngine@@XZ','pointer',['pointer']);
const jsEvaluate = fn(qml,'?evaluate@QJSEngine@@QEAA?AVQJSValue@@AEBVQString@@0H@Z','pointer',['pointer','pointer','pointer','pointer','int']);
const jsText = fn(qml,'?toString@QJSValue@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
const jsDestroy = fn(qml,'??1QJSValue@@QEAA@XZ','void',['pointer']);
const emit = value => send({run_id:runId,...value});
function str(call,self,...args){const s=Memory.alloc(8);call(self,s,...args);try{const n=s.readPointer().add(4).readS32();if(n<0||n>32768)throw Error('QString bound');return constData(s).readUtf16String(n);}finally{destroyString(s);}}
function withString(value,call){const s=Memory.alloc(8);stringCtor(s,Memory.allocUtf16String(value),value.length);try{return call(s);}finally{destroyString(s);}}
let engine=null,evaluating=false;
for(const ex of qml.enumerateExports().filter(e=>/^\?call.*@QJSValue@@/.test(e.name)))Interceptor.attach(ex.address,{onEnter(args){if(!evaluating&&Process.getCurrentThreadId()===uiThread){const e=jsEngine(args[0]);if(!e.isNull())engine=e;}}});
Interceptor.attach(qml.getExportByName('?evaluate@QJSEngine@@QEAA?AVQJSValue@@AEBVQString@@0H@Z'),{onEnter(args){if(!evaluating&&Process.getCurrentThreadId()===uiThread)engine=args[0];}});
function evaluate(source){if(engine===null)return null;evaluating=true;try{return withString(source,s=>withString('TermForgeCmdDiagnostic',file=>{const value=Memory.alloc(16);jsEvaluate(engine,value,s,file,1);try{return str(jsText,value);}finally{jsDestroy(value);}}));}finally{evaluating=false;}}
let main=null,tree=null,editor=null,scanCount=0;
function scan(){
    // A bounded number of Qt QList return allocations live until Job teardown.
    if(++scanCount>8)throw Error('widget scan bound');
    const result=Memory.alloc(8);all(result);const d=result.readPointer(),b=d.add(8).readS32(),e=d.add(12).readS32();
    if(b<0||e<b||e>2500)throw Error('widget list bound');
    const rows=[];
    for(let i=b;i<e;i++){const p=d.add(16+i*8).readPointer();if(!visible(p))continue;const meta=new NativeFunction(p.readPointer().readPointer(),'pointer',['pointer'])(p);rows.push({p,class:className(meta).readUtf8String(),parent:parent(p).toString()});}
    const roots=rows.filter(x=>x.class==='MainWindow'&&x.parent==='0x0');if(roots.length!==1)throw Error('one main window required');main=roots[0].p;
    const panes=rows.filter(x=>x.class==='SessionPane');const trees=rows.filter(x=>x.class==='TreeView'&&panes.some(p=>p.p.toString()===x.parent));if(trees.length!==1)throw Error('one synthetic tree required');tree=trees[0].p;
    const editors=rows.filter(x=>x.class==='ScintillaEditBase');if(editors.length>1||rows.some(x=>x.class==='MessageBar'))throw Error('unexpected terminal state');editor=editors.length===1?editors[0].p:null;
}
function key(p,k){withString('',s=>{for(const t of [6,7]){const event=Memory.alloc(256);keyCtor(event,t,k,0,s,0,1);try{sendEvent(p,event);}finally{keyDestroy(event);}}});}
function state(){
    const value=evaluate(`JSON.stringify((function(){var t=window.activeText;if(!t)return null;var n=t.visibleLineCount;if(typeof n==='function')n=n.call(t);if(typeof n!=='number'||n<0||n>128)throw Error('logical line count bound');var rows=[];for(var i=0;i<n;i++)rows.push(t.lineText(i));return {line_count:n,lines:rows,screen_line_capacity:t.screenLineCapacity};})())`);
    if(value===null||value==='null')return null;
    return JSON.parse(value);
}
let stage=0,busy=false,finished=false,due=0,modelReady=0;const started=Date.now();
const postThreadMessage=fn(user,'PostThreadMessageW','bool',['uint32','uint32','uint64','int64']);
setInterval(()=>{if(!finished)postThreadMessage(uiThread,0,0,0);},100);
emit({kind:'probe_ready',desktop_verified:true,clipboard_access:false});
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
            let producer=null;try{producer=JSON.parse(File.readAllText(directory+'/producer.json'));}catch(e){due=Date.now()+200;return;}
            if(producer.run_id!==runId||producer.status!=='complete')throw Error('producer identity or completion');
            const snapshot=state();if(snapshot===null){due=Date.now()+200;return;}
            if(!Array.isArray(snapshot.lines)||!snapshot.lines.includes('TF-CMD-END '+runId)){due=Date.now()+200;return;}
            const r=geometry(editor),f=font(editor);
            emit({kind:'text_complete',...snapshot,widget_geometry:[r.readS32(),r.add(4).readS32(),r.add(8).readS32(),r.add(12).readS32()],widget_font:{family:str(fontFamily,f),point_size:pointSize(f),pixel_size:pixelSize(f)}});
            finished=true;
        }
        due=Date.now()+250;
    }catch(error){finished=true;emit({error:String(error),stage});}finally{busy=false;}
}});
