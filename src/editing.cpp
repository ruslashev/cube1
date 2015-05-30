// editing.cpp: most map editing commands go here, entity editing commands are in world.cpp

#include "cube.h"

bool editmode = false; 

// the current selection, used by almost all editing commands
// invariant: all code assumes that these are kept inside MINBORD distance of the edge of the map
block sel =
{
    variable("selx",  0, 0, 4096, &sel.x,  NULL, false),
    variable("sely",  0, 0, 4096, &sel.y,  NULL, false),
    variable("selxs", 0, 0, 4096, &sel.xs, NULL, false),
    variable("selys", 0, 0, 4096, &sel.ys, NULL, false),
};

int selh = 0;
bool selset = false;

#define loopselxy(b) { makeundo(); loop(x,sel.xs) loop(y,sel.ys) { sqr *s = S(sel.x+x, sel.y+y); b; }; remip(sel); }

int cx, cy, ch;

int curedittex[] = { -1, -1, -1 };

bool dragging = false;
int lastx, lasty, lasth;

int lasttype = 0, lasttex = 0;
sqr rtex;

void correctsel()                                       // ensures above invariant
{
    selset = !OUTBORD(sel.x, sel.y);
    int bsize = ssize-MINBORD;
    if(sel.xs+sel.x>bsize) sel.xs = bsize-sel.x;
    if(sel.ys+sel.y>bsize) sel.ys = bsize-sel.y;
    if(sel.xs<=0 || sel.ys<=0) selset = false;
};

void selectpos(int x, int y, int xs, int ys)
{
    block s = { x, y, xs, ys };
    sel = s;
    selh = 0;
    correctsel();
};

void makesel()
{
    block s = { min(lastx,cx), min(lasty,cy), abs(lastx-cx)+1, abs(lasty-cy)+1 };
    sel = s;
    selh = max(lasth,ch);
    correctsel();
    if(selset) rtex = *S(sel.x, sel.y);
};

VAR(flrceil,0,0,2);

float sheight(sqr *s, sqr *t, float z)                  // finds out z height when cursor points at wall
{
    return !flrceil //z-s->floor<s->ceil-z
        ? (s->type==FHF ? s->floor-t->vdelta/4.0f : (float)s->floor)
        : (s->type==CHF ? s->ceil+t->vdelta/4.0f : (float)s->ceil);
};

void cursorupdate()                                     // called every frame from hud
{
    flrceil = ((int)(player1->pitch>=0))*2;

    volatile float x = worldpos.x;                      // volatile needed to prevent msvc7 optimizer bug?
    volatile float y = worldpos.y;
    volatile float z = worldpos.z;
    
    cx = (int)x;
    cy = (int)y;

    if(OUTBORD(cx, cy)) return;
    sqr *s = S(cx,cy);
    
    if(fabs(sheight(s,s,z)-z)>1)                        // selected wall
    {
        x += x>player1->o.x ? 0.5f : -0.5f;             // find right wall cube
        y += y>player1->o.y ? 0.5f : -0.5f;

        cx = (int)x;
        cy = (int)y;

        if(OUTBORD(cx, cy)) return;
    };
        
    if(dragging) makesel();

    const int GRIDSIZE = 5;
    const float GRIDW = 0.5f;
    const float GRID8 = 2.0f;
    const float GRIDS = 2.0f;
    const int GRIDM = 0x7;
    
    // render editing grid

    for(int ix = cx-GRIDSIZE; ix<=cx+GRIDSIZE; ix++) for(int iy = cy-GRIDSIZE; iy<=cy+GRIDSIZE; iy++)
    {
        if(OUTBORD(ix, iy)) continue;
        sqr *s = S(ix,iy);
        if(SOLID(s)) continue;
        float h1 = sheight(s, s, z);
        float h2 = sheight(s, SWS(s,1,0,ssize), z);
        float h3 = sheight(s, SWS(s,1,1,ssize), z);
        float h4 = sheight(s, SWS(s,0,1,ssize), z);
        if(s->tag) linestyle(GRIDW, 0xFF, 0x40, 0x40);
        else if(s->type==FHF || s->type==CHF) linestyle(GRIDW, 0x80, 0xFF, 0x80);
        else linestyle(GRIDW, 0x80, 0x80, 0x80);
        block b = { ix, iy, 1, 1 };
        box(b, h1, h2, h3, h4);
        linestyle(GRID8, 0x40, 0x40, 0xFF);
        if(!(ix&GRIDM))   line(ix,   iy,   h1, ix,   iy+1, h4);
        if(!(ix+1&GRIDM)) line(ix+1, iy,   h2, ix+1, iy+1, h3);
        if(!(iy&GRIDM))   line(ix,   iy,   h1, ix+1, iy,   h2);
        if(!(iy+1&GRIDM)) line(ix,   iy+1, h4, ix+1, iy+1, h3);
    };

    if(!SOLID(s))
    {
        float ih = sheight(s, s, z);
        linestyle(GRIDS, 0xFF, 0xFF, 0xFF);
        block b = { cx, cy, 1, 1 };
        box(b, ih, sheight(s, SWS(s,1,0,ssize), z), sheight(s, SWS(s,1,1,ssize), z), sheight(s, SWS(s,0,1,ssize), z));
        linestyle(GRIDS, 0xFF, 0x00, 0x00);
        dot(cx, cy, ih);
        ch = (int)ih;
    };

    if(selset)
    {
        linestyle(GRIDS, 0xFF, 0x40, 0x40);
        box(sel, (float)selh, (float)selh, (float)selh, (float)selh);
    };
};

vector<block *> undos;                                  // unlimited undo
VARP(undomegs, 0, 1, 10);                                // bounded by n megs

void pruneundos(int maxremain)                          // bound memory
{
    int t = 0;
    loopvrev(undos)
    {
        t += undos[i]->xs*undos[i]->ys*sizeof(sqr);
        if(t>maxremain) free(undos.remove(i));
    };
};

void makeundo()
{
    undos.add(blockcopy(sel));
    pruneundos(undomegs<<20);
};

void tofronttex()                                       // maintain most recently used of the texture lists when applying texture
{
    loopi(3)
    {
        int c = curedittex[i];
        if(c>=0)
        {
            uchar *p = hdr.texlists[i];
            int t = p[c];
            for(int a = c-1; a>=0; a--) p[a+1] = p[a];
            p[0] = t;
            curedittex[i] = -1;
        };
    };
};

// the core editing function. all the *xy functions perform the core operations
// and are also called directly from the network, the function below it is strictly
// triggered locally. They all have very similar structure.

void editheightxy(bool isfloor, int amount, block &sel)
{
    loopselxy(if(isfloor)
    {
        s->floor += amount;
        if(s->floor>=s->ceil) s->floor = s->ceil-1;
    }
    else
    {
        s->ceil += amount;
        if(s->ceil<=s->floor) s->ceil = s->floor+1;
    });
};

void edittexxy(int type, int t, block &sel)            
{
    loopselxy(switch(type)
    {
        case 0: s->ftex = t; break;
        case 1: s->wtex = t; break;
        case 2: s->ctex = t; break;
        case 3: s->utex = t; break;
    });
};

void edittypexy(int type, block &sel)
{
    loopselxy(s->type = type);
};

void editequalisexy(bool isfloor, block &sel)
{
    int low = 127, hi = -128;
    loopselxy(
    {
        if(s->floor<low) low = s->floor;
        if(s->ceil>hi) hi = s->ceil;
    });
    loopselxy(
    {
        if(isfloor) s->floor = low; else s->ceil = hi;
        if(s->floor>=s->ceil) s->floor = s->ceil-1;
    });
};

void setvdeltaxy(int delta, block &sel)
{
    loopselxy(s->vdelta = max(s->vdelta+delta, 0));
    remipmore(sel);    
};

const int MAXARCHVERT = 50;
int archverts[MAXARCHVERT][MAXARCHVERT];
bool archvinit = false;

void archvertex(int span, int vert, int delta)
{
    if(!archvinit)
    {
        archvinit = true;
        loop(s,MAXARCHVERT) loop(v,MAXARCHVERT) archverts[s][v] = 0;
    };
    if(span>=MAXARCHVERT || vert>=MAXARCHVERT || span<0 || vert<0) return;
    archverts[span][vert] = delta;
};

VARF(fullbright, 0, 0, 1,
    if(fullbright)
    {
        if(1) return;
    };
);

COMMANDN(select, selectpos, ARG_4INT);
COMMAND(archvertex, ARG_3INT);

