/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel
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

// Fairly exotic mapper used by a Front FarEast Famicom disk image copier.
// -tom7

namespace {
struct Mapper6 final : public MapInterface {
  using MapInterface::MapInterface;
  uint8 FFEmode = 0;

  void FVRAM_BANK8(uint32 A, uint32 V) {
    uint8 *addr =
      V ? &GMB_MapperExRAM(fc)[V << 13] : &GMB_CHRRAM(fc)[V << 13];
    for (int i = 0; i < 8; i++)
      fc->cart->SetSpecificVPage(i, A, addr);

    fc->ines->iNESCHRBankList[0] = (V << 3);
    fc->ines->iNESCHRBankList[1] = (V << 3) + 1;
    fc->ines->iNESCHRBankList[2] = (V << 3) + 2;
    fc->ines->iNESCHRBankList[3] = (V << 3) + 3;
    fc->ines->iNESCHRBankList[4] = (V << 3) + 4;
    fc->ines->iNESCHRBankList[5] = (V << 3) + 5;
    fc->ines->iNESCHRBankList[6] = (V << 3) + 6;
    fc->ines->iNESCHRBankList[7] = (V << 3) + 7;
    fc->ppu->PPUCHRRAM = 0xFF;
  }

  void FFEIRQHook(int a) {
    if (fc->ines->iNESIRQa) {
      fc->ines->iNESIRQCount += a;
      if (fc->ines->iNESIRQCount >= 0x10000) {
        fc->X->IRQBegin(FCEU_IQEXT);
        fc->ines->iNESIRQa = 0;
        fc->ines->iNESIRQCount = 0;
      }
    }
  }

  void Mapper6_write(DECLFW_ARGS) {
    if (A < 0x8000) {
      switch (A) {
      case 0x42FF: fc->ines->MIRROR_SET((V >> 4) & 1); break;
      case 0x42FE:
        fc->ines->onemir((V >> 3) & 2);
        FFEmode = V & 0x80;
        break;
      case 0x4501:
        fc->ines->iNESIRQa = 0;
        fc->X->IRQEnd(FCEU_IQEXT);
        break;
      case 0x4502:
        fc->ines->iNESIRQCount &= 0xFF00;
        fc->ines->iNESIRQCount |= V;
        break;
      case 0x4503:
        fc->ines->iNESIRQCount &= 0xFF;
        fc->ines->iNESIRQCount |= V << 8;
        fc->ines->iNESIRQa = 1;
        break;
      }
    } else {
      switch (FFEmode) {
      case 0x80: fc->cart->setchr8(V); break;
      default:
        fc->ines->ROM_BANK16(0x8000, V >> 2);
        FVRAM_BANK8(0x0000, V & 3);
      }
    }
  }

  void StateRestore(int version) final override {
    for (int x = 0; x < 8; x++) {
      if (fc->ppu->PPUCHRRAM & (1 << x)) {
        if (fc->ines->iNESCHRBankList[x] > 7) {
	  fc->cart->SetVPage(
	      x << 10,
	      &GMB_MapperExRAM(fc)[(fc->ines->iNESCHRBankList[x] & 31) << 10]);
        } else {
	  fc->cart->SetVPage(
	      x << 10,
	      &GMB_CHRRAM(fc)[(fc->ines->iNESCHRBankList[x] & 7) << 10]);
        }
      }
    }
  }
};
}

MapInterface *Mapper6_init(FC *fc) {
  MapInterface *m = new Mapper6(fc);
  fc->X->MapIRQHook = [](FC *fc, int a) {
    ((Mapper6*)fc->fceu->mapiface)->FFEIRQHook(a);
  };

  // XXX Note that mapiface has not yet been installed, so if
  // calls like these try to make calls back into mapper code,
  // they will fail. I don't think they do. -tom7
  fc->ines->ROM_BANK16(0xc000, 7);

  fc->fceu->SetWriteHandler(0x4020, 0x5fff, [](DECLFW_ARGS) {
    ((Mapper6*)fc->fceu->mapiface)->Mapper6_write(DECLFW_FORWARD);
  });
  fc->fceu->SetWriteHandler(0x8000, 0xffff, [](DECLFW_ARGS) {
    ((Mapper6*)fc->fceu->mapiface)->Mapper6_write(DECLFW_FORWARD);
  });
  return m;
}
