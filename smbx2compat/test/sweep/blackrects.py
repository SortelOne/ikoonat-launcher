# blackrects.py <png>...: solid pure-black rectangles (>= 6x6, mostly filled) in screenshots
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngread import load
for p in sys.argv[1:]:
    w,h,px=load(p)
    blk=[[ (q[0]<2 and q[1]<2 and q[2]<2) for q in row] for row in px]
    seen=[[False]*w for _ in range(h)]
    found=[]
    for y in range(h):
        for x in range(w):
            if blk[y][x] and not seen[y][x]:
                st=[(x,y)]; seen[y][x]=True; n=0; x0=x1=x; y0=y1=y
                while st:
                    a,b=st.pop(); n+=1
                    x0=min(x0,a);x1=max(x1,a);y0=min(y0,b);y1=max(y1,b)
                    for c,d in ((a+1,b),(a-1,b),(a,b+1),(a,b-1)):
                        if 0<=c<w and 0<=d<h and blk[d][c] and not seen[d][c]:
                            seen[d][c]=True; st.append((c,d))
                bw,bh=x1-x0+1,y1-y0+1
                if bw>=6 and bh>=6 and n>=0.9*bw*bh and bw*bh<0.5*w*h:
                    found.append((x0,y0,bw,bh))
    if found: print(p, found[:6])
