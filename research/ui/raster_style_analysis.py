"""Pure bounded analysis of private H/space QRgb samples; never publish pixels."""
from __future__ import annotations
from collections import Counter
import hashlib
import json
import math


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


def audit_capture(capture):
    """Return a scalar-only projection, retaining calibrated/unknown boundaries."""
    public={key:capture[key] for key in ("phase","label","cursor_reply","grab_attempted","constructed","destroyed","calibration","reason")}
    require(type(capture["calibration"]) is bool,"Calibration status missing")
    require(capture["constructed"] in ([],["pixmap"],["pixmap","image"]),"Construction order differs")
    require(capture["destroyed"]==list(reversed(capture["constructed"])) or (not capture["calibration"] and capture["reason"]=="object-destruction-failed"),"Destruction order differs")
    image=capture["image"]
    if image is not None:
        require(all(type(image[key]) is int and 0<=image[key]<=4096 for key in ("width","height","viewport_width","viewport_height")),"Image scalar bound differs")
        require(type(image["is_null"]) is bool,"Image null state missing")
        require(image["grab_rect"]==[0,0,image["viewport_width"]-1,image["viewport_height"]-1],"Grab QRect is not inclusive")
        require(all(image[key] is None or (type(image[key]) in (int,float) and math.isfinite(image[key]) and abs(image[key])<=4) for key in ("pixmap_dpr","image_dpr")),"DPR scalar bound differs")
    if capture["calibration"]:
        require(capture["grab_attempted"] is True and capture["constructed"]==["pixmap","image"] and capture["destroyed"]==["image","pixmap"],"Calibrated image lifecycle differs")
        require(image and image["is_null"] is False,"Empty calibrated image")
        pdpr,idpr=image["pixmap_dpr"],image["image_dpr"]
        require(type(pdpr) in (int,float) and math.isfinite(pdpr) and 0<pdpr<=4 and pdpr==idpr,"DPR differs")
        require(image["width"]==image["viewport_width"]*pdpr and image["height"]==image["viewport_height"]*pdpr,"Image/viewport/DPR differs")
        require(image["grab_rect"]==[0,0,image["viewport_width"]-1,image["viewport_height"]-1],"Grab QRect is not inclusive")
    public["image"]=None if image is None else {key:image[key] for key in ("viewport_width","viewport_height","grab_rect","pixmap_dpr","image_dpr","width","height","is_null")}
    public["rois"]=[]
    terminal=capture["terminal"]
    for roi in capture["rois"]:
        require(type(roi["ordinal"]) is int and roi["ordinal"] in (0,1),"ROI ordinal differs")
        require(image is not None,"ROI without image")
        require(roi["ordinal"]<len(terminal["markers"]),"ROI has no independent marker")
        marker=terminal["markers"][roi["ordinal"]]
        require(marker["character"]=="H" and marker["preceding_character"]==" ","ROI marker text differs")
        for kind in ("h","space"):
            check_rectangle(roi[kind+"_rect"],image["width"],image["height"])
            rectangle=roi[kind+"_rect"]
            logical=marker[kind+"_rect"]
            require(rectangle==[logical[key]*image["image_dpr"] for key in ("x","y","width","height")],"Physical ROI differs from marker/DPR")
            require(len(roi[kind+"_pixels"])==rectangle[2]*rectangle[3],"ROI sample count differs")
        entry={"ordinal":roi["ordinal"],"h_rect":roi["h_rect"],"space_rect":roi["space_rect"],"h":pixel_summary(roi["h_pixels"]),"space":pixel_summary(roi["space_pixels"]),"colors":roi["colors"]}
        if roi["colors"] is not None:
            require(roi["colors"]==analyze_pixels(roi["h_pixels"],roi["space_pixels"]),"Producer colors differ from private samples")
        if capture["calibration"]:
            require(roi["colors"] is not None,"Calibrated ROI has no colors")
        public["rois"].append(entry)
    if capture["calibration"]:
        require(len(capture["rois"])==(2 if capture["phase"]==0 else 1),"Calibrated ROI count differs")
        if capture["phase"]==0:
            require(capture["rois"][0]["colors"]==capture["rois"][1]["colors"],"Calibration H pair differs")
    return public


def comparisons(captures):
    if len(captures)!=5 or not all(capture["calibration"] for capture in captures):
        return None
    colors=[capture["rois"][0]["colors"] for capture in captures]
    require(colors[0]!=colors[2],"References are not distinguishable")
    return {"measurement":"Equality of sampled H/space raster background and dominant ink, not internal pen state",
            "entry_same_sample_colors_as_main":colors[1]==colors[0],"entry_same_sample_colors_as_alternate_reference":colors[1]==colors[2],
            "repeat_same_sample_colors_as_main":colors[3]==colors[0],"repeat_same_sample_colors_as_alternate_reference":colors[3]==colors[2],
            "return_same_sample_colors_as_main":colors[4]==colors[0],"return_same_sample_colors_as_alternate_reference":colors[4]==colors[2]}
