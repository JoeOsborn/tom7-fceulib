/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2006 CaH4e3
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

#include "mapinc.h"

static uint8 chr_reg[4];
static uint8 kogame, prg_reg, nt1, nt2, mirr;

static uint8 *WRAM=NULL;
static uint32 WRAMSIZE, count;

static SFORMAT StateRegs[]=
{
  {&nt1, 1, "NT1"},
  {&nt2, 1, "NT2"},
  {&mirr, 1, "MIRR"},
  {&prg_reg, 1, "PRG"},
  {&kogame, 1, "KGME"},
  {&count, 4, "CNT"},
  {chr_reg, 4, "CHR"},
  {0}
};

static void M68NTfix(void) {
  if((!fceulib__unif.UNIFchrrama)&&(mirr&0x10)) {
    fceulib__ppu.PPUNTARAM = 0;
    switch(mirr&3) {
     case 0: fceulib__ppu.vnapage[0]=fceulib__ppu.vnapage[2]=fceulib__cart.CHRptr[0]+(((nt1|128)&fceulib__cart.CHRmask1[0])<<10);
             fceulib__ppu.vnapage[1]=fceulib__ppu.vnapage[3]=fceulib__cart.CHRptr[0]+(((nt2|128)&fceulib__cart.CHRmask1[0])<<10);
             break;
     case 1: fceulib__ppu.vnapage[0]=fceulib__ppu.vnapage[1]=fceulib__cart.CHRptr[0]+(((nt1|128)&fceulib__cart.CHRmask1[0])<<10);
             fceulib__ppu.vnapage[2]=fceulib__ppu.vnapage[3]=fceulib__cart.CHRptr[0]+(((nt2|128)&fceulib__cart.CHRmask1[0])<<10);
             break;
     case 2: fceulib__ppu.vnapage[0]=fceulib__ppu.vnapage[1]=fceulib__ppu.vnapage[2]=fceulib__ppu.vnapage[3]=fceulib__cart.CHRptr[0]+(((nt1|128)&fceulib__cart.CHRmask1[0])<<10);
             break;
     case 3: fceulib__ppu.vnapage[0]=fceulib__ppu.vnapage[1]=fceulib__ppu.vnapage[2]=fceulib__ppu.vnapage[3]=fceulib__cart.CHRptr[0]+(((nt2|128)&fceulib__cart.CHRmask1[0])<<10);
             break;
    }
  }
  else
    switch(mirr&3)
    {
      case 0: fceulib__cart.setmirror(MI_V); break;
      case 1: fceulib__cart.setmirror(MI_H); break;
      case 2: fceulib__cart.setmirror(MI_0); break;
      case 3: fceulib__cart.setmirror(MI_1); break;
    }
}

static void Sync(void)
{
  fceulib__cart.setchr2(0x0000,chr_reg[0]);
  fceulib__cart.setchr2(0x0800,chr_reg[1]);
  fceulib__cart.setchr2(0x1000,chr_reg[2]);
  fceulib__cart.setchr2(0x1800,chr_reg[3]);
  fceulib__cart.setprg8r(0x10,0x6000,0);
  fceulib__cart.setprg16r((fceulib__cart.PRGptr[1])?kogame:0,0x8000,prg_reg);
  fceulib__cart.setprg16(0xC000,~0);
}

static DECLFR(M68Read)
{
  if(!(kogame&8))
  {
    count++;
    if(count==1784)
      fceulib__cart.setprg16r(0,0x8000,prg_reg);
  }
  return Cart::CartBR(A);
}

static DECLFW(M68WriteLo)
{
  if(!V)
  {
    count = 0;
    fceulib__cart.setprg16r((fceulib__cart.PRGptr[1])?kogame:0,0x8000,prg_reg);
  }
}

static DECLFW(M68WriteCHR)
{
  chr_reg[(A>>12)&3]=V;
  Sync();
}

static DECLFW(M68WriteNT1)
{
  nt1 = V;
  M68NTfix();
}

static DECLFW(M68WriteNT2)
{
  nt2 = V;
  M68NTfix();
}

static DECLFW(M68WriteMIR)
{
  mirr = V;
  M68NTfix();
}

static DECLFW(M68WriteROM)
{
  prg_reg = V&7;
  kogame = ((V>>3)&1)^1;
  Sync();
}

static void M68Power(void)
{
  prg_reg = 0;
  kogame = 0;
  Sync();
  M68NTfix();
  fceulib__fceu.SetReadHandler(0x6000,0x7FFF,Cart::CartBR);
  fceulib__fceu.SetReadHandler(0x8000,0xBFFF,M68Read);
  fceulib__fceu.SetReadHandler(0xC000,0xFFFF,Cart::CartBR);
  fceulib__fceu.SetWriteHandler(0x8000,0xBFFF,M68WriteCHR);
  fceulib__fceu.SetWriteHandler(0xC000,0xCFFF,M68WriteNT1);
  fceulib__fceu.SetWriteHandler(0xD000,0xDFFF,M68WriteNT2);
  fceulib__fceu.SetWriteHandler(0xE000,0xEFFF,M68WriteMIR);
  fceulib__fceu.SetWriteHandler(0xF000,0xFFFF,M68WriteROM);
  fceulib__fceu.SetWriteHandler(0x6000,0x6000,M68WriteLo);
  fceulib__fceu.SetWriteHandler(0x6001,0x7FFF,Cart::CartBW);
}

static void M68Close(void)
{
  if(WRAM)
    free(WRAM);
  WRAM=NULL;
}

static void StateRestore(int version)
{
  Sync();
  M68NTfix();
}

void Mapper68_Init(CartInfo *info) {
  info->Power=M68Power;
  info->Close=M68Close;
  fceulib__fceu.GameStateRestore=StateRestore;
  WRAMSIZE=8192;
  WRAM=(uint8*)FCEU_gmalloc(WRAMSIZE);
  fceulib__cart.SetupCartPRGMapping(0x10,WRAM,WRAMSIZE,1);
  if(info->battery) {
    info->SaveGame[0]=WRAM;
    info->SaveGameLen[0]=WRAMSIZE;
  }
  AddExState(WRAM, WRAMSIZE, 0, "WRAM");
  AddExState(&StateRegs, ~0, 0, 0);
}
