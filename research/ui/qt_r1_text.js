// Original bounded scenario for the pinned Qt5 / MSVC x64 WindTerm fixture.
// Every text read belongs to the newly created synthetic profile and peer.
'use strict';
const uiThread = __UI_THREAD_ID__, expectedStation = '__OWNED_STATION__';
const hostedCI = __HOSTED_CI__;
const widgets = Process.getModuleByName('Qt5Widgets.dll'), gui = Process.getModuleByName('Qt5Gui.dll');
const core = Process.getModuleByName('Qt5Core.dll'), wind = Process.getModuleByName('WindTerm.exe');
const user = Process.getModuleByName('user32.dll'), kernel = Process.getModuleByName('kernel32.dll');
const fn = (m,n,r,a) => new NativeFunction(m.getExportByName(n), r, a);
const station = fn(user,'GetProcessWindowStation','pointer',[])();
const name = Memory.alloc(512), needed = Memory.alloc(4);
if (!fn(user,'GetUserObjectInformationW','bool',['pointer','int','pointer','uint32','pointer'])(station,2,name,512,needed) || name.readUtf16String() !== expectedStation)
    throw Error('clipboard station guard failed');
const all = fn(widgets,'?allWidgets@QApplication@@SA?AV?$QList@PEAVQWidget@@@@XZ','pointer',['pointer']);
const geometry = fn(widgets,'?geometry@QWidget@@QEBAAEBVQRect@@XZ','pointer',['pointer']);
const parent = fn(widgets,'?parentWidget@QWidget@@QEBAPEAV1@XZ','pointer',['pointer']);
const visible = fn(widgets,'?isVisible@QWidget@@QEBA_NXZ','bool',['pointer']);
const cname = fn(core,'?className@QMetaObject@@QEBAPEBDXZ','pointer',['pointer']);
const objectName = fn(core,'?objectName@QObject@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
const constData = fn(core,'?constData@QString@@QEBAPEBVQChar@@XZ','pointer',['pointer']);
const destroyString = fn(core,'??1QString@@QEAA@XZ','void',['pointer']);
const stringCtor = fn(core,'??0QString@@QEAA@PEBVQChar@@H@Z','pointer',['pointer','pointer','int']);
const resize = new NativeFunction(widgets.getExportByName('?resize@QWidget@@QEAAXHH@Z'),'void',['pointer','int','int'],{exceptions:'propagate'});
const focus = fn(widgets,'?setFocus@QWidget@@QEAAXW4FocusReason@Qt@@@Z','void',['pointer','int']);
const focused = fn(widgets,'?focusWidget@QApplication@@SAPEAVQWidget@@XZ','pointer',[]);
const expandAll = fn(widgets,'?expandAll@QTreeView@@QEAAXXZ','void',['pointer']);
const keyCtor = fn(gui,'??0QKeyEvent@@QEAA@W4Type@QEvent@@HV?$QFlags@W4KeyboardModifier@Qt@@@@AEBVQString@@_NG@Z','pointer',['pointer','int','int','int','pointer','bool','uint16']);
const keyDestroy = fn(gui,'??1QKeyEvent@@UEAA@XZ','void',['pointer']);
const sendEvent = new NativeFunction(core.getExportByName('?sendEvent@QCoreApplication@@SA_NPEAVQObject@@PEAVQEvent@@@Z'),'bool',['pointer','pointer'],{exceptions:'propagate'});
const getClipboard = fn(gui,'?clipboard@QGuiApplication@@SAPEAVQClipboard@@XZ','pointer',[]);
let clipboard = null;
const clipText = fn(gui,'?text@QClipboard@@QEBA?AVQString@@W4Mode@1@@Z','pointer',['pointer','pointer','int']);
const clipSet = fn(gui,'?setText@QClipboard@@QEAAXAEBVQString@@W4Mode@1@@Z','void',['pointer','pointer','int']);
const lineText = fn(widgets,'?text@QLineEdit@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
const sliderValue = fn(widgets,'?value@QAbstractSlider@@QEBAHXZ','int',['pointer']);
const sliderMax = fn(widgets,'?maximum@QAbstractSlider@@QEBAHXZ','int',['pointer']);
const sliderSet = fn(widgets,'?setValue@QAbstractSlider@@QEAAXH@Z','void',['pointer','int']);
const sliderPage = fn(widgets,'?pageStep@QAbstractSlider@@QEBAHXZ','int',['pointer']);
const qml = Process.getModuleByName('Qt5Qml.dll');
const jsEngine = fn(qml,'?engine@QJSValue@@QEBAPEAVQJSEngine@@XZ','pointer',['pointer']);
const jsEvaluate = fn(qml,'?evaluate@QJSEngine@@QEAA?AVQJSValue@@AEBVQString@@0H@Z','pointer',['pointer','pointer','pointer','pointer','int']);
const jsText = fn(qml,'?toString@QJSValue@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
const jsDestroy = fn(qml,'??1QJSValue@@QEAA@XZ','void',['pointer']);
let engine = null, evaluating = false;
for(const ex of qml.enumerateExports().filter(e=>/^\?call.*@QJSValue@@/.test(e.name)))Interceptor.attach(ex.address,{onEnter(args){if(!evaluating&&Process.getCurrentThreadId()===uiThread){const e=jsEngine(args[0]);if(!e.isNull())engine=e;}}});
Interceptor.attach(qml.getExportByName('?evaluate@QJSEngine@@QEAA?AVQJSValue@@AEBVQString@@0H@Z'),{onEnter(args){if(!evaluating&&Process.getCurrentThreadId()===uiThread)engine=args[0];}});
function evalText(source){if(engine===null)return null;evaluating=true;try{return withString(source,s=>withString('TermForgeR1',file=>{const value=Memory.alloc(16);jsEvaluate(engine,value,s,file,1);try{return str(jsText,value);}finally{jsDestroy(value);}}));}finally{evaluating=false;}}
function textState(){return evalText(`JSON.stringify((function(){var t=window.activeText;if(!t)return null;function get(k){try{return typeof t[k]==='function'?t[k]():t[k];}catch(e){return String(e);}}return {top:get('topLine'),scroll:get('scrollTop'),lines:get('visibleLineCount'),capacity:get('screenLineCapacity'),caret:get('selectionCaretP'),anchor:get('selectionAnchorP'),selection:get('selectionText')};})())`);}
const inherits = fn(core,'?inherits@QObject@@QEBA_NPEBD@Z','bool',['pointer','pointer']);
const labelText = fn(widgets,'?text@QLabel@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
const buttonText = fn(widgets,'?text@QAbstractButton@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
const comboText = fn(widgets,'?currentText@QComboBox@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
const mouseCtor = fn(gui,'??0QMouseEvent@@QEAA@W4Type@QEvent@@AEBVQPointF@@W4MouseButton@Qt@@V?$QFlags@W4MouseButton@Qt@@@@V?$QFlags@W4KeyboardModifier@Qt@@@@@Z','pointer',['pointer','int','pointer','int','int','int']);
const mouseFullCtor = fn(gui,'??0QMouseEvent@@QEAA@W4Type@QEvent@@AEBVQPointF@@11W4MouseButton@Qt@@V?$QFlags@W4MouseButton@Qt@@@@V?$QFlags@W4KeyboardModifier@Qt@@@@@Z','pointer',['pointer','int','pointer','pointer','pointer','int','int','int']);
const mapGlobal = fn(widgets,'?mapToGlobal@QWidget@@QEBA?AVQPoint@@AEBV2@@Z','pointer',['pointer','pointer','pointer']);
const mouseDestroy = fn(gui,'??1QMouseEvent@@UEAA@XZ','void',['pointer']);
const eventTimestamp = fn(gui,'?setTimestamp@QInputEvent@@QEAAXK@Z','void',['pointer','uint32']);
const mouseLocal = fn(gui,'?localPos@QMouseEvent@@QEBAAEBVQPointF@@XZ','pointer',['pointer']);
const mouseScreen = fn(gui,'?screenPos@QMouseEvent@@QEBAAEBVQPointF@@XZ','pointer',['pointer']);
const keyboardModifiers = fn(gui,'?keyboardModifiers@QGuiApplication@@SA?AV?$QFlags@W4KeyboardModifier@Qt@@@@XZ','pointer',['pointer']);
function readModifiers(){const r=Memory.alloc(8);keyboardModifiers(r);return r.readS32();}
const openClip=fn(user,'OpenClipboard','bool',['pointer']),closeClip=fn(user,'CloseClipboard','bool',[]);
const clipData=fn(user,'GetClipboardData','pointer',['uint32']),globalLock=fn(kernel,'GlobalLock','pointer',['pointer']);
const globalUnlock=fn(kernel,'GlobalUnlock','bool',['pointer']),globalSize=fn(kernel,'GlobalSize','uint64',['pointer']);
const globalAlloc=fn(kernel,'GlobalAlloc','pointer',['uint32','uint64']),globalFree=fn(kernel,'GlobalFree','pointer',['pointer']);
const emptyClip=fn(user,'EmptyClipboard','bool',[]),setClip=fn(user,'SetClipboardData','pointer',['uint32','pointer']);
const winId=fn(widgets,'?winId@QWidget@@QEBA_KXZ','pointer',['pointer']);
const modalWidget=fn(widgets,'?activeModalWidget@QApplication@@SAPEAVQWidget@@XZ','pointer',[]);
const buttonDefault=fn(widgets,'?isDefault@QPushButton@@QEBA_NXZ','bool',['pointer']);
const buttonClick=fn(widgets,'?click@QAbstractButton@@QEAAXXZ','void',['pointer']);
const doubleClickInterval=fn(user,'GetDoubleClickTime','uint32',[])();
const inputTimestamp=fn(kernel,'GetTickCount','uint32',[]);
function rawClipboard(){if(!openClip(ptr(0)))throw Error('OpenClipboard failed');try{const h=clipData(13);if(h.isNull())return '';const n=Number(globalSize(h));if(n>65536)throw Error('clipboard bound');const p=globalLock(h);if(p.isNull())throw Error('GlobalLock failed');try{return p.readUtf16String(n/2).split('\u0000')[0];}finally{globalUnlock(h);}}finally{closeClip();}}
function writeClipboard(text){if(!hostedCI)throw Error('system clipboard write requires disposable hosted VM');if(!openClip(winId(main)))throw Error('OpenClipboard write failed');let h=ptr(0);try{if(!emptyClip())throw Error('EmptyClipboard failed');h=globalAlloc(2,(text.length+1)*2);if(h.isNull())throw Error('GlobalAlloc failed');const p=globalLock(h);if(p.isNull())throw Error('GlobalLock failed');p.writeUtf16String(text);globalUnlock(h);if(setClip(13,h).isNull())throw Error('SetClipboardData failed');h=ptr(0);}finally{if(!h.isNull())globalFree(h);closeClip();}send({kind:'clipboard_set',system_clipboard:rawClipboard()});}
function isA(p,name){return inherits(p,Memory.allocUtf8String(name));}
function str(call,self,...args) { const s=Memory.alloc(8); call(self,s,...args); try { const n=s.readPointer().add(4).readS32(); if(n<0||n>32768)throw Error('text bound');return constData(s).readUtf16String(n); }finally{destroyString(s);} }
function withString(value,call) {const s=Memory.alloc(8),p=Memory.allocUtf16String(value); stringCtor(s,p,value.length);try{return call(s);}finally{destroyString(s);}}
function rect(p){let r=geometry(p);return [r.readS32(),r.add(4).readS32(),r.add(8).readS32(),r.add(12).readS32()];}
function typeOf(p){if(p.isNull())return null;return cname(new NativeFunction(p.readPointer().readPointer(),'pointer',['pointer'])(p)).readUtf8String();}
let cached=[],main=null,tree=null,view=null,editor=null,scroll=null;
function scan(){
    const q=Memory.alloc(8);all(q); const d=q.readPointer(),b=d.add(8).readS32(),e=d.add(12).readS32();
    if(b<0||e<b||e>2500)throw Error('widget bound');
    cached=[];for(let i=b;i<e;i++){const p=d.add(16+i*8).readPointer();if(!visible(p))continue;
        const cl=typeOf(p), row={ptr:p.toString(),parent:parent(p).toString(),class:cl,name:str(objectName,p),rect:rect(p)};
        if(cl==='QLineEdit'||cl==='LineEdit')row.text=str(lineText,p);
        if(cl==='QPushButton'){row.text=str(buttonText,p);row.default=buttonDefault(p);}
        if(cl==='SciScrollbar'||cl==='QScrollBar')row.scroll={value:sliderValue(p),max:sliderMax(p),page:sliderPage(p)};
        cached.push(row);
    }
    const root=cached.filter(x=>x.class==='MainWindow'&&x.parent==='0x0');if(root.length!==1)throw Error('root guard');main=ptr(root[0].ptr);
    const pane=cached.find(x=>x.class==='SessionPane');const t=pane&&cached.find(x=>x.class==='TreeView'&&x.parent===pane.ptr);if(t)tree=ptr(t.ptr);
    const v=cached.find(x=>x.class==='TerminalView');if(v)view=ptr(v.ptr);
    const ed=cached.find(x=>x.class==='ScintillaEditBase');if(ed)editor=ptr(ed.ptr);
    const sc=cached.find(x=>x.class==='SciScrollbar');if(sc)scroll=ptr(sc.ptr);
    return cached;
}
function peerState(){try{return JSON.parse(File.readAllText(__RUN_DIRECTORY_JSON__+'/peer.json'));}catch(e){return null;}}
function sample(label){scan();send({kind:'snapshot',phase:label,focus:typeOf(focused()),modal:typeOf(modalWidget()),text_state:textState(),peer:peerState(),widgets:cached});}
function key(p,k,mods=0,text=''){withString(text,s=>{for(const t of [6,7]){const e=Memory.alloc(256);keyCtor(e,t,k,mods,s,0,1);try{sendEvent(p,e);}finally{keyDestroy(e);}}});if(mods){withString('',s=>{for(const mod of [0x1000020,0x1000021]){const e=Memory.alloc(256);keyCtor(e,7,mod,0,s,0,1);try{sendEvent(p,e);}finally{keyDestroy(e);}}});}}
function mouse(p,t,x,y,button=1,buttons=1){const pos=Memory.alloc(16);pos.writeDouble(x);pos.add(8).writeDouble(y);const ip=Memory.alloc(8),gp=Memory.alloc(16),sp=Memory.alloc(16);ip.writeS32(x);ip.add(4).writeS32(y);mapGlobal(p,gp,ip);sp.writeDouble(gp.readS32());sp.add(8).writeDouble(gp.add(4).readS32());const e=Memory.alloc(256);mouseFullCtor(e,t,pos,pos,sp,button,buttons,0);eventTimestamp(e,inputTimestamp());try{const local=mouseLocal(e),screen=mouseScreen(e);send({kind:'mouse_event',event_type:t,local:[local.readDouble(),local.add(8).readDouble()],screen:[screen.readDouble(),screen.add(8).readDouble()]});sendEvent(p,e);}finally{mouseDestroy(e);}}
function click(p,x,y){mouse(p,2,x,y);mouse(p,3,x,y,1,0);}
function viewPort(){return ptr(cached.find(x=>x.name==='qt_scrollarea_viewport'&&x.parent===editor.toString()).ptr);}
function dragPoints(){return JSON.parse(evalText('JSON.stringify({a:window.activeText.visibleRectFromPosition(window.activeText.lineStart(81)),b:window.activeText.visibleRectFromPosition(window.activeText.lineEnd(82))})'));}
function dragText(reverse){const r=dragPoints();const a=reverse?r.b:r.a,b=reverse?r.a:r.b,vp=viewPort();mouse(vp,2,a.x+1,a.y+8);mouse(vp,5,b.x+1,b.y+8,0,1);mouse(vp,3,b.x+1,b.y+8,1,0);}
const postEvent=fn(core,'?postEvent@QCoreApplication@@SAXPEAVQObject@@PEAVQEvent@@H@Z','void',['pointer','pointer','int']);
const eventAlloc=fn(Process.getModuleByName('ucrtbase.dll'),'malloc','pointer',['uint64']);
// This pinned Qt/MSVC distribution deletes posted events through the shared UCRT.
function queuedKey(p,k,mods=0,text=''){withString(text,s=>{for(const t of [6,7]){const e=eventAlloc(256);if(e.isNull())throw Error('event allocation failed');keyCtor(e,t,k,mods,s,0,1);postEvent(p,e,0);}});}
function modalKey(next,p,k,mods=0){stage=next;due=Date.now()+500;busy=false;return queuedKey(p,k,mods);}
const treeModel=fn(widgets,'?model@QAbstractItemView@@QEBAPEAVQAbstractItemModel@@XZ','pointer',['pointer']);
const indexCtor=fn(core,'??0QModelIndex@@QEAA@XZ','pointer',['pointer']);
const hasIndex=fn(core,'?hasIndex@QAbstractItemModel@@QEBA_NHHAEBVQModelIndex@@@Z','bool',['pointer','int','int','pointer']);
let readinessLast='',readinessTick=0,readySamples=0,observationStep='initializing';
const postThreadMessage=fn(user,'PostThreadMessageW','bool',['uint32','uint32','uint64','int64']);
// An owned WM_NULL heartbeat keeps idle/modal event loops observable without input.
setInterval(()=>{if(!finished)postThreadMessage(uiThread,0,0,0);},100);
function ready(kind){scan();let ok=false;const state=textState(),p=peerState();if(kind==='ui'){const invalid=Memory.alloc(32);indexCtor(invalid);ok=!!tree&&engine!==null&&!treeModel(tree).isNull()&&hasIndex(treeModel(tree),0,0,invalid);readySamples=ok?readySamples+1:0;ok=readySamples>=2;}else{const t=state&&JSON.parse(state);ok=!!editor&&!!p&&!!t&&typeof t.lines==='number'&&(kind==='shell'?t.lines>=80&&typeof p.received==='string':p.connected&&!!p.sent_sha256&&t.lines>=6);}const value=JSON.stringify({phase:kind,ready:!!ok,has_tree:!!tree,has_engine:engine!==null,has_editor:!!editor,peer_ready:!!p});if(value!==readinessLast||Date.now()-readinessTick>2000){send({kind:'readiness',...JSON.parse(value)});readinessLast=value;readinessTick=Date.now();}if(!ok)due=Date.now()+200;return ok;}
// Scenario driver (the menu/wrap companion reuses only the original helpers above).
let stage=0,busy=false,finished=false,due=0;const started=Date.now(),CTRL=0x4000000,SHIFT=0x2000000;
send({kind:'probe_ready',station_isolated:true,exports:gui.enumerateExports().filter(x=>/QMouseEvent.*QEAA/.test(x.name)).map(x=>x.name)});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
    try{
        if(wind.base.add(0xc29394).readU8()!==0)throw Error('first run guard');
        if(Date.now()-started>__SCENARIO_TIMEOUT_MS__)throw Error('scenario timeout');
        if(stage===0){observationStep='ui-ready';if(!ready('ui'))return;sample('initial');observationStep='resize-main';resize(main,1360,860);}
        else if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);}
        else if(stage===2){key(tree,0x1000004);}
        else if(stage===3){observationStep='shell-ready';if(!ready('shell'))return;sample('session');if(!editor)throw Error('no editor');focus(editor,7);key(editor,0x1000010,CTRL|SHIFT);key(editor,65,CTRL|SHIFT);}
        else if(stage===4){sample('select-all');key(editor,0x1000000);key(editor,70,CTRL|SHIFT);}
        else if(stage===5){sample('find-open');key(focused(),78,0,'needle');stage=50;due=Date.now()+500;return;}
        else if(stage===50){sample('find-typed');stage=6;due=Date.now()+500;key(focused(),0x1000004);return;}
        else if(stage===6){sample('find-needle-enter');key(focused(),0x1000032);}
        else if(stage===7){sample('find-f3');key(focused(),0x1000032);stage=52;due=Date.now()+500;return;}
        else if(stage===52){sample('find-f3-wrap');key(focused(),0x1000032,SHIFT);stage=8;due=Date.now()+500;return;}
        else if(stage===8){sample('find-shift-f3');key(focused(),0x1000000);}
        else if(stage===9){sample('find-escape');focus(editor,7);key(editor,0x1000010,CTRL|SHIFT);}
        else if(stage===10){sample('history-home');const f=new File(__RUN_DIRECTORY_JSON__+'/APPEND','w');f.write('append');f.close();}
        else if(stage===11){sample('history-after-output');key(editor,0x1000011,CTRL|SHIFT);}
        else if(stage===12){sample('history-end');key(editor,0x1000016,SHIFT);}
        else if(stage===13){sample('shift-page-up');key(editor,0x1000017,SHIFT);}
        else if(stage===14){sample('shift-page-down');key(editor,0x1000000);sliderSet(scroll,0);}
        else if(stage===15){sample('select-top');send({kind:'mouse_setup',mods:readModifiers(),hook:evalText('this.tfMouse=[]; window.activeText.mousePressed.connect(function(e){tfMouse.push({keys:e?Object.keys(e):[],value:String(e)});}); "hooked"')});const vp=viewPort();click(vp,180,10);mouse(vp,4,180,10);mouse(vp,3,180,10,1,0);}
        else if(stage===16){sample('doubleclick-word');sliderSet(scroll,0);const vp=viewPort();click(vp,180,10);mouse(vp,4,180,10);mouse(vp,3,180,10,1,0);click(vp,180,10);}
        else if(stage===17){sample('tripleclick-line');send({kind:'mouse_target',value:evalText('JSON.stringify(tfMouse)')});key(editor,0x1000010,CTRL|SHIFT);}
        else if(stage===18){sample('before-typing');key(editor,88,0,'x');}
        else if(stage===19){sample('after-typing');send({kind:'text_projection',value:evalText('JSON.stringify({sep:window.activeText.wordSeparators(),line80:window.activeText.lineText(80),line81:window.activeText.lineText(81),line82:window.activeText.lineText(82),rect:window.activeText.visibleRectFromPosition(21)})')});send({kind:'action',name:'select-word-api',result:evalText('window.activeText.gotoPosition(5); window.activeText.selectWord(); "done"')});}
        else if(stage===20){sample('select-word-api');send({kind:'action',name:'select-line-api',result:evalText('window.activeText.selectLine(); "done"')});}
        else if(stage===21){sample('select-line-api');focus(editor,7);key(editor,70,CTRL|SHIFT);key(focused(),65,CTRL);key(focused(),0,0,'中文');key(focused(),0x1000004);}
        else if(stage===22){sample('find-unicode');key(focused(),0x1000000);key(editor,0x1000000);sliderSet(scroll,80);}
        else if(stage===23){sample('before-drag');dragText(false);}
        else if(stage===24){sample('drag-unicode');click(viewPort(),10,10);stage=51;due=Date.now()+doubleClickInterval+150;return;}
        else if(stage===51){sample('before-reverse-drag');const r=dragPoints();mouse(viewPort(),2,r.b.x+1,r.b.y+8);stage=53;due=Date.now()+200;return;}
        else if(stage===53){const r=dragPoints();mouse(viewPort(),5,r.a.x+1,r.a.y+8,0,1);stage=54;due=Date.now()+200;return;}
        else if(stage===54){const r=dragPoints();mouse(viewPort(),3,r.a.x+1,r.a.y+8,1,0);stage=25;due=Date.now()+500;return;}
        else if(stage===25){sample('reverse-drag-unicode');send({kind:'text_api_guard',value:evalText('JSON.stringify({connected:window.activeText.isConnected(),protocol:window.activeText.protocolName(),mode:window.activeText.keyMode})')});if(!hostedCI){finished=true;send({kind:'scenario_complete',coverage:'navigation-selection-only',clipboard:'requires-disposable-hosted-vm'});return;}key(editor,67,CTRL|SHIFT);}
        else if(stage===26){send({kind:'copy-system',system_clipboard:rawClipboard()});writeClipboard('TF-SINGLE');return modalKey(27,editor,86,CTRL|SHIFT);}
        else if(stage===27){sample('paste-single');return modalKey(28,focused(),0x1000000);}
        else if(stage===28){sample('paste-single-cancel');writeClipboard('TF-FIRST\r\nTF-SECOND\nTF-THIRD');return modalKey(29,editor,0x1000006,SHIFT);}
        else if(stage===29){sample('paste-multiline');return modalKey(30,focused(),0x1000000);}
        else if(stage===30){sample('paste-multiline-cancel');focus(editor,7);return modalKey(31,editor,86,CTRL|SHIFT);}
        else if(stage===31){sample('paste-multiline-confirm');const buttons=cached.filter(x=>x.class==='QPushButton'&&x.default);if(buttons.length!==1)throw Error('expected one default confirmation button');send({kind:'confirmation_action',text:buttons[0].text});stage=32;due=Date.now()+600;busy=false;buttonClick(ptr(buttons[0].ptr));return;}
        else if(stage===32){sample('paste-multiline-accepted');send({kind:'scenario_complete',coverage:'navigation-selection-system-clipboard-paste',system_clipboard:rawClipboard()});finished=true;}
        stage++;due=Date.now()+(stage===3?1800:((stage===16||stage===24)?doubleClickInterval+150:450));
    }catch(error){finished=true;send({error:String(error),stack:error.stack,stage,operation:observationStep});}finally{busy=false;}
}});
