// Bounded companion: shared helpers and menu helpers precede this driver.
if(!hostedCI)throw Error('action-context requires disposable hosted VM');
const comboEdit=fn(widgets,'?lineEdit@QComboBox@@QEBAPEAVQLineEdit@@XZ','pointer',['pointer']);
const selectedText=fn(widgets,'?selectedText@QLineEdit@@QEBA?AVQString@@XZ','pointer',['pointer','pointer']);
function findEdit(){const p=focused();if(p.isNull()||!isA(p,'QComboBox'))throw Error('find focus guard');const e=comboEdit(p);if(e.isNull())throw Error('find edit guard');return e;}
function findSample(phase){const e=findEdit();sample(phase);send({kind:'find_edit',phase,text:str(lineText,e),selected:str(selectedText,e),system_clipboard:rawClipboard()});}
function action(id){send({kind:'context_action',action:id,input:'Qt key press/release'});}
send({kind:'probe_ready',station_isolated:true,scenario:'action-context'});
Interceptor.attach(user.getExportByName('DispatchMessageW'),{onEnter(){
    if(busy||finished||Process.getCurrentThreadId()!==uiThread||Date.now()<due)return;busy=true;
    try{
        if(wind.base.add(0xc29394).readU8()!==0)throw Error('first run guard');
        if(Date.now()-started>__SCENARIO_TIMEOUT_MS__)throw Error('scenario timeout');
        if(stage===0){if(!ready('ui'))return;resize(main,1360,860);}
        else if(stage===1){scan();expandAll(tree);focus(tree,7);key(tree,0x1000011);}
        else if(stage===2){key(tree,0x1000004);}
        else if(stage===3){if(!ready('vt'))return;sample('context-session');focus(editor,7);action('terminal-find');key(editor,70,CTRL|SHIFT);}
        else if(stage===4){findEdit();writeClipboard('TF-FIND-SENTINEL');key(focused(),0,0,'LEFTRIGHT');}
        else if(stage===5){findSample('find-typed');action('edit-all');key(focused(),65,CTRL);}
        else if(stage===6){findSample('find-all');action('edit-copy');key(focused(),67,CTRL);}
        else if(stage===7){findSample('find-copy');writeClipboard('中文');action('edit-paste');key(focused(),86,CTRL);}
        else if(stage===8){findSample('find-paste');action('edit-find');key(focused(),70,CTRL|SHIFT);}
        else if(stage===9){findSample('find-refocus');action('edit-escape');key(focused(),0x1000000);}
        else if(stage===10){sample('find-return');if(typeOf(focused())!=='ScintillaEditBase')throw Error('terminal return guard');action('terminal-type');key(focused(),0,0,'x');}
        else if(stage===11){sample('paste-before');writeClipboard('TF-FIRST\r\nTF-SECOND\nTF-THIRD');return openContext(12);}
        else if(stage===12){return menuChoose(13,'paste',/^(Paste|粘贴)$/i);}
        else if(stage===13){sample('paste-confirm');if(typeOf(modalWidget())!=='PasteDialog')throw Error('paste dialog guard');const p=focused();if(p.isNull())throw Error('paste focus guard');send({kind:'confirmation_action',text:'queued Return to actual focused widget',focus:typeOf(p)});return modalKey(14,p,0x1000004);}
        else if(stage===14){sample('paste-enter');}
        else if(stage===15){sample('paste-settled');send({kind:'scenario_complete',coverage:'action-context'});finished=true;return;}
        stage++;due=Date.now()+500;
    }catch(error){send({error:String(error),stage});finished=true;}finally{busy=false;}
}});
