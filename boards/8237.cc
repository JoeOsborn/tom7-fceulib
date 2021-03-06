/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2005-2011 CaH4e3
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
 *
 * Super Game (Sugar Softec) protected mapper
 * Pocahontas 2 (Unl) [U][!], etc.
 * TODO: 9in1 LION KING HANGS!
 */

#include "mapinc.h"
#include "mmc3.h"

static constexpr uint8 regperm[8][8] = {
  {0, 1, 2, 3, 4, 5, 6, 7},
  {0, 2, 6, 1, 7, 3, 4, 5},
  {0, 5, 4, 1, 7, 2, 6, 3},  // unused
  {0, 6, 3, 7, 5, 2, 4, 1},
  {0, 2, 5, 3, 6, 1, 7, 4},
  {0, 1, 2, 3, 4, 5, 6, 7},  // empty
  {0, 1, 2, 3, 4, 5, 6, 7},  // empty
  {0, 1, 2, 3, 4, 5, 6, 7},  // empty
};

static constexpr uint8 adrperm[8][8] = {
  {0, 1, 2, 3, 4, 5, 6, 7},
  {3, 2, 0, 4, 1, 5, 6, 7},
  {0, 1, 2, 3, 4, 5, 6, 7},  // unused
  {5, 0, 1, 2, 3, 7, 6, 4},
  {3, 1, 0, 5, 2, 4, 6, 7},
  {0, 1, 2, 3, 4, 5, 6, 7},  // empty
  {0, 1, 2, 3, 4, 5, 6, 7},  // empty
  {0, 1, 2, 3, 4, 5, 6, 7},  // empty
};

namespace {
struct UNL8237 : public MMC3 {
  uint8 EXPREGS[8] = {};

  uint8 cmdin = 0;

  void CWrap(uint32 A, uint8 V) override {
    if (EXPREGS[0] & 0x40)
      fc->cart->setchr1(A,
                        ((EXPREGS[1] & 0xc) << 6) | (V & 0x7F) |
                        ((EXPREGS[1] & 0x20) << 2));
    else
      fc->cart->setchr1(A, ((EXPREGS[1] & 0xc) << 6) | V);
  }

  void PWrap(uint32 A, uint8 V) override {
    if (EXPREGS[0] & 0x40) {
      uint8 sbank = (EXPREGS[1] & 0x10);
      if (EXPREGS[0] & 0x80) {
        uint8 bank = ((EXPREGS[1] & 3) << 4) |
          (EXPREGS[0] & 0x7) | (sbank >> 1);
        if (EXPREGS[0] & 0x20)
          fc->cart->setprg32(0x8000, bank >> 1);
        else {
          fc->cart->setprg16(0x8000, bank);
          fc->cart->setprg16(0xC000, bank);
        }
      } else
        fc->cart->setprg8(A, ((EXPREGS[1] & 3) << 5) | (V & 0x0F) | sbank);
    } else {
      if (EXPREGS[0] & 0x80) {
        uint8 bank = ((EXPREGS[1] & 3) << 4) | (EXPREGS[0] & 0xF);
        if (EXPREGS[0] & 0x20)
          fc->cart->setprg32(0x8000, bank >> 1);
        else {
          fc->cart->setprg16(0x8000, bank);
          fc->cart->setprg16(0xC000, bank);
        }
      } else
        fc->cart->setprg8(A, ((EXPREGS[1] & 3) << 5) | (V & 0x1F));
    }
  }

  void UNL8237Write(DECLFW_ARGS) {
    uint8 dat = V;
    uint8 adr = adrperm[EXPREGS[2]][((A >> 12) & 6) | (A & 1)];
    uint16 addr = (adr & 1) | ((adr & 6) << 12) | 0x8000;
    if (adr < 4) {
      if (!adr) dat = (dat & 0xC0) | (regperm[EXPREGS[2]][dat & 7]);
      MMC3_CMDWrite(fc, addr, dat);
    } else {
      MMC3_IRQWrite(fc, addr, dat);
    }
  }

  void UNL8237ExWrite(DECLFW_ARGS) {
    switch (A) {
      case 0x5000:
        EXPREGS[0] = V;
        FixMMC3PRG(MMC3_cmd);
        break;
      case 0x5001:
        EXPREGS[1] = V;
        FixMMC3PRG(MMC3_cmd);
        FixMMC3CHR(MMC3_cmd);
        break;
      case 0x5007: EXPREGS[2] = V; break;
    }
  }

  void Power() final override {
    EXPREGS[0] = EXPREGS[2] = 0;
    EXPREGS[1] = 3;
    MMC3::Power();
    fc->fceu->SetWriteHandler(0x8000, 0xFFFF, [](DECLFW_ARGS) {
      ((UNL8237*)fc->fceu->cartiface)->UNL8237Write(DECLFW_FORWARD);
    });
    fc->fceu->SetWriteHandler(0x5000, 0x7FFF, [](DECLFW_ARGS) {
      ((UNL8237*)fc->fceu->cartiface)->UNL8237ExWrite(DECLFW_FORWARD);
    });
  }

  UNL8237(FC *fc, CartInfo *info) : MMC3(fc, info, 256, 256, 0, 0) {
    fc->state->AddExState(EXPREGS, 3, 0, "EXPR");
    fc->state->AddExState(&cmdin, 1, 0, "CMDI");
  }
};

struct UNL8237A final : public UNL8237 {
  using UNL8237::UNL8237;

  void CWrap(uint32 A, uint8 V) final override {
    if (EXPREGS[0] & 0x40)
      fc->cart->setchr1(A,
                        ((EXPREGS[1] & 0xE) << 7) |
                        (V & 0x7F) |
                        ((EXPREGS[1] & 0x20) << 2));
    else
      fc->cart->setchr1(A, ((EXPREGS[1] & 0xE) << 7) | V);
  }

  void PWrap(uint32 A, uint8 V) final override {
    if (EXPREGS[0] & 0x40) {
      uint8 sbank = (EXPREGS[1] & 0x10);
      if (EXPREGS[0] & 0x80) {
        uint8 bank = ((EXPREGS[1] & 3) << 4) | ((EXPREGS[1] & 8) << 3) |
          (EXPREGS[0] & 0x7) | (sbank >> 1);
        if (EXPREGS[0] & 0x20)
          fc->cart->setprg32(0x8000, bank >> 1);
        else {
          fc->cart->setprg16(0x8000, bank);
          fc->cart->setprg16(0xC000, bank);
        }
      } else
        fc->cart->setprg8(A, ((EXPREGS[1] & 3) << 5) |
                          ((EXPREGS[1] & 8) << 4) | (V & 0x0F) |
                          sbank);
    } else {
      if (EXPREGS[0] & 0x80) {
        uint8 bank = ((EXPREGS[1] & 3) << 4) | ((EXPREGS[1] & 8) << 3) |
          (EXPREGS[0] & 0xF);
        if (EXPREGS[0] & 0x20)
          fc->cart->setprg32(0x8000, bank >> 1);
        else {
          fc->cart->setprg16(0x8000, bank);
          fc->cart->setprg16(0xC000, bank);
        }
      } else
        fc->cart->setprg8(A,
                          ((EXPREGS[1] & 3) << 5) |
                          ((EXPREGS[1] & 8) << 4) | (V & 0x1F));
    }
  }
};
}

CartInterface *UNL8237_Init(FC *fc, CartInfo *info) {
  return new UNL8237(fc, info);
}

CartInterface *UNL8237A_Init(FC *fc, CartInfo *info) {
  return new UNL8237A(fc, info);
}
