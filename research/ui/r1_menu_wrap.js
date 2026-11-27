// Original companion driver: menus receive input events; VT arrives over owned TCP.
const popupWidget=fn(widgets,'?activePopupWidget@QApplication@@SAPEAVQWidget@@XZ','pointer',[]);
const contextCtor=fn(gui,'??0QContextMenuEvent@@QEAA@W4Reason@0@AEBVQPoint@@1V?$QFlags@W4KeyboardModifier@Qt@@@@@Z','pointer',['pointer','int','pointer','pointer','int']);
const actionList=fn(widgets,'?actions@QWidget@@QEBA?AV?$QList@PEAVQAction@@@@XZ','pointer',['pointer','pointer']);
const actionText=fn(widgets,'?text@QAction@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
const actionEnabled=fn(widgets,'?isEnabled@QAction@@QEBA_NXZ','bool',['pointer']);
const actionMenu=fn(widgets,'?menu@QAction@@QEBAPEAVQMenu@@XZ','pointer',['pointer']);
const activeAction=fn(widgets,'?setActiveAction@QMenu@@QEAAXPEAVQAction@@@Z','void',['pointer','pointer']);
let stage=0,busy=false,finished=false,due=0;const started=Date.now(),CTRL=0x4000000,SHIFT=0x2000000;
function openContext(next){const p=viewPort(),local=Memory.alloc(8),global=Memory.alloc(8);local.writeS32(180);local.add(4).writeS32(10);mapGlobal(p,global,local);const event=eventAlloc(128);if(event.isNull())throw Error('event allocation failed');contextCtor(event,0,local,global,0);observationStep='post-context';postEvent(p,event,0);stage=next;due=Date.now()+500;busy=false;}
function menuActions(menu){const list=Memory.alloc(8);actionList(menu,list);const d=list.readPointer(),begin=d.add(8).readS32(),end=d.add(12).readS32();if(begin<0||end<begin||end>100)throw Error('menu bound');const result=[];for(let i=begin;i<end;i++){const p=d.add(16+8*i).readPointer();result.push({pointer:p,text:str(actionText,p),name:str(objectName,p),enabled:actionEnabled(p),submenu:!actionMenu(p).isNull()});}return result;}
function menuSnapshot(label){const menu=popupWidget();if(menu.isNull())throw Error('no context popup');const actions=menuActions(menu);send({kind:'menu',phase:label,menu_class:typeOf(menu),actions:actions.map(({pointer,...a})=>a)});return {menu,actions};}
function menuChoose(next,id,pattern){const {menu,actions}=menuSnapshot('before-activation');const action=actions.filter(x=>pattern.test(x.text.split('\t')[0].replace(/\(&.\)/g,'').replace(/&/g,'').replace(/…/g,'...').trim()));if(action.length!==1||!action[0].enabled)throw Error('menu choice guard');send({kind:'menu_activation',action:id,text:action[0].text,name:action[0].name,input:'QMenu active action plus queued Return key'});activeAction(menu,action[0].pointer);return modalKey(next,menu,0x1000004);}
send({kind:'probe_ready',station_isolated:true,scenario:'menu-wrap'});
Interceptor.attach(gui.getExportByName('?clipboard@QGuiApplication@@SAPEAVQClipboard@@XZ'),{onEnter(){if(Process.getCurrentThreadId()===uiThread)send({kind:'clipboard_entry',stage});}});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
    try{
        if(wind.base.add(0xc29394).readU8()!==0)throw Error('first run guard');
        if(Date.now()-started>__SCENARIO_TIMEOUT_MS__)throw Error('scenario timeout');
        if(stage===0){observationStep='ui-ready';if(!ready('ui'))return;sample('initial');observationStep='resize-main';resize(main,1360,860);}
        else if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);}
        else if(stage===2){key(tree,0x1000004);}
        else {
            const phase=Math.floor((stage-3)/2);
            if(phase===6){
                if((peerState()||{}).completed!==6)return;
                finished=true;send({kind:'scenario_complete',coverage:'bracketed-paste'});return;
            }
            scan();
            if((stage-3)%2===0){
                if(!editor || !engine || !evalText("String(window.activeText.lineText("+phase+"))").includes('TF-PHASE-'+phase))return;
                focus(editor,7);
                writeClipboard(phase%2===0 ? 'TF-PASTE' : 'A\r\nB\n中END');
                return modalKey(stage+1,editor,86,CTRL|SHIFT);
            }
            if(!modalWidget().isNull()){
                if(typeOf(modalWidget())!=='PasteDialog')throw Error('unexpected paste dialog');
                send({kind:'paste_confirmed',phase});
                return modalKey(stage,focused(),0x1000004);
            }
            const peer=peerState();
            if(!peer || !(peer.received_hex.split('7c').pop()||'').includes(phase%2===0?'54462d5041535445':'454e44'))return;
            key(editor,124,0,'|');
        }
        stage++;due=Date.now()+250;
    }catch(e){finished=true;send({kind:'failed',stage,error:String(e)});}finally{busy=false;}
}});
