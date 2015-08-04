/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2007 CaH4e3
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "mapinc.h"

static uint8 reg;

static uint8 *WRAM = nullptr;
static uint32 WRAMSIZE;

static vector<SFORMAT> StateRegs = {{&reg, 1, "REGS"}};

static void Sync() {
  fceulib__.cart->setchr8(0);
  fceulib__.cart->setprg8r(0x10, 0x6000, 0);
  fceulib__.cart->setprg32(0x8000, reg & 0x1f);
  fceulib__.cart->setmirror(((reg & 0x20) >> 5) ^ 1);
}

static DECLFW(M177Write) {
  reg = V;
  Sync();
}

static void M177Power(FC *fc) {
  reg = 0;
  Sync();
  fceulib__.fceu->SetReadHandler(0x6000, 0x7fff, Cart::CartBR);
  fceulib__.fceu->SetWriteHandler(0x6000, 0x7fff, Cart::CartBW);
  fceulib__.fceu->SetReadHandler(0x8000, 0xFFFF, Cart::CartBR);
  fceulib__.fceu->SetWriteHandler(0x8000, 0xFFFF, M177Write);
}

static void M177Close(FC *fc) {
  free(WRAM);
  WRAM = nullptr;
}

static void StateRestore(FC *fc, int version) {
  Sync();
}

void Mapper177_Init(CartInfo *info) {
  info->Power = M177Power;
  info->Close = M177Close;
  fceulib__.fceu->GameStateRestore = StateRestore;

  WRAMSIZE = 8192;
  WRAM = (uint8 *)FCEU_gmalloc(WRAMSIZE);
  fceulib__.cart->SetupCartPRGMapping(0x10, WRAM, WRAMSIZE, 1);
  fceulib__.state->AddExState(WRAM, WRAMSIZE, 0, "WRAM");
  if (info->battery) {
    info->SaveGame[0] = WRAM;
    info->SaveGameLen[0] = WRAMSIZE;
  }

  fceulib__.state->AddExVec(StateRegs);
}
