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
function menuChoose(next,pattern){const {menu,actions}=menuSnapshot('before-activation');const action=actions.filter(x=>pattern.test(x.text.split('\t')[0].replace(/\(&.\)/g,'').replace(/&/g,'').replace(/…/g,'...').trim()));if(action.length!==1||!action[0].enabled)throw Error('menu choice guard');send({kind:'menu_activation',text:action[0].text,name:action[0].name,input:'QMenu active action plus queued Return key'});activeAction(menu,action[0].pointer);return modalKey(next,menu,0x1000004);}
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
        else if(stage===3){observationStep='vt-ready';if(!ready('vt'))return;sample('vt-session');send({kind:'vt_projection',value:evalText('JSON.stringify({protocol:window.activeText.protocolName(),connected:window.activeText.isConnected(),line0:window.activeText.lineText(0),line1:window.activeText.lineText(1),line2:window.activeText.lineText(2),line3:window.activeText.lineText(3),line4:window.activeText.lineText(4),line5:window.activeText.lineText(5)})')});focus(editor,7);key(editor,65,CTRL|SHIFT);}
        else if(stage===4){sample('vt-select-all');key(editor,0x1000000);key(editor,70,CTRL|SHIFT);key(focused(),0,0,'LEFTRIGHT');}
        else if(stage===5){sample('vt-soft-search');key(focused(),0x1000032);}
        else if(stage===6){sample('vt-soft-search-wrap');key(focused(),0x1000000);key(editor,0x1000000);evalText('window.activeText.gotoPosition(5); "selection setup"');}
        else if(stage===7){sample('before-context');return openContext(8);}
        else if(stage===8){menuSnapshot('context-open');return menuChoose(9,/^(?:Select All|全选)$/i);}
        else if(stage===9){sample('menu-select-all');evalText('window.activeText.gotoPosition(5); "selection setup"');return openContext(10);}
        else if(stage===10){return menuChoose(11,/^(?:Find|查找)(?:\.\.\.)?$/i);}
        else if(stage===11){sample('menu-find-open');key(focused(),65,CTRL);key(focused(),0,0,'LEFTRIGHT');}
        else if(stage===12){sample('menu-find-result');key(focused(),0x1000000);finished=true;send({kind:'scenario_complete',coverage:'menu-wrap-only',clipboard:'requires-disposable-hosted-vm'});return;}
        stage++;due=Date.now()+(stage===3?2500:500);
    }catch(error){finished=true;send({error:String(error),stage,operation:observationStep,stack:error.stack});}finally{busy=false;}
}});
