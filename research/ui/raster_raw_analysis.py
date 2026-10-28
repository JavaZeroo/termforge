"""Offline raw-capture-v1 analysis; pixel arrays never enter public output."""
from __future__ import annotations
from collections import Counter
import hashlib
import json
import math
from research.ui.raster_selection_analysis import require,rgba,check_rectangle,native_bool,require_collapsed,GROUPS

MARKERS=[
    [('D0 H H',3,'default',0),('D0 H H',5,'default',1),('M0 H H',3,'main',0),('M0 H H',5,'main',1)],
    [('A0 H',3,'entry',0),('A0S1 H',5,'control-main',0),('A0D0 H',5,'control-default',0)],
    [('A1 H H',3,'alternate',0),('A1 H H',5,'alternate',1)],
    [('A2 H',3,'repeat',0),('A2D0 H',5,'control-default',0),('A2S1 H',5,'control-main',0),('A2S2 H',5,'control-alternate',0)],
    [('M1 H',3,'return',0),('M1S1 H',5,'control-main',0),('M1S2 H',5,'control-alternate',0),('M1D0 H',5,'control-default',0)]]


def marker_bindings(terminal,phase):
    require(len(terminal['markers'])==len(MARKERS[phase]),'Marker count differs')
    for ordinal,(label,offset,group,sample) in enumerate(MARKERS[phase]):
        marker=terminal['markers'][ordinal]
        require((marker['ordinal'],marker['label'],marker['group'],marker['sample'])==(ordinal,label,group,sample),'Marker identity differs')
        hits=[]
        for row,line in enumerate(terminal['lines']):
            at=line.find(label)
            while at>=0:
                end=at+len(label)
                if (at==0 or line[at-1]==' ') and (end==len(line) or line[end]==' '):hits.append((row,at+offset))
                at=line.find(label,at+1)
        require(hits==[(marker['document_line'],marker['column'])],'Complete marker token is not bound uniquely')


def pixel_summary(pixels):
    require(isinstance(pixels,list) and 0<len(pixels)<=8192,'Empty or oversized ROI')
    for value in pixels:rgba(value)
    return {'samples':len(pixels),'sha256':hashlib.sha256(json.dumps(pixels,separators=(',',':')).encode()).hexdigest(),
            'hash_encoding':'UTF-8 compact JSON array of unsigned QRgb values',
            'histogram':[{'rgba':rgba(value),'count':count} for value,count in sorted(Counter(pixels).items())]}


def classify_pixels(h_pixels,space_pixels):
    result={'h':pixel_summary(h_pixels),'space':pixel_summary(space_pixels),'background_rgba':None,
            'dominant_ink_rgba':None,'dominant_ink_candidates':[],'ink_observed':None,'status':'space-background-ambiguous'}
    backgrounds=Counter(space_pixels)
    if len(backgrounds)!=1:return result
    background=space_pixels[0];result['background_rgba']=rgba(background)
    ink=Counter(value for value in h_pixels if value!=background)
    result['ink_observed']=bool(ink)
    if not ink:
        result['status']='no-marker-ink';return result
    maximum=max(ink.values())
    candidates=[{'rgba':rgba(value),'count':count} for value,count in sorted(ink.items()) if count==maximum]
    result['dominant_ink_candidates']=candidates
    result['status']='unique-dominant-ink' if len(candidates)==1 else 'dominant-ink-ambiguous'
    if len(candidates)==1:result['dominant_ink_rgba']=candidates[0]['rgba']
    return result


def raw_signature(roi):
    return {kind:{'shape':roi[kind+'_rect'][2:4],'sha256':roi[kind]['sha256'],'samples':roi[kind]['samples']} for kind in ('h','space')}


def compare_frame(rois):
    names={'control-default':'SGR0','control-main':'SGR31;44','control-alternate':'SGR32;45'}
    target=rois[0]
    matches=[names[roi['group']] for roi in rois[1:] if target['ink_observed'] is True and roi['ink_observed'] is True and raw_signature(roi)==raw_signature(target)]
    return {'matching_controls':matches,'unique_control':matches[0] if len(matches)==1 else None,
            'result':'unique-raw-match' if len(matches)==1 else 'ambiguous' if matches else 'unknown',
            'signature':'Exact ordered H and space QRgb SHA-256 plus shapes and sample counts',
            'unmatched_does_not_imply_reset':True}


def audit_capture(capture):
    """Verify capture structure, then derive a scalar-only classification."""
    public={key:capture[key] for key in ("phase","label","cursor_reply","grab_attempted","constructed","destroyed","capture_valid","reason")}
    require(type(capture["capture_valid"]) is bool,"Capture validity missing")
    require(capture["constructed"] in ([],["pixmap"],["pixmap","image"]),"Construction order differs")
    require(capture["destroyed"]==list(reversed(capture["constructed"])) or (not capture["capture_valid"] and capture["reason"]=="object-destruction-failed"),"Destruction order differs")
    image=capture["image"]
    if image is not None:
        require(all(type(image[key]) is int and 0<=image[key]<=4096 for key in ("width","height","viewport_width","viewport_height")),"Image scalar bound differs")
        is_null=native_bool(image["is_null"])
        require(image["grab_rect"]==[0,0,image["viewport_width"]-1,image["viewport_height"]-1],"Grab QRect is not inclusive")
        require(all(image[key] is None or (type(image[key]) in (int,float) and math.isfinite(image[key]) and abs(image[key])<=4) for key in ("pixmap_dpr","image_dpr")),"DPR scalar bound differs")
    if capture["capture_valid"]:
        require(capture["grab_attempted"] is True and capture["constructed"]==["pixmap","image"] and capture["destroyed"]==["image","pixmap"],"Valid image lifecycle differs")
        require(image and is_null is False,"Empty valid image")
        pdpr,idpr=image["pixmap_dpr"],image["image_dpr"]
        require(type(pdpr) in (int,float) and math.isfinite(pdpr) and 0<pdpr<=4 and pdpr==idpr,"DPR differs")
        require(image["width"]==image["viewport_width"]*pdpr and image["height"]==image["viewport_height"]*pdpr,"Image/viewport/DPR differs")
        require(image["grab_rect"]==[0,0,image["viewport_width"]-1,image["viewport_height"]-1],"Grab QRect is not inclusive")
    public["image"]=None if image is None else {key:image[key] for key in ("viewport_width","viewport_height","grab_rect","pixmap_dpr","image_dpr","width","height","is_null")}
    if image is not None:
        public["image"].update(is_null=is_null,is_null_raw_value=image["is_null"],is_null_raw_type="boolean" if type(image["is_null"]) is bool else "number")
    public["rois"]=[]
    terminal=capture["terminal"]
    require('selection' not in terminal,'Raw selectionText must not be retained in new captures')
    text_type,text_length=terminal['selection_text_type'],terminal['selection_text_length']
    require(text_type in ('string','null','undefined','number','boolean','object','function'),'Unknown selectionText type')
    require(text_length is None or (type(text_length) is int and 0<=text_length<=1000000),'Invalid selectionText length')
    caret,anchor=terminal['selection_caret'],terminal['selection_anchor']
    safe=lambda value:value if type(value) is int and 0<=value<=9007199254740991 else None
    public['selection']={'caret':safe(caret),'anchor':safe(anchor),'text_type':text_type,'text_length':text_length}
    if capture['capture_valid']:
        require_collapsed(caret,anchor)
        marker_bindings(terminal,capture['phase'])
    require([roi['ordinal'] for roi in capture['rois']]==list(range(len(capture['rois']))),'ROI identity is not an ordered unique prefix')
    if capture['capture_valid'] and capture['phase'] in (1,3,4):
        require(len({marker['document_line'] for marker in terminal['markers']})==1,'Same-frame controls are not on one document row')
        require(len({(marker['h_rect']['y'],marker['h_rect']['height']) for marker in terminal['markers']})==1,'Same-frame controls do not share the logical row')
    for roi in capture["rois"]:
        require(type(roi["ordinal"]) is int and 0<=roi["ordinal"]<len(GROUPS[capture['phase']]),"ROI ordinal differs")
        require(roi['group']==GROUPS[capture['phase']][roi['ordinal']] and roi['sample'] in (0,1),'ROI group differs')
        require(image is not None,"ROI without image")
        require(roi["ordinal"]<len(terminal["markers"]),"ROI has no independent marker")
        marker=terminal["markers"][roi["ordinal"]]
        require(marker["character"]=="H" and marker["preceding_character"]==" ","ROI marker text differs")
        if capture['capture_valid']:
            cursor=terminal['caret_rect']
            require(cursor and all(type(cursor[key]) in (int,float) and math.isfinite(cursor[key]) for key in ('x','y','width','height')) and cursor['width']>=0 and cursor['height']>0,'caret-location-unknown')
            occupancy={**cursor,'width':max(1,cursor['width'])}
            for logical in (marker['h_rect'],marker['space_rect']):
                intersects=logical['x']<occupancy['x']+occupancy['width'] and occupancy['x']<logical['x']+logical['width'] and logical['y']<occupancy['y']+occupancy['height'] and occupancy['y']<logical['y']+logical['height']
                require(not intersects,'caret-overlaps-roi')
        for kind in ("h","space"):
            check_rectangle(roi[kind+"_rect"],image["width"],image["height"])
            rectangle=roi[kind+"_rect"]
            logical=marker[kind+"_rect"]
            require(rectangle==[logical[key]*image["image_dpr"] for key in ("x","y","width","height")],"Physical ROI differs from marker/DPR")
            require(len(roi[kind+"_pixels"])==rectangle[2]*rectangle[3],"ROI sample count differs")
        require(roi['h_rect'][1:4]==roi['space_rect'][1:4] and roi['space_rect'][0]+roi['space_rect'][2]==roi['h_rect'][0],'H/space adjacency differs')
        require('colors' not in roi,'Raw capture must not contain producer classifications')
        entry={"ordinal":roi["ordinal"],'group':roi['group'],'sample':roi['sample'],"h_rect":roi["h_rect"],"space_rect":roi["space_rect"],**classify_pixels(roi['h_pixels'],roi['space_pixels'])}
        entry['raw_signature']=raw_signature(entry)
        public["rois"].append(entry)
    if capture["capture_valid"]:
        require(len(capture["rois"])==len(GROUPS[capture['phase']]),"Complete capture ROI count differs")
    return public



def frame_comparisons(captures):
    return [{'phase':capture['phase'],**compare_frame(capture['rois'])} for capture in captures if capture['capture_valid'] and capture['phase'] in (1,3,4)]
