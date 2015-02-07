/* FCE Ultra - NES/Famicom Emulator
*
* Copyright notice for this file:
*  Copyright (C) 1998 BERO
*  Copyright (C) 2003 Xodnizel
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

// Tom 7's notes. See this page for an ok explanation:
// http://wiki.nesdev.com/w/index.php/PPU_OAM
// Note sprites are drawn from the end of the array to the beginning.

#include  <string.h>
#include  <stdio.h>
#include  <stdlib.h>

#include  "types.h"
#include  "x6502.h"
#include  "fceu.h"
#include  "ppu.h"
#include  "sound.h"
#include  "file.h"
#include  "utils/endian.h"
#include  "utils/memory.h"

#include  "cart.h"
#include  "palette.h"
#include  "state.h"
#include  "video.h"
#include  "input.h"
#include  "driver.h"

#define DEBUGF if (0) fprintf

#define VBlankON  (PPU[0]&0x80)   //Generate VBlank NMI
#define Sprite16  (PPU[0]&0x20)   //Sprites 8x16/8x8
#define BGAdrHI   (PPU[0]&0x10)   //BG pattern adr $0000/$1000
#define SpAdrHI   (PPU[0]&0x08)   //Sprite pattern adr $0000/$1000
#define INC32     (PPU[0]&0x04)   //auto increment 1/32

#define SpriteON  (PPU[1]&0x10)   //Show Sprite
#define ScreenON  (PPU[1]&0x08)   //Show screen
#define PPUON    (PPU[1]&0x18)		//PPU should operate
#define GRAYSCALE (PPU[1]&0x01) //Grayscale (AND palette entries with 0x30)

#define SpriteLeft8 (PPU[1]&0x04)
#define BGLeft8 (PPU[1]&0x02)

#define PPU_status      (PPU[2])

static void FetchSpriteData();
static void RefreshLine(int lastpixel);
static void RefreshSprites();
static void CopySprites(uint8 *target);

static void Fixit1();

// PPU lookup table? I think? These are constant arrays
// that do some kind of bit transformations. ppulut2 is
// the same as ppulut1, but with the bits shifted up one.
// (if they are constant, can they be initialized constexpr? -tom7)
static uint32 ppulut1[256];
static uint32 ppulut2[256];
static uint32 ppulut3[128];

// These used to be options that could be controlled through the UI
// but that's probably not a good idea for general purposes. Saved as
// constants in case it becomes useful to make them settable in the
// future (e.g. automapping).
static constexpr bool rendersprites = true;
static constexpr bool renderbg = true;

// XXX not thread safe. Only used in (disabled) debugging code.
static const char *attrbits(uint8 b) {
  static char buf[9] = {0};
  for (int i = 0; i < 8; i ++) {
    buf[7 - i] = (b & (1 << i))? "VHB???11"[7 - i] : "__F___00"[7 - i];
  }
  return buf;
}

template<int BITS>
struct BITREVLUT {
  uint8* lut;

  BITREVLUT() {
    int bits = BITS;
    int n = 1<<BITS;
    lut = new uint8[n];

    int m = 1;
    int a = n >> 1;
    int j = 2;

    lut[0] = 0;
    lut[1] = a;

    while (--bits) {
      m <<= 1;
      a >>= 1;
      for (int i = 0; i < m; i++)
	lut[j++] = lut[i] + a;
    }
  }

  uint8 operator[](int index) { return lut[index]; }
};
BITREVLUT<8> bitrevlut;

struct PPUSTATUS {
  int32 sl;
  int32 cycle, end_cycle;
};
struct SPRITE_READ {
  int32 num;
  int32 count;
  int32 fetch;
  int32 found;
  int32 found_pos[8];
  int32 ret;
  int32 last;
  int32 mode;

  void reset() {
    num = count = fetch = found = ret = last = mode = 0;
    found_pos[0] = found_pos[1] = found_pos[2] = found_pos[3] = 0;
    found_pos[4] = found_pos[5] = found_pos[6] = found_pos[7] = 0;
  }

  void start_scanline() {
    num = 1;
    found = 0;
    fetch = 1;
    count = 0;
    last = 64;
    mode = 0;
    found_pos[0] = found_pos[1] = found_pos[2] = found_pos[3] = 0;
    found_pos[4] = found_pos[5] = found_pos[6] = found_pos[7] = 0;
  }
};

// doesn't need to be savestated as it is just a reflection of the
// current position in the ppu loop
// PPUPHASE ppuphase;

// this needs to be savestated since a game may be trying to read from
// this across vblanks
SPRITE_READ spr_read;

// definitely needs to be savestated
uint8 idleSynch = 1;

// uses the internal counters concept at
// http://nesdev.icequake.net/PPU%20addressing.txt
struct PPUREGS {
  // normal clocked regs. as the game can interfere with these at any
  // time, they need to be savestated
  uint32 fv;//3
  uint32 v;//1
  uint32 h;//1
  uint32 vt;//5
  uint32 ht;//5

  //temp unlatched regs (need savestating, can be written to at any time)
  uint32 _fv, _v, _h, _vt, _ht;

  //other regs that need savestating
  uint32 fh; //3 (horz scroll)
  uint32 s; //1 ($2000 bit 4: "Background pattern table address (0: $0000; 1: $1000)")

  // other regs that don't need saving
  uint32 par; // 8 (sort of a hack, just stored in here, but not managed by this system)

  // cached state data. these are always reset at the beginning of a frame and don't need saving
  // but just to be safe, we're gonna save it
  PPUSTATUS status;

  void reset() {
    fv = v = h = vt = ht = 0;
    fh = par = s = 0;
    _fv = _v = _h = _vt = _ht = 0;
    status.cycle = 0;
    status.end_cycle = 341;
    status.sl = 241;
  }

  void install_latches() {
    fv = _fv;
    v = _v;
    h = _h;
    vt = _vt;
    ht = _ht;
  }

  void install_h_latches() {
    ht = _ht;
    h = _h;
  }

  void clear_latches() {
    _fv = _v = _h = _vt = _ht = 0;
    fh = 0;
  }

  void increment_hsc() {
    // The first one, the horizontal scroll counter, consists of 6
    // bits, and is made up by daisy-chaining the HT counter to the H
    // counter. The HT counter is then clocked every 8 pixel dot
    // clocks (or every 8/3 CPU clock cycles).
    ht++;
    h += (ht>>5);
    ht &= 31;
    h &= 1;
  }

  void increment_vs() {
    fv++;
    int fv_overflow = (fv >> 3);
    vt += fv_overflow;
    vt &= 31; //fixed tecmo super bowl
    if (vt == 30 && fv_overflow==1) {
      // caution here (only do it at the exact instant of overflow) fixes p'radikus conflict
      v++;
      vt = 0;
    }
    fv &= 7;
    v &= 1;
  }

  uint32 get_ntread() const {
    return 0x2000 | (v<<0xB) | (h<<0xA) | (vt<<5) | ht;
  }

  uint32 get_2007access() const {
    return ((fv&3)<<0xC) | (v<<0xB) | (h<<0xA) | (vt<<5) | ht;
  }

  // The PPU has an internal 4-position, 2-bit shifter, which it uses for
  // obtaining the 2-bit palette select data during an attribute table byte
  // fetch. To represent how this data is shifted in the diagram, letters a..c
  // are used in the diagram to represent the right-shift position amount to
  // apply to the data read from the attribute data (a is always 0). This is why
  // you only see bits 0 and 1 used off the read attribute data in the diagram.
  uint32 get_atread() const {
    return 0x2000 | (v<<0xB) | (h<<0xA) | 0x3C0 | ((vt&0x1C)<<1) | ((ht&0x1C)>>2);
  }

  // address line 3 relates to the pattern table fetch occuring (the
  // PPU always makes them in pairs).
  uint32 get_ptread() const {
    return (s<<0xC) | (par<<0x4) | fv;
  }

  void increment2007(bool by32) {
    // If the VRAM address increment bit (2000.2) is clear (inc. amt. = 1), all the
    // scroll counters are daisy-chained (in the order of HT, VT, H, V, FV) so that
    // the carry out of each counter controls the next counter's clock rate. The
    // result is that all 5 counters function as a single 15-bit one. Any access to
    // 2007 clocks the HT counter here.
    //
    // If the VRAM address increment bit is set (inc. amt. = 32), the only
    // difference is that the HT counter is no longer being clocked, and the VT
    // counter is now being clocked by access to 2007.
    if (by32) {
      vt++;
    } else {
      ht++;
      vt+=(ht>>5)&1;
    }
    h+=(vt>>5);
    v+=(h>>1);
    fv+=(v>>1);
    ht &= 31;
    vt &= 31;
    h &= 1;
    v &= 1;
    fv &= 7;
  }
};

PPUREGS ppur;

static void makeppulut() {
  for (int x=0; x < 256; x++) {
    ppulut1[x] = 0;

    for (int y=0; y < 8; y++) {
      ppulut1[x] |= ((x>>(7-y))&1)<<(y*4);
    }

    ppulut2[x] = ppulut1[x] << 1;
  }

  for (int cc = 0; cc < 16; cc++) {
    for (int xo = 0;xo < 8; xo++) {
      ppulut3[ xo | ( cc << 3 ) ] = 0;

      for (int pixel = 0; pixel < 8; pixel++) {
	int shiftr;
	shiftr = ( pixel + xo ) / 8;
	shiftr *= 2;
	ppulut3[ xo | (cc<<3) ] |= ( ( cc >> shiftr ) & 3 ) << ( 2 + pixel * 4 );
      }
      //    printf("%08x\n",ppulut3[xo|(cc<<3)]);
    }
  }
}

static int ppudead=1;
static int kook=0;

//mbg 6/23/08
//make the no-bg fill color configurable
//0xFF shall indicate to use palette[0]
uint8 gNoBGFillColor = 0xFF;

int MMC5Hack = 0;
uint32 MMC5HackVROMMask = 0;
uint8 *MMC5HackExNTARAMPtr = nullptr;
uint8 *MMC5HackVROMPTR = nullptr;
uint8 MMC5HackCHRMode = 0;
uint8 MMC5HackSPMode = 0;
uint8 MMC50x5130 = 0;
uint8 MMC5HackSPScroll = 0;
uint8 MMC5HackSPPage = 0;


uint8 VRAMBuffer = 0, PPUGenLatch = 0;
uint8 *vnapage[4] = { nullptr, nullptr, nullptr, nullptr };
uint8 PPUNTARAM = 0;
uint8 PPUCHRRAM = 0;

//Color deemphasis emulation.  Joy...
static uint8 deemp = 0;
static int deempcnt[8] = {};

void (*GameHBIRQHook)(), (*GameHBIRQHook2)();
void (*PPU_hook)(uint32 A);

uint8 vtoggle = 0;
uint8 XOffset = 0;

static uint32 TempAddr = 0;
static uint32 RefreshAddr = 0;

static int maxsprites = 8;

//scanline is equal to the current visible scanline we're on.
int scanline;
int g_rasterpos;
static uint32 scanlines_per_frame;

uint8 PPU[4];
uint8 PPUSPL;
uint8 NTARAM[0x800],PALRAM[0x20],SPRAM[0x100],SPRBUF[0x100];
uint8 UPALRAM[0x03]; //for 0x4/0x8/0xC addresses in palette, the ones in
                     //0x20 are 0 to not break fceu rendering.


#define MMC5SPRVRAMADR(V)      &fceulib__cart.MMC5SPRVPage[(V)>>10][(V)]
#define VRAMADR(V)      &fceulib__cart.VPage[(V)>>10][(V)]

//mbg 8/6/08 - fix a bug relating to
//"When in 8x8 sprite mode, only one set is used for both BG and sprites."
//in mmc5 docs
uint8 * MMC5BGVRAMADR(uint32 V) {
  if (!Sprite16) {
    extern uint8 mmc5ABMode;                /* A=0, B=1 */
    if (mmc5ABMode==0)
      return MMC5SPRVRAMADR(V);
    else
      return &fceulib__cart.MMC5BGVPage[(V)>>10][(V)];
  } else return &fceulib__cart.MMC5BGVPage[(V)>>10][(V)];
}

// this duplicates logic which is embedded in the ppu rendering code
// which figures out where to get CHR data from depending on various hack modes
// mostly involving mmc5.
// this might be incomplete.
uint8* FCEUPPU_GetCHR(uint32 vadr, uint32 refreshaddr) {
  if (MMC5Hack) {
    if (MMC5HackCHRMode==1) {
      uint8 *C = MMC5HackVROMPTR;
      C += (((MMC5HackExNTARAMPtr[refreshaddr & 0x3ff]) & 
	     0x3f & MMC5HackVROMMask) << 12) + (vadr & 0xfff);
      C += (MMC50x5130&0x3)<<18; //11-jun-2009 for kuja_killer
      return C;
    } else {
      return MMC5BGVRAMADR(vadr);
    }
  }
  else return VRAMADR(vadr);
}

//likewise for ATTR
int FCEUPPU_GetAttr(int ntnum, int xt, int yt) {
  int attraddr = 0x3C0+((yt>>2)<<3)+(xt>>2);
  int temp = (((yt&2)<<1)+(xt&2));
  int refreshaddr = xt+yt*32;
  if (MMC5Hack && MMC5HackCHRMode==1)
    return (MMC5HackExNTARAMPtr[refreshaddr & 0x3ff] & 0xC0)>>6;
  else
    return (vnapage[ntnum][attraddr] & (3<<temp)) >> temp;
}

// TODO(tom7): Maybe just remove this entirely. Can at least make it a
// compile-time constant to reduce code size / branches.
// whether to use the new ppu (new PPU doesn't handle MMC5 extra
// nametables at all
// static constexpr int newppu = 0;

//---------------

static DECLFR(A2002) {
  TRACEF("A2002 %d", PPU_status);

  FCEUPPU_LineUpdate();
  uint8 ret = PPU_status;
  TRACEN(ret);

  ret|=PPUGenLatch&0x1F;
  TRACEN(ret);

  vtoggle=0;
  PPU_status&=0x7F;
  PPUGenLatch=ret;

  return ret;
}

static DECLFR(A2004) {
  FCEUPPU_LineUpdate();
  return PPUGenLatch;
}

/* Not correct for $2004 reads. */
static DECLFR(A200x) {
  FCEUPPU_LineUpdate();
  return PPUGenLatch;
}

static DECLFR(A2007) {
  uint8 ret;
  uint32 tmp=RefreshAddr&0x3FFF;

  FCEUPPU_LineUpdate();

  ret=VRAMBuffer;

  if (PPU_hook) PPU_hook(tmp);
  PPUGenLatch=VRAMBuffer;
  if (tmp < 0x2000) {
    VRAMBuffer=fceulib__cart.VPage[tmp>>10][tmp];
  } else if (tmp < 0x3F00) {
    VRAMBuffer=vnapage[(tmp>>10)&0x3][tmp&0x3FF];
  }


  if ((ScreenON || SpriteON) && scanline < 240) {
    uint32 rad=RefreshAddr;

    if ((rad&0x7000)==0x7000) {
      rad^=0x7000;
      if ((rad&0x3E0)==0x3A0)
	rad^=0xBA0;
      else if ((rad&0x3E0)==0x3e0)
	rad^=0x3e0;
      else
	rad+=0x20;
    } else {
      rad+=0x1000;
    }
    RefreshAddr=rad;
  } else {
    if (INC32)
      RefreshAddr+=32;
    else
      RefreshAddr++;
  }
  if (PPU_hook) PPU_hook(RefreshAddr&0x3fff);

  return ret;
}

static DECLFW(B2000) {
  //    FCEU_printf("%04x:%02x, (%d) %02x, %02x\n",A,V,scanline,PPU[0],PPU_status);

  FCEUPPU_LineUpdate();
  PPUGenLatch=V;
  if (!(PPU[0]&0x80) && (V&0x80) && (PPU_status&0x80)) {
    //     FCEU_printf("Trigger NMI, %d, %d\n",timestamp,ppudead);
    TriggerNMI2();
  }
  PPU[0]=V;
  TempAddr&=0xF3FF;
  TempAddr|=(V&3)<<10;

  ppur._h = V&1;
  ppur._v = (V>>1)&1;
  ppur.s = (V>>4)&1;
}

static DECLFW(B2001) {
  //printf("%04x:$%02x, %d\n",A,V,scanline);
  FCEUPPU_LineUpdate();
  PPUGenLatch=V;
  PPU[1]=V;
  if (V&0xE0)
    deemp=V>>5;
}
//
static DECLFW(B2002)
{
	PPUGenLatch=V;
}

static DECLFW(B2003)
{
	//printf("$%04x:$%02x, %d, %d\n",A,V,timestamp,scanline);
	PPUGenLatch=V;
	PPU[3]=V;
	PPUSPL=V&0x7;
}

static DECLFW(B2004)
{
  //printf("Wr: %04x:$%02x\n",A,V);
  PPUGenLatch=V;
  if (PPUSPL>=8) {
    if (PPU[3]>=8)
      SPRAM[PPU[3]]=V;
  } else {
    //printf("$%02x:$%02x\n",PPUSPL,V);
    SPRAM[PPUSPL]=V;
  }
  PPU[3]++;
  PPUSPL++;
}

static DECLFW(B2005) {
  uint32 tmp=TempAddr;
  FCEUPPU_LineUpdate();
  PPUGenLatch=V;
  if (!vtoggle) {
    tmp&=0xFFE0;
    tmp|=V>>3;
    XOffset=V&7;
    ppur._ht = V>>3;
    ppur.fh = V&7;
  } else {
    tmp&=0x8C1F;
    tmp|=((V&~0x7)<<2);
    tmp|=(V&7)<<12;
    ppur._vt = V>>3;
    ppur._fv = V&7;
  }
  TempAddr=tmp;
  vtoggle^=1;
}


static DECLFW(B2006) {
  FCEUPPU_LineUpdate();

  PPUGenLatch=V;
  if (!vtoggle) {
    TempAddr&=0x00FF;
    TempAddr|=(V&0x3f)<<8;

    ppur._vt &= 0x07;
    ppur._vt |= (V&0x3)<<3;
    ppur._h = (V>>2)&1;
    ppur._v = (V>>3)&1;
    ppur._fv = (V>>4)&3;
  } else {
    TempAddr&=0xFF00;
    TempAddr|=V;

    RefreshAddr=TempAddr;
    if (PPU_hook)
      PPU_hook(RefreshAddr);
    //printf("%d, %04x\n",scanline,RefreshAddr);

    ppur._vt &= 0x18;
    ppur._vt |= (V>>5);
    ppur._ht = V&31;

    ppur.install_latches();
  }

  vtoggle^=1;
}

static DECLFW(B2007) {
  const uint32 tmp=RefreshAddr&0x3FFF;

  PPUGenLatch=V;
  if (tmp>=0x3F00) {
    // hmmm....
    if (!(tmp&0xf))
      PALRAM[0x00]=PALRAM[0x04]=PALRAM[0x08]=PALRAM[0x0C]=V&0x3F;
    else if (tmp&3) PALRAM[(tmp&0x1f)]=V&0x3f;
  } else if (tmp<0x2000) {
    if (PPUCHRRAM&(1<<(tmp>>10)))
      fceulib__cart.VPage[tmp>>10][tmp]=V;
  } else {
    if (PPUNTARAM&(1<<((tmp&0xF00)>>10)))
      vnapage[((tmp&0xF00)>>10)][tmp&0x3FF]=V;
  }
  //      FCEU_printf("ppu (%04x) %04x:%04x %d, %d\n",X.PC,RefreshAddr,PPUGenLatch,scanline,timestamp);
  if (INC32) RefreshAddr+=32;
  else RefreshAddr++;
  if (PPU_hook) PPU_hook(RefreshAddr&0x3fff);
}

static DECLFW(B4014) {
  const uint32 t=V<<8;

  for (int x=0;x<256;x++) {
    X6502_DMW(0x2004,X6502_DMR(t+x));
  }
}

static uint8 *Pline,*Plinef;
static int firsttile;
static int linestartts;
static int tofix=0;

static void ResetRL(uint8 *target) {
  memset(target,0xFF,256);
  InputScanlineHook(0,0,0,0);
  Plinef=target;
  Pline=target;
  firsttile=0;
  linestartts=timestamp*48+X.count;
  tofix=0;
  FCEUPPU_LineUpdate();
  tofix=1;
}

static uint8 sprlinebuf[256+8];

void FCEUPPU_LineUpdate() {
  if (Pline) {
    const int l = (PAL? ((timestamp*48-linestartts)/15) : 
		        ((timestamp*48-linestartts)>>4) );
    RefreshLine(l);
  }
}

// These two used to not be saved in stateinfo, but that caused execution
// to diverge in 'Ultimate Basketball'.
static int32 sphitx;
static uint8 sphitdata;

static void CheckSpriteHit(int p) {
  TRACEF("CheckSpriteHit %d %d %02x\n", p, sphitx, sphitdata);
  const int l = p - 16;

  if (sphitx==0x100) return;

  for (int x=sphitx;x<(sphitx+8) && x<l;x++) {
    if ((sphitdata&(0x80>>(x-sphitx))) && !(Plinef[x]&64) && x < 255) {
      TRACELOC();
      PPU_status|=0x40;
      //printf("Ha:  %d, %d, Hita: %d, %d, %d, %d, %d\n",p,p&~7,scanline,GETLASTPIXEL-16,&Plinef[x],Pline,Pline-Plinef);
      //printf("%d\n",GETLASTPIXEL-16);
      //if (Plinef[x] == 0xFF)
      //printf("PL: %d, %02x\n",scanline, Plinef[x]);
      sphitx=0x100;
      break;
    }
  }
}

static void EndRL() {
  RefreshLine(272);
  if (tofix)
    Fixit1();
  CheckSpriteHit(272);
  Pline=0;
}

//spork the world.  Any sprites on this line? Then this will be set to 1.
//Needed for zapper emulation and *gasp* sprite emulation.
static int any_sprites_on_line = 0;

// These used to be static inside RefreshLine, but interleavings of
// save/restore in "Ultimate Basketball" can cause execution to diverge.
// Now saved in stateinfo.
static uint32 pshift[2];
// This was also static; why not save it too? -tom7
static uint32 atlatch;

// lasttile is really "second to last tile."
static void RefreshLine(int lastpixel) {
  // pputile.inc modifies this variable, but we don't put the result
  // into RefreshAddr until we're done -- maybe so that hooks that
  // we execute don't see the updated value until the end. (Note
  // however that RefreshAddr is only used in ppu*.
  uint32 refreshaddr_local=RefreshAddr;

  /* Yeah, recursion would be bad.
     PPU_hook() functions can call
     mirroring/chr bank switching functions,
     which call FCEUPPU_LineUpdate, which call this
     function. */
  static int norecurse = 0;
  if (norecurse) return;

  TRACEF("RefreshLine %d %u %u %u %u %d",
	 lastpixel, pshift[0], pshift[1], atlatch, refreshaddr_local,
	 norecurse);

  uint32 vofs;
  int X1;

  register uint8 *P=Pline;
  int lasttile=lastpixel>>3;

  if (sphitx != 0x100 && !(PPU_status&0x40)) {
    if ((sphitx < (lastpixel-16)) && !(sphitx < ((lasttile - 2)*8))) {
      //printf("OK: %d\n",scanline);
      lasttile++;
    }
  }

  if (lasttile>34) lasttile=34;
  int numtiles=lasttile-firsttile;

  if (numtiles<=0) return;

  P=Pline;

  vofs=0;

  vofs=((PPU[0]&0x10)<<8) | ((refreshaddr_local>>12)&7);

  static constexpr int TOFIXNUM = 272 - 0x4;
  if (!ScreenON && !SpriteON) {
    uint32 tem;
    tem=PALRAM[0]|(PALRAM[0]<<8)|(PALRAM[0]<<16)|(PALRAM[0]<<24);
    tem|=0x40404040;
    FCEU_dwmemset(Pline,tem,numtiles*8);
    P+=numtiles*8;
    Pline=P;

    firsttile=lasttile;

    if (lastpixel>=TOFIXNUM && tofix) {
      Fixit1();
      tofix=0;
    }

    if ((lastpixel-16)>=0) {
      InputScanlineHook(Plinef,any_sprites_on_line?sprlinebuf:0,
			linestartts,lasttile*8-16);
    }
    return;
  }

  // Priority bits, needed for sprite emulation.
  PALRAM[0]|=64;
  PALRAM[4]|=64;
  PALRAM[8]|=64;
  PALRAM[0xC]|=64;

  // This high-level graphics MMC5 emulation code was written for MMC5
  //carts in "CL" mode. It's probably not totally correct for carts in
  //"SL" mode.

#define PPUT_MMC5
  if (MMC5Hack) {
    if (MMC5HackCHRMode==0 && (MMC5HackSPMode&0x80)) {
      int tochange=MMC5HackSPMode&0x1F;
      tochange-=firsttile;
      for (X1=firsttile;X1<lasttile;X1++) {
	if ((tochange<=0 && MMC5HackSPMode&0x40) || (tochange>0 && !(MMC5HackSPMode&0x40))) {
	  TRACELOC();
#define PPUT_MMC5SP
#include "pputile.inc"
#undef PPUT_MMC5SP
	} else {
	  TRACELOC();
#include "pputile.inc"
	}
	tochange--;
      }
    } else if (MMC5HackCHRMode==1 && (MMC5HackSPMode&0x80)) {
      int tochange=MMC5HackSPMode&0x1F;
      tochange-=firsttile;

#define PPUT_MMC5SP
#define PPUT_MMC5CHR1
      for (X1=firsttile;X1<lasttile;X1++) {
	TRACELOC();
#include "pputile.inc"
      }
#undef PPUT_MMC5CHR1
#undef PPUT_MMC5SP
    } else if (MMC5HackCHRMode==1) {
#define PPUT_MMC5CHR1
      for (X1=firsttile;X1<lasttile;X1++) {
	TRACELOC();
#include "pputile.inc"
      }
#undef PPUT_MMC5CHR1
    } else {
      for (X1=firsttile;X1<lasttile;X1++) {
	TRACELOC();
#include "pputile.inc"
      }
    }
  }
#undef PPUT_MMC5
  else if (PPU_hook) {
    norecurse=1;
#define PPUT_HOOK
    for (X1=firsttile;X1<lasttile;X1++) {
      TRACELOC();
#include "pputile.inc"
    }
#undef PPUT_HOOK
    norecurse=0;
  } else {
    for (X1=firsttile;X1<lasttile;X1++) {
      TRACELOC();
#include "pputile.inc"
    }
  }

  //Reverse changes made before.
  PALRAM[0]&=63;
  PALRAM[4]&=63;
  PALRAM[8]&=63;
  PALRAM[0xC]&=63;

  RefreshAddr=refreshaddr_local;
  if (firsttile<=2 && 2<lasttile && !(PPU[1]&2))
    {
      uint32 tem;
      tem=PALRAM[0]|(PALRAM[0]<<8)|(PALRAM[0]<<16)|(PALRAM[0]<<24);
      tem|=0x40404040;
      *(uint32 *)Plinef=*(uint32 *)(Plinef+4)=tem;
    }

  if (!ScreenON)
    {
      uint32 tem;
      int tstart,tcount;
      tem=PALRAM[0]|(PALRAM[0]<<8)|(PALRAM[0]<<16)|(PALRAM[0]<<24);
      tem|=0x40404040;

      tcount=lasttile-firsttile;
      tstart=firsttile-2;
      if (tstart<0)
	{
	  tcount+=tstart;
	  tstart=0;
	}
      if (tcount>0)
	FCEU_dwmemset(Plinef+tstart*8,tem,tcount*8);
    }

  if (lastpixel>=TOFIXNUM && tofix)
    {
      //puts("Fixed");
      Fixit1();
      tofix=0;
    }

  //CheckSpriteHit(lasttile*8); //lasttile*8); //lastpixel);

  //This only works right because of a hack earlier in this function.
  CheckSpriteHit(lastpixel);

  if ((lastpixel-16)>=0)
    {
      InputScanlineHook(Plinef,any_sprites_on_line?sprlinebuf:0,linestartts,lasttile*8-16);
    }
  Pline=P;
  firsttile=lasttile;
}

static inline void Fixit2() {
  if (ScreenON || SpriteON) {
    uint32 rad=RefreshAddr;
    rad&=0xFBE0;
    rad|=TempAddr&0x041f;
    RefreshAddr=rad;
    //PPU_hook(RefreshAddr);
    //PPU_hook(RefreshAddr,-1);
  }
}

static void Fixit1() {
  if (ScreenON || SpriteON) {
    uint32 rad=RefreshAddr;

    if ((rad & 0x7000) == 0x7000) {
      rad ^= 0x7000;
      if ((rad & 0x3E0) == 0x3A0)
	rad ^= 0xBA0;
      else if ((rad & 0x3E0) == 0x3e0)
	rad ^= 0x3e0;
      else
	rad += 0x20;
    } else {
      rad += 0x1000;
    }
    RefreshAddr = rad;
  }
}

void MMC5_hb(int);     //Ugh ugh ugh.
static void DoLine() {
  uint8 *target = XBuf + (scanline << 8);

  if (MMC5Hack && (ScreenON || SpriteON)) MMC5_hb(scanline);

  X6502_Run(256);
  EndRL();

  if (!renderbg) {
    // User asked to not display background data.
    uint8 col = (gNoBGFillColor == 0xFF) ? PALRAM[0] : gNoBGFillColor;
    uint32 tem = col|(col<<8)|(col<<16)|(col<<24);
    tem |= 0x40404040;
    FCEU_dwmemset(target,tem,256);
  }

  if (SpriteON)
    CopySprites(target);


  // What is this?? ORs every byte in the buffer with 0x30 if PPU[1]
  // has its lowest bit set.

  if (ScreenON || SpriteON) {
    // Yes, very el-cheapo.
    if (PPU[1]&0x01) {
      for (int x = 63; x >= 0; x--)
	*(uint32 *)&target[x<<2]=(*(uint32*)&target[x<<2])&0x30303030;
    }
  }
  if ((PPU[1]>>5)==0x7) {
    for (int x = 63; x >= 0; x--)
      *(uint32 *)&target[x<<2]=((*(uint32*)&target[x<<2])&0x3f3f3f3f)|0xc0c0c0c0;
  } else if (PPU[1]&0xE0) {
    for (int x = 63; x >= 0; x--)
      *(uint32 *)&target[x<<2]=(*(uint32*)&target[x<<2])|0x40404040;
  } else {
    for (int x = 63; x >= 0; x--)
      *(uint32 *)&target[x<<2]=((*(uint32*)&target[x<<2])&0x3f3f3f3f)|0x80808080;
  }

  sphitx=0x100;

  if (ScreenON || SpriteON)
    FetchSpriteData();

  if (GameHBIRQHook && (ScreenON || SpriteON) && ((PPU[0]&0x38)!=0x18)) {
    X6502_Run(6);
    Fixit2();
    X6502_Run(4);
    GameHBIRQHook();
    X6502_Run(85-16-10);
  } else {
    X6502_Run(6);  // Tried 65, caused problems with Slalom(maybe others)
    Fixit2();
    X6502_Run(85-6-16);

    // A semi-hack for Star Trek: 25th Anniversary
    if (GameHBIRQHook && (ScreenON || SpriteON) && ((PPU[0]&0x38)!=0x18))
      GameHBIRQHook();
  }

  if (SpriteON)
    RefreshSprites();
  if (GameHBIRQHook2 && (ScreenON || SpriteON))
    GameHBIRQHook2();
  scanline++;
  if (scanline<240) {
    ResetRL(XBuf+(scanline<<8));
  }
  X6502_Run(16);
}

#define V_FLIP  0x80
#define H_FLIP  0x40
#define SP_BACK 0x20

struct SPR {
  // no is just a tile number, but
  uint8 y,no,atr,x;
};

struct SPRB {
  // I think ca is the actual character data, but separated into
  // two planes. They together make the 2-bit color information,
  // which is done through the lookup tables ppulut1 and 2.
  // They have to be the actual data (not addresses) because
  // ppulut is a fixed transformation.
  uint8 ca[2],atr,x;
};

#define STATIC_ASSERT( condition, name ) \
  static_assert( condition, #condition " " #name)

STATIC_ASSERT( sizeof (SPR) == 4, spr_size );
STATIC_ASSERT( sizeof (SPRB) == 4, sprb_size );
STATIC_ASSERT( sizeof (uint32) == 4, uint32_size );

void FCEUI_DisableSpriteLimitation(int a) {
  maxsprites=a?64:8;
}

// I believe this corresponds to the "internal operation" section of
// http://wiki.nesdev.com/w/index.php/PPU_OAM
// where the PPU is looking for sprites for the NEXT scanline.
static uint8 numsprites,SpriteBlurp;
static void FetchSpriteData() {
  int n;
  int vofs;
  uint8 P0=PPU[0];

  SPR *spr=(SPR *)SPRAM;
  uint8 H=8;

  uint8 ns = 0, sb = 0;

  vofs=(unsigned int)(P0&0x8&(((P0&0x20)^0x20)>>2))<<9;
  H+=(P0&0x20)>>2;

  DEBUGF(stderr, "FetchSprites @%d\n", scanline);
  if (!PPU_hook)
    for (n = 63; n >= 0; n--, spr++) {
      if ((unsigned int)(scanline - spr->y) >= H) continue;
      //printf("%d, %u\n",scanline,(unsigned int)(scanline-spr->y));
      if (ns < maxsprites) {
	DEBUGF(stderr, "   sp %2d: %d,%d #%d attr %s\n",
	       n, spr->x, spr->y, spr->no, attrbits(spr->atr));

	if (n==63) sb=1;

	{
	  SPRB dst;
	  uint8 *C;
	  int t = (int)scanline-(spr->y);
	  // made uint32 from uint -tom7
	  uint32 vadr;

	  if (Sprite16)
	    vadr = ((spr->no&1)<<12) + ((spr->no&0xFE)<<4);
	  else
	    vadr = (spr->no<<4)+vofs;

	  if (spr->atr & V_FLIP) {
	    vadr+=7;
	    vadr-=t;
	    vadr+=(P0&0x20)>>1;
	    vadr-=t&8;
	  } else {
	    vadr+=t;
	    vadr+=t&8;
	  }

	  if (MMC5Hack) C = MMC5SPRVRAMADR(vadr);
	  else C = VRAMADR(vadr);

	  dst.ca[0]=C[0];
	  dst.ca[1]=C[8];
	  dst.x=spr->x;
	  dst.atr=spr->atr;

	  {
	    uint32 *dest32 = (uint32 *)&dst;
	    uint32 *sprbuf32 = (uint32 *)&SPRBUF[ns<<2];
	    *sprbuf32=*dest32;
	  }
	}

	ns++;
      } else {
	TRACELOC();
	PPU_status|=0x20;
	break;
      }
    }
  else
    for (n=63;n>=0;n--,spr++) {
      if ((unsigned int)(scanline-spr->y)>=H) continue;

      if (ns<maxsprites) {
	if (n==63) sb=1;

	{
	  SPRB dst;
	  uint8 *C;
	  int t;
	  unsigned int vadr;

	  t = (int)scanline-(spr->y);

	  if (Sprite16)
	    vadr = ((spr->no&1)<<12) + ((spr->no&0xFE)<<4);
	  else
	    vadr = (spr->no<<4)+vofs;

	  if (spr->atr&V_FLIP) {
	    vadr+=7;
	    vadr-=t;
	    vadr+=(P0&0x20)>>1;
	    vadr-=t&8;
	  } else {
	    vadr+=t;
	    vadr+=t&8;
	  }

	  if (MMC5Hack) C = MMC5SPRVRAMADR(vadr);
	  else C = VRAMADR(vadr);
	  dst.ca[0]=C[0];
	  if (ns<8) {
	    PPU_hook(0x2000);
	    PPU_hook(vadr);
	  }
	  dst.ca[1]=C[8];
	  dst.x=spr->x;
	  dst.atr=spr->atr;

	  {
	    uint32 *dst32 = (uint32 *)&dst;
	    uint32 *sprbuf32 = (uint32 *)&SPRBUF[ns<<2];
	    *sprbuf32=*dst32;
	  }
	}

	ns++;
      }
      else {
	TRACELOC();
	PPU_status|=0x20;
	break;
      }
    }
  //if (ns>=7)
  //printf("%d %d\n",scanline,ns);

  //Handle case when >8 sprites per scanline option is enabled.
  if (ns>8) {
    TRACELOC();
    PPU_status|=0x20;
  } else if (PPU_hook) {
    for (n=0;n<(8-ns);n++) {
      PPU_hook(0x2000);
      PPU_hook(vofs);
    }
  }
  numsprites = ns;
  SpriteBlurp = sb;
}

static void RefreshSprites() {
  SPRB *spr;

  any_sprites_on_line=0;
  if (!numsprites) return;

  // Initialize the line buffer to 0x80, meaning "no pixel here."
  FCEU_dwmemset(sprlinebuf,0x80808080,256);
  numsprites--;
  spr = (SPRB*)SPRBUF + numsprites;

  DEBUGF(stderr, "RefreshSprites @%d with numsprites = %d\n",
	 scanline, numsprites);
  for (int n = numsprites; n>=0; n--,spr--) {
    int x = spr->x;
    uint8 *C;
    uint8 *VB;

    // I think the lookup table basically gets the 4 bytes
    // of sprite data for this scanline. Since ppulut2 is
    // ppulut1 shifted up a bit, I think we're getting
    // 2-bit color data from the two planes ca[0] and
    // ca[1], and that's why this is an OR. I don't
    // understand why ca[0] and ca[1] are (can be)
    // different though. 32 bits is 16 pixels, as expected.

    uint32 pixdata = ppulut1[spr->ca[0]] | ppulut2[spr->ca[1]];
    // treat all sprites as checkerboard!
    // uint32 pixdata = (scanline & 1) ? 0xCCCC : 0x3333;
    // uint32 pixdata = 0xFFFF;

    // So then J is like the 1-bit mask of non-zero pixels.
    uint8 J = spr->ca[0] | spr->ca[1];
    // uint8 J = (scanline & 1) ? 0xAA : 0x55;
    // uint8 J = 0xFF;

    uint8 atr = spr->atr;

    DEBUGF(stderr, "   sp %2d: x=%d ca[%d,%d] attr %s\n",
	   n, spr->x, spr->ca[0], spr->ca[1], attrbits(spr->atr));

    if (J) {
      if (n==0 && SpriteBlurp && !(PPU_status&0x40)) {
	sphitx=x;
	sphitdata=J;
	// reverses the mask
	if (atr & H_FLIP)
	  sphitdata = ((J<<7)&0x80) |
	    ((J<<5)&0x40) |
	    ((J<<3)&0x20) |
	    ((J<<1)&0x10) |
	    ((J>>1)&0x08) |
	    ((J>>3)&0x04) |
	    ((J>>5)&0x02) |
	    ((J>>7)&0x01);
      }

      // C is destination for the 8 pixels we'll write
      // on this scanline.
      // C is an array of bytes, each corresponding to
      // a pixel. The bit 0x40 is set if the pixel should
      // show behind the background. The rest of the pixels
      // come from VB (probably just the lowest two?)
      C = sprlinebuf + x;
      // pixdata is abstract color values 0,1,2,3.
      // VB gives us an index into the palette data
      // based on the palette selector in this sprite's
      // attributes.
      VB = (PALRAM+0x10)+((atr&3)<<2);

      // In back or in front of background?
      if (atr & SP_BACK) {
	// back...

	if (atr&H_FLIP) {
	  if (J&0x80) C[7]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x40) C[6]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x20) C[5]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x10) C[4]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x08) C[3]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x04) C[2]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x02) C[1]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x01) C[0]=VB[pixdata]|0x40;
	} else {
	  if (J&0x80) C[0]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x40) C[1]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x20) C[2]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x10) C[3]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x08) C[4]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x04) C[5]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x02) C[6]=VB[pixdata&3]|0x40;
	  pixdata>>=4;
	  if (J&0x01) C[7]=VB[pixdata]|0x40;
	}
      } else {
	if (atr&H_FLIP) {
	  if (J&0x80) C[7]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x40) C[6]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x20) C[5]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x10) C[4]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x08) C[3]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x04) C[2]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x02) C[1]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x01) C[0]=VB[pixdata];
	} else {
	  if (J&0x80) C[0]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x40) C[1]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x20) C[2]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x10) C[3]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x08) C[4]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x04) C[5]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x02) C[6]=VB[pixdata&3];
	  pixdata>>=4;
	  if (J&0x01) C[7]=VB[pixdata];
	}
      }
    }
  }
  SpriteBlurp = 0;
  any_sprites_on_line = 1;
}

// Actually writes sprites to the pixel buffer for a particular scanline.
// target is the beginning of the scanline.
static void CopySprites(uint8 *target) {
  // ends up either 8 or zero. But why?
  uint8 n = ((PPU[1]&4)^4)<<1;
  uint8 *P = target;

  if (!any_sprites_on_line) return;
  any_sprites_on_line=0;

  if (!rendersprites) return;  //User asked to not display sprites.

  // looping until n overflows to 0. This is the whole scanline, I think,
  // 4 pixels at a time.
  do {
    uint32 t=*(uint32 *)(sprlinebuf+n);

    // I think we're testing to see if the pixel should not be drawn
    // because because there's already a sprite drawn there. (bit 0x80).
    // But how does that bit get set?
    // Might come from the VB array above. If there is one there, then
    // we don't copy. If there isn't one, then we look to see if
    // there's a transparent background pixel (has bit 0x40 set)
    if (t!=0x80808080) {

      // t is 4 bytes of pixel data; we do the same thing
      // for each of them.

#if 1 // was ifdef LSB_FIRST!

      if (!(t&0x80)) {
	if (!(t&0x40) || (P[n]&0x40))       // Normal sprite || behind bg sprite
	  P[n]=sprlinebuf[n];
      }

      if (!(t&0x8000)) {
	if (!(t&0x4000) || (P[n+1]&0x40))       // Normal sprite || behind bg sprite
	  P[n+1]=(sprlinebuf+1)[n];
      }

      if (!(t&0x800000)) {
	if (!(t&0x400000) || (P[n+2]&0x40))       // Normal sprite || behind bg sprite
	  P[n+2]=(sprlinebuf+2)[n];
      }

      if (!(t&0x80000000)) {
	if (!(t&0x40000000) || (P[n+3]&0x40))       // Normal sprite || behind bg sprite
	  P[n+3]=(sprlinebuf+3)[n];
      }
#else
# error LSB_FIRST is assumed, because endianness detection is wrong in this compile, sorry

      /* TODO:  Simplify */
      if (!(t&0x80000000)) {
	if (!(t&0x40000000))       // Normal sprite
	  P[n]=sprlinebuf[n];
	else if (P[n]&64)  // behind bg sprite
	  P[n]=sprlinebuf[n];
      }

      if (!(t&0x800000)) {
	if (!(t&0x400000))       // Normal sprite
	  P[n+1]=(sprlinebuf+1)[n];
	else if (P[n+1]&64)  // behind bg sprite
	  P[n+1]=(sprlinebuf+1)[n];
      }

      if (!(t&0x8000)) {
	if (!(t&0x4000))       // Normal sprite
	  P[n+2]=(sprlinebuf+2)[n];
	else if (P[n+2]&64)  // behind bg sprite
	  P[n+2]=(sprlinebuf+2)[n];
      }

      if (!(t&0x80)) {
	if (!(t&0x40))       // Normal sprite
	  P[n+3]=(sprlinebuf+3)[n];
	else if (P[n+3]&64)  // behind bg sprite
	  P[n+3]=(sprlinebuf+3)[n];
      }
#endif
    }
    n +=4 ;
  } while (n);
}

void FCEUPPU_SetVideoSystem(int w) {
  if (w) {
    scanlines_per_frame=312;
    FSettings.FirstSLine=FSettings.UsrFirstSLine[1];
    FSettings.LastSLine=FSettings.UsrLastSLine[1];
  } else {
    scanlines_per_frame=262;
    FSettings.FirstSLine=FSettings.UsrFirstSLine[0];
    FSettings.LastSLine=FSettings.UsrLastSLine[0];
  }
}

//Initializes the PPU
void FCEUPPU_Init() {
  makeppulut();
}

void FCEUPPU_Reset() {
  VRAMBuffer = PPU[0] = PPU[1] = PPU_status = PPU[3] = 0;
  PPUSPL=0;
  PPUGenLatch = 0;
  RefreshAddr = TempAddr = 0;
  vtoggle = 0;
  ppudead = 2;
  kook = 0;
  idleSynch = 1;
  //	XOffset=0;

  ppur.reset();
  spr_read.reset();
}

void FCEUPPU_Power() {
  memset(NTARAM,0x00,0x800);
  memset(PALRAM,0x00,0x20);
  memset(UPALRAM,0x00,0x03);
  memset(SPRAM,0x00,0x100);
  FCEUPPU_Reset();

  for (int x = 0x2000; x < 0x4000; x += 8) {
    ARead[x]=A200x;
    BWrite[x]=B2000;
    ARead[x+1]=A200x;
    BWrite[x+1]=B2001;
    ARead[x+2]=A2002;
    BWrite[x+2]=B2002;
    ARead[x+3]=A200x;
    BWrite[x+3]=B2003;
    ARead[x+4]=A2004; //A2004;
    BWrite[x+4]=B2004;
    ARead[x+5]=A200x;
    BWrite[x+5]=B2005;
    ARead[x+6]=A200x;
    BWrite[x+6]=B2006;
    ARead[x+7]=A2007;
    BWrite[x+7]=B2007;
  }
  BWrite[0x4014]=B4014;
}

int FCEUPPU_Loop(int skip) {
  // Needed for Knight Rider, possibly others.
  if (ppudead) {
    memset(XBuf, 0x80, 256*240);
    X6502_Run(scanlines_per_frame*(256+85));
    ppudead--;
  } else {
    TRACELOC();
    X6502_Run(256+85);
    TRACEA(RAM, 0x800);

    PPU_status |= 0x80;

    // Not sure if this is correct.
    // According to Matt Conte and my own tests, it is.
    // Timing is probably off, though.
    // NOTE:  Not having this here breaks a Super Donkey Kong game.
    PPU[3]=PPUSPL=0;

    // I need to figure out the true nature and length of this delay.
    X6502_Run(12);

    if (VBlankON)
      TriggerNMI();

    X6502_Run((scanlines_per_frame-242)*(256+85)-12);
    PPU_status&=0x1f;
    X6502_Run(256);

    {
      if (ScreenON || SpriteON) {
	if (GameHBIRQHook && ((PPU[0]&0x38)!=0x18))
	  GameHBIRQHook();
	if (PPU_hook)
	  for (int x=0;x<42;x++) {PPU_hook(0x2000); PPU_hook(0);}
	if (GameHBIRQHook2)
	  GameHBIRQHook2();
      }
      X6502_Run(85-16);
      if (ScreenON || SpriteON) {
	RefreshAddr=TempAddr;
	if (PPU_hook) PPU_hook(RefreshAddr&0x3fff);
      }

      //Clean this stuff up later.
      any_sprites_on_line = numsprites = 0;
      ResetRL(XBuf);

      X6502_Run(16-kook);
      kook ^= 1;
    }

    // n.b. FRAMESKIP results in different behavior in memory, so don't do it.
    if (0) { /* used to be nsf playing code here -tom7 */ }
#ifdef FRAMESKIP
    else if (skip) {
      int y;

      y=SPRAM[0];
      y++;

      TRACELOC();
      PPU_status|=0x20;       // Fixes "Bee 52".  Does it break anything?
      if (GameHBIRQHook) {
	X6502_Run(256);
	for (scanline=0;scanline<240;scanline++) {
	  if (ScreenON || SpriteON)
	    GameHBIRQHook();
	  if (scanline==y && SpriteON) {
	    TRACELOC();
	    PPU_status|=0x40;
	  }
	  X6502_Run((scanline==239)?85:(256+85));
	}
      } else if (y<240) {
	X6502_Run((256+85)*y);
	if (SpriteON) {
	  TRACELOC();
	  PPU_status|=0x40; // Quick and very dirty hack.
	}
	X6502_Run((256+85)*(240-y));
      } else {
	X6502_Run((256+85)*240);
      }
    }
#endif
    else {
      deemp=PPU[1]>>5;
      for (scanline=0;scanline<240;) {
	//scanline is incremented in  DoLine.  Evil. :/
	deempcnt[deemp]++;
	DoLine();
      }

      if (MMC5Hack && (ScreenON || SpriteON)) MMC5_hb(scanline);
      int max = 0, maxref = 0;
      for (int x = 0; x < 7; x++) {

	if (deempcnt[x]>max) {
	  max=deempcnt[x];
	  maxref=x;
	}
	deempcnt[x]=0;
      }
      //FCEU_DispMessage("%2x:%2x:%2x:%2x:%2x:%2x:%2x:%2x %d",0,deempcnt[0],deempcnt[1],deempcnt[2],deempcnt[3],deempcnt[4],deempcnt[5],deempcnt[6],deempcnt[7],maxref);
      //memset(deempcnt,0,sizeof(deempcnt));
      fceulib__palette.SetNESDeemph(maxref,0);
    }
  } //else... to if (ppudead)

#ifdef FRAMESKIP
  return !skip;
#else
  return 1;
#endif
}

int (*PPU_MASTER)(int skip) = FCEUPPU_Loop;

static uint16 TempAddrT,RefreshAddrT;

void FCEUPPU_LoadState(int version) {
  TempAddr=TempAddrT;
  RefreshAddr=RefreshAddrT;
}

const SFORMAT FCEUPPU_STATEINFO[] = {
  { NTARAM, 0x800, "NTAR"},
  { PALRAM, 0x20, "PRAM"},
  { SPRAM, 0x100, "SPRA"},
  { PPU, 0x4, "PPUR"},
  { &kook, 1, "KOOK"},
  { &ppudead, 1, "DEAD"},
  { &PPUSPL, 1, "PSPL"},
  { &XOffset, 1, "XOFF"},
  { &vtoggle, 1, "VTOG"},
  { &RefreshAddrT, 2|FCEUSTATE_RLSB, "RADD"},
  { &TempAddrT, 2|FCEUSTATE_RLSB, "TADD"},
  { &VRAMBuffer, 1, "VBUF"},
  { &PPUGenLatch, 1, "PGEN"},
  { &pshift[0], 4|FCEUSTATE_RLSB, "PSH1" },
  { &pshift[1], 4|FCEUSTATE_RLSB, "PSH2" },
  { &sphitx, 4|FCEUSTATE_RLSB, "Psph" },
  { &sphitdata, 1, "Pspd" },
  { 0 }
};

// TODO: PERF: Can avoid saving new ppu state! -tom7
// XXX deleteme.
const SFORMAT FCEU_NEWPPU_STATEINFO[] = {
  { &idleSynch, 1, "IDLS" },
  { &spr_read.num, 4|FCEUSTATE_RLSB, "SR_0" },
  { &spr_read.count, 4|FCEUSTATE_RLSB, "SR_1" },
  { &spr_read.fetch, 4|FCEUSTATE_RLSB, "SR_2" },
  { &spr_read.found, 4|FCEUSTATE_RLSB, "SR_3" },
  { &spr_read.found_pos[0], 4|FCEUSTATE_RLSB, "SRx0" },
  { &spr_read.found_pos[0], 4|FCEUSTATE_RLSB, "SRx1" },
  { &spr_read.found_pos[0], 4|FCEUSTATE_RLSB, "SRx2" },
  { &spr_read.found_pos[0], 4|FCEUSTATE_RLSB, "SRx3" },
  { &spr_read.found_pos[0], 4|FCEUSTATE_RLSB, "SRx4" },
  { &spr_read.found_pos[0], 4|FCEUSTATE_RLSB, "SRx5" },
  { &spr_read.found_pos[0], 4|FCEUSTATE_RLSB, "SRx6" },
  { &spr_read.found_pos[0], 4|FCEUSTATE_RLSB, "SRx7" },
  { &spr_read.ret, 4|FCEUSTATE_RLSB, "SR_4" },
  { &spr_read.last, 4|FCEUSTATE_RLSB, "SR_5" },
  { &spr_read.mode, 4|FCEUSTATE_RLSB, "SR_6" },
  { &ppur.fv, 4|FCEUSTATE_RLSB, "PFVx" },
  { &ppur.v, 4|FCEUSTATE_RLSB, "PVxx" },
  { &ppur.h, 4|FCEUSTATE_RLSB, "PHxx" },
  { &ppur.vt, 4|FCEUSTATE_RLSB, "PVTx" },
  { &ppur.ht, 4|FCEUSTATE_RLSB, "PHTx" },
  { &ppur._fv, 4|FCEUSTATE_RLSB, "P_FV" },
  { &ppur._v, 4|FCEUSTATE_RLSB, "P_Vx" },
  { &ppur._h, 4|FCEUSTATE_RLSB, "P_Hx" },
  { &ppur._vt, 4|FCEUSTATE_RLSB, "P_VT" },
  { &ppur._ht, 4|FCEUSTATE_RLSB, "P_HT" },
  { &ppur.fh, 4|FCEUSTATE_RLSB, "PFHx" },
  { &ppur.s, 4|FCEUSTATE_RLSB, "PSxx" },
  { &ppur.status.sl, 4|FCEUSTATE_RLSB, "PST0" },
  { &ppur.status.cycle, 4|FCEUSTATE_RLSB, "PST1" },
  { &ppur.status.end_cycle, 4|FCEUSTATE_RLSB, "PST2" },
  { 0 }
};

void FCEUPPU_SaveState() {
  TempAddrT = TempAddr;
  RefreshAddrT = RefreshAddr;
}

// probably can go:
//  ppuphase