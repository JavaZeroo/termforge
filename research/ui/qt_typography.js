// Original controlled UI probe for the pinned Qt 5 / MSVC x64 package only.
// Reads geometry/font properties, never terminal text or credential fields.
// Opens only the single synthetic local session supplied by launch.py.
'use strict';
const uiThread = __UI_THREAD_ID__;
const widgets = Process.getModuleByName('Qt5Widgets.dll');
const gui = Process.getModuleByName('Qt5Gui.dll');
const core = Process.getModuleByName('Qt5Core.dll');
const wind = Process.getModuleByName('WindTerm.exe');
const fn = (module, name, result, args) => new NativeFunction(module.getExportByName(name), result, args);
const all = fn(widgets, '?allWidgets@QApplication@@SA?AV?$QList@PEAVQWidget@@@@XZ', 'pointer', ['pointer']);
const geometry = fn(widgets, '?geometry@QWidget@@QEBAAEBVQRect@@XZ', 'pointer', ['pointer']);
const parent = fn(widgets, '?parentWidget@QWidget@@QEBAPEAV1@XZ', 'pointer', ['pointer']);
const visible = fn(widgets, '?isVisible@QWidget@@QEBA_NXZ', 'bool', ['pointer']);
const className = fn(core, '?className@QMetaObject@@QEBAPEBDXZ', 'pointer', ['pointer']);
const objectName = fn(core, '?objectName@QObject@@QEBA?AVQString@@XZ', 'pointer', ['pointer', 'pointer']);
const constData = fn(core, '?constData@QString@@QEBAPEBVQChar@@XZ', 'pointer', ['pointer']);
const destroyString = fn(core, '??1QString@@QEAA@XZ', 'void', ['pointer']);
const emptyString = fn(core, '??0QString@@QEAA@XZ', 'pointer', ['pointer']);
const resize = fn(widgets, '?resize@QWidget@@QEAAXHH@Z', 'void', ['pointer', 'int', 'int']);
const update = fn(widgets, '?update@QWidget@@QEAAXXZ', 'void', ['pointer']);
const font = fn(widgets, '?font@QWidget@@QEBAAEBVQFont@@XZ', 'pointer', ['pointer']);
const fontFamily = fn(gui, '?family@QFont@@QEBA?AVQString@@XZ', 'pointer', ['pointer', 'pointer']);
const pointSize = fn(gui, '?pointSizeF@QFont@@QEBANXZ', 'double', ['pointer']);
const pixelSize = fn(gui, '?pixelSize@QFont@@QEBAHXZ', 'int', ['pointer']);
const fontBold = fn(gui, '?bold@QFont@@QEBA_NXZ', 'bool', ['pointer']);
const infoCtor = fn(gui, '??0QFontInfo@@QEAA@AEBVQFont@@@Z', 'pointer', ['pointer', 'pointer']);
const infoDestroy = fn(gui, '??1QFontInfo@@QEAA@XZ', 'void', ['pointer']);
const infoFamily = fn(gui, '?family@QFontInfo@@QEBA?AVQString@@XZ', 'pointer', ['pointer', 'pointer']);
const infoPoint = fn(gui, '?pointSizeF@QFontInfo@@QEBANXZ', 'double', ['pointer']);
const infoPixel = fn(gui, '?pixelSize@QFontInfo@@QEBAHXZ', 'int', ['pointer']);
const infoExact = fn(gui, '?exactMatch@QFontInfo@@QEBA_NXZ', 'bool', ['pointer']);
const logicalX = fn(gui, '?logicalDpiX@QPaintDevice@@QEBAHXZ', 'int', ['pointer']);
const logicalY = fn(gui, '?logicalDpiY@QPaintDevice@@QEBAHXZ', 'int', ['pointer']);
const physicalX = fn(gui, '?physicalDpiX@QPaintDevice@@QEBAHXZ', 'int', ['pointer']);
const physicalY = fn(gui, '?physicalDpiY@QPaintDevice@@QEBAHXZ', 'int', ['pointer']);
const dpr = fn(gui, '?devicePixelRatioF@QPaintDevice@@QEBANXZ', 'double', ['pointer']);
const painterDevice = fn(gui, '?device@QPainter@@QEBAPEAVQPaintDevice@@XZ', 'pointer', ['pointer']);
const painterFont = fn(gui, '?font@QPainter@@QEBAAEBVQFont@@XZ', 'pointer', ['pointer']);
const tabCount = fn(widgets, '?count@QTabBar@@QEBAHXZ', 'int', ['pointer']);
const tabRect = fn(widgets, '?tabRect@QTabBar@@QEBA?AVQRect@@H@Z', 'pointer', ['pointer', 'pointer', 'int']);
const expandAll = fn(widgets, '?expandAll@QTreeView@@QEAAXXZ', 'void', ['pointer']);
const focus = fn(widgets, '?setFocus@QWidget@@QEAAXW4FocusReason@Qt@@@Z', 'void', ['pointer', 'int']);
const keyCtor = fn(gui, '??0QKeyEvent@@QEAA@W4Type@QEvent@@HV?$QFlags@W4KeyboardModifier@Qt@@@@AEBVQString@@_NG@Z',
    'pointer', ['pointer', 'int', 'int', 'int', 'pointer', 'bool', 'uint16']);
const keyDestroy = fn(gui, '??1QKeyEvent@@UEAA@XZ', 'void', ['pointer']);
const sendEvent = fn(core, '?sendEvent@QCoreApplication@@SA_NPEAVQObject@@PEAVQEvent@@@Z', 'bool', ['pointer', 'pointer']);
const windowHandle = fn(widgets, '?windowHandle@QWidget@@QEBAPEAVQWindow@@XZ', 'pointer', ['pointer']);
const windowScreen = fn(gui, '?screen@QWindow@@QEBAPEAVQScreen@@XZ', 'pointer', ['pointer']);
const screenDpr = fn(gui, '?devicePixelRatio@QScreen@@QEBANXZ', 'double', ['pointer']);
const screenLogical = fn(gui, '?logicalDotsPerInchX@QScreen@@QEBANXZ', 'double', ['pointer']);
const screenPhysical = fn(gui, '?physicalDotsPerInchX@QScreen@@QEBANXZ', 'double', ['pointer']);

function stringResult(call, self) {
    const result = Memory.alloc(8);
    call(self, result);
    try {
        const length = result.readPointer().add(4).readS32();
        if (length < 0 || length > 256) throw Error('QString bound exceeded');
        return constData(result).readUtf16String(length);
    } finally { destroyString(result); }
}
function fontData(value) {
    const info = Memory.alloc(8);
    infoCtor(info, value);
    try {
        return {family: stringResult(fontFamily, value), point_size: pointSize(value), pixel_size: pixelSize(value), bold: fontBold(value),
            resolved_family: stringResult(infoFamily, info), resolved_point_size: infoPoint(info),
            resolved_pixel_size: infoPixel(info), exact_match: infoExact(info)};
    } finally { infoDestroy(info); }
}
function rectData(rect) { return [rect.readS32(), rect.add(4).readS32(), rect.add(8).readS32(), rect.add(12).readS32()]; }
let devices = new Map(), observedFonts = new Set(), paintBusy = false;
const phaseSizes = new Map();
let phase = 'starting', busy = false, finished = false, stage = 0, due = 0, main = null, tree = null;
const started = Date.now();
function sample(label) {
    phase = label;
    if (wind.base.add(0xc29394).readU8() !== 0) throw Error('first-run import guard failed');
    const result = Memory.alloc(8); all(result);
    // Bounded Qt5 QList return allocations retained until owned-process teardown.
    const data = result.readPointer(), begin = data.add(8).readS32(), end = data.add(12).readS32();
    if (begin < 0 || end < begin || end > 2500) throw Error('QList bound exceeded');
    const entries = [], nextDevices = new Map();
    for (let i = begin; i < end; ++i) {
        const widget = data.add(16 + i * 8).readPointer();
        if (!visible(widget)) continue;
        const meta = new NativeFunction(widget.readPointer().readPointer(), 'pointer', ['pointer'])(widget);
        const entry = {ptr: widget.toString(), parent: parent(widget).toString(),
            class: className(meta).readUtf8String(), name: stringResult(objectName, widget), rect: rectData(geometry(widget))};
        // The QWidget -> QPaintDevice +0x10 adjustment is verified in this DLL.
        const device = widget.add(0x10);
        entry.dpi = {logical: [logicalX(device), logicalY(device)], physical: [physicalX(device), physicalY(device)], dpr: dpr(device)};
        entry.font = fontData(font(widget));
        if (entry.class === 'TabBar') {
            const count = tabCount(widget); if (count < 0 || count > 32) throw Error('tab bound exceeded');
            entry.tabs = [];
            for (let n = 0; n < count; ++n) { const r = Memory.alloc(16); tabRect(widget, r, n); entry.tabs.push(rectData(r)); }
        }
        nextDevices.set(device.toString(), {ptr: entry.ptr, class: entry.class, name: entry.name});
        entries.push(entry);
    }
    devices = nextDevices;
    const roots = entries.filter(e => e.class === 'MainWindow' && e.parent === '0x0');
    if (roots.length !== 1) throw Error('expected one MainWindow');
    main = ptr(roots[0].ptr);
    const mainRect = roots[0].rect;
    phaseSizes.set(label, [mainRect[2] - mainRect[0] + 1, mainRect[3] - mainRect[1] + 1]);
    const panes = entries.filter(e => e.class === 'SessionPane');
    const trees = entries.filter(e => e.class === 'TreeView' && panes.some(p => p.ptr === e.parent));
    if (trees.length !== 1) throw Error('expected one session tree');
    tree = ptr(trees[0].ptr);
    const window = windowHandle(main), screen = window.isNull() ? ptr(0) : windowScreen(window);
    if (label.startsWith('session-')) {
        const views = entries.filter(e => e.class === 'TerminalView');
        const groups = entries.filter(e => e.name === 'ViewGroup');
        const tabs = entries.filter(e => e.class === 'TabBar' && groups.some(g => g.ptr === e.parent));
        if (views.length !== 1 || tabs.length !== 1 || tabs[0].tabs.length !== 1 || entries.some(e => e.class === 'MessageBar'))
            throw Error('expected one main terminal view without an error bar');
    }
    send({kind: 'snapshot', phase: label, total_widgets: end - begin, widgets: entries,
        first_run_import_flag: wind.base.add(0xc29394).readU8(),
        screen: screen.isNull() ? null : {dpr: screenDpr(screen), logical_dpi_x: screenLogical(screen), physical_dpi_x: screenPhysical(screen)}});
    for (const entry of entries) update(ptr(entry.ptr));
}
function keyPress(widget, key) {
    const text = Memory.alloc(8); emptyString(text);
    try {
        for (const type of [6, 7]) {
            const event = Memory.alloc(256); keyCtor(event, type, key, 0, text, 0, 1);
            try { sendEvent(widget, event); } finally { keyDestroy(event); }
        }
    } finally { destroyString(text); }
}
// A text draw call establishes painter font use, but not per-glyph fallback.
// No text arguments are read. Unmatched backing-store devices are omitted.
const textHooks = gui.enumerateExports().filter(e => /^\?draw(Text|StaticText|TextItem)@QPainter@@/.test(e.name));
for (const exported of textHooks) Interceptor.attach(exported.address, {
    onEnter(args) {
        if (paintBusy || finished || busy || phase === null || Process.getCurrentThreadId() !== uiThread) return;
        paintBusy = true;
        try {
            const widget = devices.get(painterDevice(args[0]).toString());
            if (!widget) return;
            const r = rectData(geometry(main)), size = [r[2] - r[0] + 1, r[3] - r[1] + 1];
            if (JSON.stringify(size) !== JSON.stringify(phaseSizes.get(phase))) throw Error('font phase size changed');
            const value = fontData(painterFont(args[0]));
            const key = JSON.stringify([phase, widget.ptr, value]);
            if (observedFonts.has(key)) return;
            if (observedFonts.size >= 500) throw Error('font observation bound exceeded');
            observedFonts.add(key); send({kind: 'text_draw_font', phase, window_size: size, widget, font: value, api: exported.name});
        } catch (error) { finished = true; send({error: String(error), phase}); }
        finally { paintBusy = false; }
    }
});
send({kind: 'probe_ready', text_hook_count: textHooks.length, qt_abi: 'pinned-5.15.2-msvc-x64'});
Interceptor.attach(Process.getModuleByName('user32.dll').getExportByName('DispatchMessageW'), {
    onEnter() {
        if (busy || finished || Process.getCurrentThreadId() !== uiThread || Date.now() < due) return;
        busy = true;
        try {
            if (Date.now() - started > 15000) throw Error('scenario timeout');
            if (stage === 0) { sample('initial'); due = Date.now() + 300; stage = 1; }
            else if (stage === 1) { phase = null; resize(main, 1360, 860); due = Date.now() + 300; stage = 2; }
            else if (stage === 2) { sample('empty-1360'); due = Date.now() + 300; stage = 3; }
            else if (stage === 3) {
                phase = null; expandAll(tree); focus(tree, 7);
                keyPress(tree, 0x01000011); // End selects the only synthetic leaf.
                due = Date.now() + 200; stage = 4;
            } else if (stage === 4) { keyPress(tree, 0x01000004); due = Date.now() + 800; stage = 5; }
            else if (stage === 5) { sample('session-1360'); due = Date.now() + 500; stage = 6; }
            else if (stage === 6) { phase = null; resize(main, 2048, 1120); due = Date.now() + 500; stage = 7; }
            else if (stage === 7) { sample('session-2048'); due = Date.now() + 500; stage = 8; }
            else if (stage === 8) { finished = true; send({kind: 'scenario_complete', text_font_observations: observedFonts.size}); }
        } catch (error) { finished = true; send({error: String(error), phase}); }
        finally { busy = false; }
    }
});
