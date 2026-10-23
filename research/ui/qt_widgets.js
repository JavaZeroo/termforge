// Original bounded observer for pinned WindTerm 2.7.0 Windows x64 / Qt 5.
// launch.py supplies the owned main thread ID. Attach only to its printed PID.
// Two samples: visible QWidget graph, then a controlled 1360 x 860 resize.
// All Qt calls occur on the GUI thread. No text, connections or credentials read.
'use strict';
const uiThread = __UI_THREAD_ID__;
const widgets = Process.getModuleByName('Qt5Widgets.dll');
const core = Process.getModuleByName('Qt5Core.dll');
const all = new NativeFunction(widgets.getExportByName('?allWidgets@QApplication@@SA?AV?$QList@PEAVQWidget@@@@XZ'), 'pointer', ['pointer']);
const geometry = new NativeFunction(widgets.getExportByName('?geometry@QWidget@@QEBAAEBVQRect@@XZ'), 'pointer', ['pointer']);
const parent = new NativeFunction(widgets.getExportByName('?parentWidget@QWidget@@QEBAPEAV1@XZ'), 'pointer', ['pointer']);
const visible = new NativeFunction(widgets.getExportByName('?isVisible@QWidget@@QEBA_NXZ'), 'bool', ['pointer']);
const className = new NativeFunction(core.getExportByName('?className@QMetaObject@@QEBAPEBDXZ'), 'pointer', ['pointer']);
const objectName = new NativeFunction(core.getExportByName('?objectName@QObject@@QEBA?AVQString@@XZ'), 'pointer', ['pointer', 'pointer']);
const constData = new NativeFunction(core.getExportByName('?constData@QString@@QEBAPEBVQChar@@XZ'), 'pointer', ['pointer']);
const destroyString = new NativeFunction(core.getExportByName('??1QString@@QEAA@XZ'), 'void', ['pointer']);
const resize = new NativeFunction(widgets.getExportByName('?resize@QWidget@@QEAAXHH@Z'), 'void', ['pointer', 'int', 'int']);

function readName(widget) {
    const result = Memory.alloc(8);
    objectName(widget, result); // MSVC x64 member sret: this, result.
    try {
        const length = result.readPointer().add(4).readS32();
        if (length < 0 || length > 512) throw Error('QString bound exceeded');
        return constData(result).readUtf16String(length);
    } finally { destroyString(result); }
}

let busy = false, finished = false, resized = false;
Interceptor.attach(Process.getModuleByName('user32.dll').getExportByName('DispatchMessageW'), {
    onEnter() {
        if (busy || finished || Process.getCurrentThreadId() !== uiThread) return;
        busy = true;
        try {
            const result = Memory.alloc(8);
            all(result);
            // Pinned Qt 5 x64 QList<QWidget*> layout. Two returned list allocations
            // intentionally remain until owned-process teardown; not a production API.
            const data = result.readPointer();
            const begin = data.add(8).readS32(), end = data.add(12).readS32();
            if (begin < 0 || end < begin || end > 2500) throw Error('QList bound exceeded');
            const entries = [];
            for (let i = begin; i < end; ++i) {
                const widget = data.add(16 + i * 8).readPointer();
                if (!visible(widget)) continue;
                const metaObject = new NativeFunction(widget.readPointer().readPointer(), 'pointer', ['pointer'])(widget);
                const rect = geometry(widget);
                entries.push({ptr: widget.toString(), parent: parent(widget).toString(),
                    class: className(metaObject).readUtf8String(), name: readName(widget),
                    rect: [rect.readS32(), rect.add(4).readS32(), rect.add(8).readS32(), rect.add(12).readS32()]});
            }
            send({phase: resized ? 'resized' : 'initial', total_widgets: end - begin, widgets: entries});
            if (resized) { finished = true; return; }
            const main = entries.filter(entry => entry.class === 'MainWindow' && entry.parent === '0x0');
            if (main.length !== 1) throw Error('Expected one owned MainWindow');
            resized = true;
            resize(ptr(main[0].ptr), 1360, 860);
        } catch (error) {
            finished = true;
            send({error: String(error)});
        } finally { busy = false; }
    }
});
