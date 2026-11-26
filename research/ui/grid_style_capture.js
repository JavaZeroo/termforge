// IDs are assigned by exact equality of every RGB pixel in a cell, across the
// entire session. Only IDs, run lengths and colour counts leave the viewport.
const knownCells = new Map();
let gridOrigin;
function captureGrid(phase, terminal) {
    scan();
    if (!gridOrigin) {
        const marker = terminal.markers.find(m => m.label === 'GRID');
        if (phase !== 0 || !marker || marker.document_line !== 1 || marker.column !== 4)
            throw Error('missing primary calibration marker');
        const r=marker.rect;
        gridOrigin=[r.x-4*r.width,r.y-r.height,r.width,r.height];
    }
    const [x,y,w,h]=gridOrigin, vp=viewPort(), bounds=rect(vp);
    const region=Memory.alloc(16);
    [0,0,bounds[2]-bounds[0],bounds[3]-bounds[1]].forEach((v,i)=>region.add(i*4).writeS32(v));
    const pm=Memory.alloc(32), im=Memory.alloc(32);
    let havePixmap=false,haveImage=false;
    try {
        grab(vp,pm,region); havePixmap=true;
        toImage(pm,im); haveImage=true;
        if (imageNull(im) || imageDpr(im)!==1 || pixmapDpr(pm)!==1 ||
            x<0 || y<0 || w<=0 || h<=0 || x+91*w>imageWidth(im) || y+25*h>imageHeight(im))
            throw Error('grid does not fit the captured viewport');
        const runs=[], classes=[], pixels=new Uint32Array(w*h);
        for(let row=0;row<25;++row) for(let col=0;col<91;++col) {
            let key='',size=0;
            for(let dy=0;dy<h;++dy) for(let dx=0;dx<w;++dx) {
                const rgb=pixel(im,x+col*w+dx,y+row*h+dy)>>>0;
                pixels[size++]=rgb;
                key+=String.fromCharCode(rgb>>>16,rgb&65535);
            }
            let id=knownCells.get(key);
            if(id===undefined) {
                id=knownCells.size;
                if(id>=512) throw Error('cell class budget exceeded');
                knownCells.set(key,id);
                const counts=new Map();
                for(const rgb of pixels) counts.set(rgb,(counts.get(rgb)||0)+1);
                const colors=[...counts].sort((a,b)=>b[1]-a[1]);
                classes.push({id,colors:colors.length,base:colors[0][0],base_count:colors[0][1]});
            }
            const last=runs[runs.length-1];
            if(last && last[0]===id) ++last[1]; else runs.push([id,1]);
        }
        return {geometry:gridOrigin,runs,classes};
    } finally {
        if(haveImage) destroyImage(im);
        if(havePixmap) destroyPixmap(pm);
    }
}
