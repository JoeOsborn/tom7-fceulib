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

#define A64reg GMB_mapbyte1(fc)[0]
#define A64wr GMB_mapbyte1(fc)[1]

static DECLFW(Mapper46_writel) {
  A64reg = V;
  fc->ines->ROM_BANK32((A64wr & 1) + ((A64reg & 0xF) << 1));
  fc->ines->VROM_BANK8(((A64wr >> 4) & 7) + ((A64reg & 0xF0) >> 1));
}

static DECLFW(Mapper46_write) {
  A64wr = V;
  fc->ines->ROM_BANK32((V & 1) + ((A64reg & 0xF) << 1));
  fc->ines->VROM_BANK8(((V >> 4) & 7) + ((A64reg & 0xF0) >> 1));
}

MapInterface *Mapper46_init(FC *fc) {
  fc->ines->MIRROR_SET(0);
  fc->ines->ROM_BANK32(0);
  fc->fceu->SetWriteHandler(0x8000, 0xffff, Mapper46_write);
  fc->fceu->SetWriteHandler(0x6000, 0x7fff, Mapper46_writel);
  return new MapInterface(fc);
}
