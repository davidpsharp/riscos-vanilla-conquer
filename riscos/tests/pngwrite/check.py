import sys,subprocess
from PIL import Image
bad=0
for (w,h,p) in [(1,1,1),(7,5,7),(7,5,16),(640,400,640),(1680,1050,1680),(1681,1051,1700),(65534,1,65534),(65535,2,65535),(65534,3,65534),(255,257,255),(3,30000,3),(1023,333,1023),(2000,2,2003),(320,200,320)]:
    out=f"o_{w}x{h}_{p}.png"
    subprocess.check_call(["./pngwrite_test",str(w),str(h),str(p),out])
    im=Image.open(out); im.load()
    raw=open(out+".raw","rb").read(); pal=open(out+".pal","rb").read()
    assert im.mode=="P" and im.size==(w,h),(im.mode,im.size)
    ok = im.tobytes()==raw and bytes(im.getpalette()[:768])==pal
    r=subprocess.run(["magick","identify","-verbose",out],capture_output=True,text=True)
    print(w,h,p,"OK" if ok and r.returncode==0 else "BAD", "magick-warn" if r.stderr.strip() else "")
    bad+= not ok
print("bad",bad)
