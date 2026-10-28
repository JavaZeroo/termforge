"""Pure bounded analysis of private H/space QRgb samples; never publish pixels."""
from __future__ import annotations
from collections import Counter
import hashlib
import json
import math

GROUPS=[['default','default','main','main'],['entry','control-main','control-default'],['alternate','alternate'],['repeat','control-default','control-main','control-alternate'],['return','control-main','control-alternate','control-default']]


def require_collapsed(caret,anchor):
    require(type(caret) is int and type(anchor) is int and 0<=caret<=9007199254740991 and 0<=anchor<=9007199254740991,'selection-endpoints-unavailable')
    require(caret==anchor,'selection-not-collapsed')
    return True


def require(condition,message):
    if not condition:
        raise ValueError(message)


def rgba(value):
    require(type(value) is int and 0<=value<=0xffffffff,"Invalid unsigned QRgb")
    return [(value>>16)&255,(value>>8)&255,value&255,(value>>24)&255]


def analyze_pixels(h_pixels,space_pixels):
    require(h_pixels and space_pixels and len(h_pixels)<=8192 and len(space_pixels)<=8192,"Empty or oversized ROI")
    for value in h_pixels+space_pixels:
        rgba(value)
    background=space_pixels[0]
    require(all(value==background for value in space_pixels),"space-background-ambiguous")
    ink=Counter(value for value in h_pixels if value!=background)
    require(ink,"no-marker-ink")
    ordered=sorted(ink.items(),key=lambda item:(-item[1],item[0]))
    require(len(ordered)==1 or ordered[0][1]>ordered[1][1],"dominant-ink-ambiguous")
    return {"background_rgba":rgba(background),"dominant_ink_rgba":rgba(ordered[0][0])}


def pixel_summary(pixels):
    for value in pixels:
        rgba(value)
    counts=Counter(pixels)
    require(0<len(pixels)<=8192 and len(counts)<=256,"ROI summary bound")
    encoded=json.dumps(pixels,separators=(",",":")).encode("utf-8")
    return {"samples":len(pixels),"sha256":hashlib.sha256(encoded).hexdigest(),
            "hash_encoding":"UTF-8 compact JSON array of unsigned QRgb values",
            "histogram":[{"rgba":rgba(value),"count":count} for value,count in sorted(counts.items())]}


def check_rectangle(rectangle,width,height):
    require(isinstance(rectangle,list) and len(rectangle)==4 and all(type(value) is int for value in rectangle),"Invalid physical rectangle")
    x,y,w,h=rectangle
    require(w>0 and h>0 and w*h<=8192 and x>=0 and y>=0 and x+w<=width and y+h<=height,"ROI outside image")


def native_bool(value):
    """Frida bool returns may be JSON booleans or numeric 0/1; retain that identity."""
    require(type(value) is bool or (type(value) is int and value in (0,1)),"Image null state must be bool or integer 0/1")
    return bool(value)


def audit_capture(capture):
    """Return a scalar-only projection, retaining calibrated/unknown boundaries."""
    public={key:capture[key] for key in ("phase","label","cursor_reply","grab_attempted","constructed","destroyed","calibration","reason")}
    require(type(capture["calibration"]) is bool,"Calibration status missing")
    require(capture["constructed"] in ([],["pixmap"],["pixmap","image"]),"Construction order differs")
    require(capture["destroyed"]==list(reversed(capture["constructed"])) or (not capture["calibration"] and capture["reason"]=="object-destruction-failed"),"Destruction order differs")
    image=capture["image"]
    if image is not None:
        require(all(type(image[key]) is int and 0<=image[key]<=4096 for key in ("width","height","viewport_width","viewport_height")),"Image scalar bound differs")
        is_null=native_bool(image["is_null"])
        require(image["grab_rect"]==[0,0,image["viewport_width"]-1,image["viewport_height"]-1],"Grab QRect is not inclusive")
        require(all(image[key] is None or (type(image[key]) in (int,float) and math.isfinite(image[key]) and abs(image[key])<=4) for key in ("pixmap_dpr","image_dpr")),"DPR scalar bound differs")
    if capture["calibration"]:
        require(capture["grab_attempted"] is True and capture["constructed"]==["pixmap","image"] and capture["destroyed"]==["image","pixmap"],"Calibrated image lifecycle differs")
        require(image and is_null is False,"Empty calibrated image")
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
    if capture['calibration']:require_collapsed(caret,anchor)
    if capture['calibration'] and capture['phase'] in (1,3,4):
        require(len({marker['document_line'] for marker in terminal['markers']})==1,'Same-frame controls are not on one document row')
        require(len({(marker['h_rect']['y'],marker['h_rect']['height']) for marker in terminal['markers']})==1,'Same-frame controls do not share the logical row')
    for roi in capture["rois"]:
        require(type(roi["ordinal"]) is int and 0<=roi["ordinal"]<len(GROUPS[capture['phase']]),"ROI ordinal differs")
        require(roi['group']==GROUPS[capture['phase']][roi['ordinal']] and roi['sample'] in (0,1),'ROI group differs')
        require(image is not None,"ROI without image")
        require(roi["ordinal"]<len(terminal["markers"]),"ROI has no independent marker")
        marker=terminal["markers"][roi["ordinal"]]
        require(marker["character"]=="H" and marker["preceding_character"]==" ","ROI marker text differs")
        if capture['calibration']:
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
        entry={"ordinal":roi["ordinal"],'group':roi['group'],'sample':roi['sample'],"h_rect":roi["h_rect"],"space_rect":roi["space_rect"],"h":pixel_summary(roi["h_pixels"]),"space":pixel_summary(roi["space_pixels"]),"colors":roi["colors"]}
        if roi["colors"] is not None:
            require(roi["colors"]==analyze_pixels(roi["h_pixels"],roi["space_pixels"]),"Producer colors differ from private samples")
        if capture["calibration"]:
            require(roi["colors"] is not None,"Calibrated ROI has no colors")
        public["rois"].append(entry)
    if capture["calibration"]:
        require(len(capture["rois"])==len(GROUPS[capture['phase']]),"Calibrated ROI count differs")
        if capture["phase"]==0:
            require(capture["rois"][0]["colors"]==capture["rois"][1]["colors"],"Calibration H pair differs")
            require(capture["rois"][2]["colors"]==capture["rois"][3]["colors"],"Main calibration H pair differs")
        if capture['phase']==2:require(capture['rois'][0]['colors']==capture['rois'][1]['colors'],'Alternate calibration H pair differs')
    return public


def comparisons(captures):
    if len(captures)!=5 or not all(capture["calibration"] for capture in captures):
        return None
    references={'SGR0':captures[0]['rois'][0]['colors'],'SGR31;44':captures[0]['rois'][2]['colors'],'SGR32;45':captures[2]['rois'][0]['colors']}
    frames=[]
    names={'control-default':'SGR0','control-main':'SGR31;44','control-alternate':'SGR32;45'}
    for index in (1,3,4):
        target=captures[index]['rois'][0]['colors']
        controls={names[roi['group']]:roi['colors'] for roi in captures[index]['rois'][1:]}
        matches=[label for label,value in controls.items() if value==target]
        frames.append({'phase':index,'same_frame_matching_controls':matches,'unique_same_frame_control':matches[0] if len(matches)==1 else None,
                       'same_frame_result':'unique-match' if len(matches)==1 else 'ambiguous' if matches else 'unmatched',
                       'cross_frame_matching_references':[label for label,value in references.items() if value==target]})
    return {'measurement':'Exact same-frame H/space output-color comparisons; no internal pen or reset inference','frames':frames,'unmatched_does_not_imply_reset':True}
