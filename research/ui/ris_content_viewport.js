// Compare painted four-character strips with controls from the same viewport.
// The document API supplies calibration rectangles only at phase zero.
let geometryOfStrip, controlMasks, traversal;
function mask(image, x, y, w, h) {
    const values = [], frequencies = new Map();
    for (let row = 0; row < h; row++) {
        for (let col = 0; col < w; col++) {
            const rgb = pixel(image, x + col, y + row) >>> 0;
            values.push(rgb);
            frequencies.set(rgb, (frequencies.get(rgb) || 0) + 1);
        }
    }
    const bg = [...frequencies].sort((a,b) => b[1] - a[1])[0][0];
    return values.map(v => Math.max(...[0,8,16].map(s =>
        Math.abs(((v >>> s) & 255) - ((bg >>> s) & 255)))) > 32 ? '1' : '0').join('');
}
function visibleLabels(phase, terminal) {
    const vp = viewPort(), bounds = rect(vp);
    const region = Memory.alloc(16);
    [0,0,bounds[2]-bounds[0],bounds[3]-bounds[1]].forEach((v,i) => region.add(4*i).writeS32(v));
    const pm = Memory.alloc(32), im = Memory.alloc(32);
    let hasPixmap = false, hasImage = false;
    try {
        grab(vp, pm, region); hasPixmap = true;
        toImage(pm, im); hasImage = true;
        if (imageNull(im) || imageDpr(im) !== 1 || pixmapDpr(pm) !== 1) throw Error('viewport image geometry');
        if (!geometryOfStrip) {
            const m = terminal.markers.find(v => v.label === 'HIST');
            if (phase !== 0 || !m || m.document_line !== 2 || m.column !== 8) throw Error('calibration marker');
            const r = m.rect;
            geometryOfStrip = [r.x, r.y - 2*r.height, 4*r.width, r.height];
        }
        const [x,y,w,h] = geometryOfStrip;
        if (w <= 0 || h <= 0 || x < 0 || y < 0 || x+w > imageWidth(im) ||
            y+terminal.capacity*h > imageHeight(im)) throw Error('strip outside viewport');
        const masks = Array.from({length: terminal.capacity}, (_,row) => mask(im,x,y+row*h,w,h));
        if (!controlMasks) {
            controlMasks = Object.fromEntries(['HIST','MAIN','ALTT','POST'].map((label,i) => [masks[2+3*i],label]));
            if (Object.keys(controlMasks).length !== 4 || Object.keys(controlMasks).some(m => !m.includes('1')))
                throw Error('painted glyph controls are ambiguous');
        }
        return masks.map(m => controlMasks[m] || (m.includes('1') ? '?' : ''));
    } finally {
        if (hasImage) destroyImage(im);
        if (hasPixmap) destroyPixmap(pm);
    }
}
function scanPages(phase, terminal) {
    scan();
    if (!scroll) throw Error('missing scrollbar');
    const max = sliderMax(scroll), saved = sliderValue(scroll);
    if (!traversal) {
        if (max < 0 || max > 200) throw Error('synthetic scroll range');
        const positions = [];
        for (let v = 0; v < max; v += terminal.capacity) positions.push(v);
        positions.push(max);
        traversal = {saved, max, positions, pages:[], moving:false};
    }
    const t = traversal;
    if (t.max !== max) throw Error('scroll range changed during capture');
    if (t.pages.length === t.positions.length) {
        if (saved !== t.saved) { sliderSet(scroll,t.saved); due=Date.now()+150; return null; }
        traversal = null;
        return {original_value:t.saved, maximum:max, strip:geometryOfStrip, pages:t.pages};
    }
    const target = t.positions[t.pages.length];
    if (!t.moving) { sliderSet(scroll,target); t.moving=true; due=Date.now()+150; return null; }
    if (saved !== target) throw Error('scroll position was not applied');
    t.pages.push({value:saved, scroll_top:terminal.scroll, rows:visibleLabels(phase, terminal)});
    t.moving=false;
    return null;
}
