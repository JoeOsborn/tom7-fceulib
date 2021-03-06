/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2009 CaH4e3
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

static constexpr uint32 WRAMSIZE = 8192;

// FIXME: 10/28 - now implemented in SDL as well.
// should we rename this to a FCEUI_* function?
unsigned int *GetKeyboard();

namespace {
struct Transformer final : public CartInterface {
  uint8 *WRAM = nullptr;

  unsigned int *TransformerKeys = nullptr, oldkeys[256] = {};
  int TransformerCycleCount = 0, TransformerChar = 0;

  void TransformerIRQHook(int a) {
    TransformerCycleCount += a;
    if (TransformerCycleCount >= 1000) {
      TransformerCycleCount -= 1000;
      TransformerKeys = GetKeyboard();

      for (uint32 i = 0; i < 256; i++) {
        if (oldkeys[i] != TransformerKeys[i]) {
          if (oldkeys[i] == 0)
            TransformerChar = i;
          else
            TransformerChar = i | 0x80;
          fc->X->IRQBegin(FCEU_IQEXT);
          memcpy((void *)&oldkeys[0], (void *)TransformerKeys, 256);
          break;
        }
      }
    }
  }

  DECLFR_RET TransformerRead(DECLFR_ARGS) {
    uint8 ret = 0;
    switch (A & 3) {
      case 0: ret = TransformerChar & 15; break;
      case 1: ret = TransformerChar >> 4; break;
      case 2: break;
      case 4: break;
    }
    fc->X->IRQEnd(FCEU_IQEXT);
    return ret;
  }

  void Power() final override {
    fc->cart->setprg8r(0x10, 0x6000, 0);
    fc->cart->setprg16(0x8000, 0);
    fc->cart->setprg16(0xC000, ~0);
    fc->cart->setchr8(0);

    fc->fceu->SetReadHandler(0x5000, 0x5004, [](DECLFR_ARGS) {
      return ((Transformer*)fc->fceu->cartiface)->
        TransformerRead(DECLFR_FORWARD);
    });
    fc->fceu->SetReadHandler(0x6000, 0x7FFF, Cart::CartBR);
    fc->fceu->SetWriteHandler(0x6000, 0x7FFF, Cart::CartBW);
    fc->fceu->SetReadHandler(0x8000, 0xFFFF, Cart::CartBR);

    fc->X->MapIRQHook = [](FC *fc, int a) {
      ((Transformer *)fc->fceu->cartiface)->TransformerIRQHook(a);
    };
  }

  void Close() final override {
    free(WRAM);
    WRAM = nullptr;
  }

  Transformer(FC *fc, CartInfo *info) : CartInterface(fc) {
    WRAM = (uint8 *)FCEU_gmalloc(WRAMSIZE);
    fc->cart->SetupCartPRGMapping(0x10, WRAM, WRAMSIZE, true);
    if (info->battery) {
      info->SaveGame[0] = WRAM;
      info->SaveGameLen[0] = WRAMSIZE;
    }
    fc->state->AddExState(WRAM, WRAMSIZE, 0, "WRAM");
  }

};
}

CartInterface *Transformer_Init(FC *fc, CartInfo *info) {
  return new Transformer(fc, info);
}
