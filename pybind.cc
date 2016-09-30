#include <pybind11/pybind11.h>
#include <pybind11/stl_bind.h>

#include "emulator.h"
#include "input.h"
#include "palette.h"
#include "cart.h"
#include "ppu.h"
#include "fceu.h"
#include "simplefm2.h"

int add(int i, int j) {
    return i + j;
}

namespace py = pybind11;

PYBIND11_MAKE_OPAQUE(std::vector<uint8>);
PYBIND11_MAKE_OPAQUE(std::vector<int16>);

PYBIND11_MAKE_OPAQUE(uint8*);

struct SharedByteArray {
public:
  //TODO: a convenience for copying this would be nice.

  SharedByteArray(uint8 const ptr[], size_t sz = 0) :
    ptr(const_cast<uint8*>(ptr)),
    sz(sz)
  {};
  SharedByteArray(uint8* ptr, size_t sz = 0) :
    ptr(ptr),
    sz(sz)
  {};
  
  uint8 operator[](size_t index) const {
    return ptr[index];
  }
  
  uint8 &operator[](size_t index) {
    return ptr[index];
  }

  size_t safeSize() const { return sz; }

  const uint8* begin() const { return ptr; }
  const uint8* end() const { return ptr+sz; }

  uint8* ptr;
private:
  size_t sz;
};

//ugh, macros, but I tried my best with templates.
#define SBA(cls,fld,len) ([](const cls*___o) { return SharedByteArray(___o->fld, len); })
#define SBA_(cls,fld) SBA(cls,fld,0)

PYBIND11_PLUGIN(fceulib) {
    py::module m("fceulib", "Python wrapper for fceulib NES emulation");

    py::bind_vector<std::vector<uint8>>(m, "VectorBytes");
    py::bind_vector<std::vector<int16>>(m, "VectorShorts");

    py::class_<SharedByteArray>(m, "BytePointer")
      /// Bare bones interface
       .def("__getitem__", [](const SharedByteArray &s, size_t i) {
           size_t safeSz = s.safeSize();
           if (safeSz && i >= safeSz)
             throw py::index_error();
           return s[i];
        })
      .def("__setitem__", [](SharedByteArray &s, size_t i, float v) {
           size_t safeSz = s.safeSize();
           if (safeSz && i >= safeSz)
                throw py::index_error();
            s[i] = v;
        })
       .def("__len__", &SharedByteArray::safeSize)
      //TODO: get a slice into a Python bytearray
      //.def("set", &SharedByteArray::set)
      //TODO: buffer interface for converting _from_, at least.
      ;
       

    // m.attr("JOY_A") = py::int_(JOY_A);
    // m.attr("JOY_B") = py::int_(JOY_B);
    // m.attr("JOY_SELECT") = py::int_(JOY_SELECT);
    // m.attr("JOY_START") = py::int_(JOY_START);
    // m.attr("JOY_UP") = py::int_(JOY_UP);
    // m.attr("JOY_DOWN") = py::int_(JOY_DOWN);
    // m.attr("JOY_LEFT") = py::int_(JOY_LEFT);
    // m.attr("JOY_RIGHT") = py::int_(JOY_RIGHT);

    m.def("runGame", &Emulator::Create);
    py::class_<Emulator>(m, "Emulator")
      .def("save", &Emulator::Save)
      .def("load", &Emulator::Load)
      .def("step", &Emulator::Step, "Bits for P1 and P2 input from most to least significant are R, L, D, U, Start, Select, B, A. Does not yield video.")
      .def("memoryInto", (void (Emulator::*)(vector<uint8>*)) &Emulator::GetMemory, "Get memory out into a buffer.")
      .def_property_readonly("memory", (vector<uint8> (Emulator::*)(void)) &Emulator::GetMemory, "Copy and return system memory.")
      .def("stepFull", &Emulator::StepFull)
      .def("imageInto", (void (Emulator::*)(vector<uint8>*) const) &Emulator::GetImage, "Get image out into a buffer.")
      .def_property_readonly("image", (vector<uint8> (Emulator::*)(void) const) &Emulator::GetImage, "Copy and return current image.")
      .def("imageARGBInto", (void (Emulator::*)(vector<uint8>*) const) &Emulator::GetImageARGB, "Get ARGB image out into a buffer.")
      .def_property_readonly("imageARGB", (vector<uint8> (Emulator::*)(void) const) &Emulator::GetImageARGB, "Copy and return current ARGB image.")
      .def("getSound", (void (Emulator::*)(vector<int16>*)) &Emulator::GetSound, "Get current sound into a buffer.")
      .def_property_readonly("ramChecksum", &Emulator::RamChecksum, "Return checksum of RAM.")
      .def_property_readonly("imageChecksum", &Emulator::ImageChecksum, "Return checksum of displayed image.")
      .def_property_readonly("cpuStateChecksum", &Emulator::CPUStateChecksum, "Return checksum of CPU state.")
      .def("getBasis", &Emulator::GetBasis, "Get basis vector for savestates into a given buffer.")
      .def("saveUncompressedInto", (void (Emulator::*)(vector<uint8>*)) &Emulator::SaveUncompressed, "Get savestate out into a given buffer.")
      .def("saveUncompressed", (vector<uint8> (Emulator::*)(void)) &Emulator::SaveUncompressed, "Copy savestate out into a fresh buffer")
      .def("loadUncompressed", &Emulator::LoadUncompressed, "Load a savestate.")
      .def("saveExInto", &Emulator::SaveEx, "Save a state with a given basis vector.")
      .def("loadExInto", &Emulator::LoadEx, "Load a state with a given basis vector.")
      .def_property_readonly("xScroll", &Emulator::GetXScroll, "Get X scroll offset from PPU, but this may not always be accurate.")
      // .def_readonly_static("indexMask", &Emulator::INDEX_MASK)
      // //TODO
      // .def_property_readonly("rawIndexedImage", SBA(Emulator,RawIndexedImage,XXX), "Get raw indexed image to be masked against indexMask. Copy it if you want to call other emulator functions.")
      .def_property_readonly("indexedImage", &Emulator::IndexedImage, "Get pre-masked indexed image in a fresh buffer.")
      .def_property_readonly("fc", (FC * (Emulator::*)(void)) &Emulator::GetFC, "Get the FC instance.")
      ;
    //FC: readonly properties for cart, fceu, fds, filter, ines, input, palette, ppu, sound, unif, vsuni, x6502, state
    py::class_<FC>(m, "FC")
      .def_readonly("cart", &FC::cart)
      .def_readonly("fceu", &FC::fceu)
      .def_readonly("input", &FC::input)
      .def_readonly("palette", &FC::palette)
      .def_readonly("ppu", &FC::ppu)
      //...
      ;
    //PPU
    ///spram, flags, state
    py::class_<PPU>(m, "PPU")
      .def_property_readonly("NTARAM", SBA(PPU,NTARAM,0x800))
      .def_property_readonly("PALRAM", SBA(PPU,PALRAM,0x20))
      .def_property_readonly("SPRAM", SBA(PPU,SPRAM,0x100))
      .def_property_readonly("xOffset", &PPU::GetXOffset)
      .def_property_readonly("tempAddr", &PPU::GetTempAddr)
      .def_property_readonly("values", SBA(PPU,PPU_values,4))
      //...
      ;
    //CART
    py::class_<Cart>(m, "Cart")
      // Each page is a 2k chunk of memory, corresponding to the address
      // (A >> 11), but located such that it is still indexed by A, not
      // A & 2047. (TODO: verify, and maybe "fix" -tom7)
      .def("writePageAddr", &Cart::WritePage)
      .def("readPageAddr", &Cart::ReadPage)
      .def("writeVPageAddr", &Cart::WriteVPage)
      .def("readVPageAddr", &Cart::ReadVPage)
      //I think the safe size is 0x800-A, but I'm not sure.
      .def("getVPageChunk", [](const Cart*c, uint32 A) { return SharedByteArray(c->VPagePointer(A)); }, "Get VPage pointer at given offset")
      .def("setVPageChunk", [](Cart*c, uint32 A, SharedByteArray sba) { c->SetVPage(A,sba.ptr); }, "Assign a VPage pointer at given offset")
      // .def_readonly("PRG", (uint8* (Cart::*)) &Cart::PRGptr)
      // .def_readonly("PRGSize", &Cart::PRGsize)
      // .def_readonly("CHR", (uint8* (Cart::*)) &Cart::CHRptr)
      // .def_readonly("CHRSize", &Cart::CHRsize)
      //...
      ;
    //FCEU
    py::class_<FCEU>(m,"FCEU")
      //size 0x800
      .def_property_readonly("RAM", SBA(FCEU,RAM,0x800))
      //size 131072
      .def_property_readonly("gameMemBlock", SBA(FCEU,GameMemBlock,131072))
      // .def_property_readonly("xBuf", SBA(FCEU,XBuf,256*256))
      // .def_property_readonly("xBackBuf", SBA(FCEU,XBackBuf,256*256))
      //TODO: GameInfo and bind FCEUGI
      //...
      ;
    //Movie, via SimpleFM
    m.def("readInputs", &SimpleFM2::ReadInputs);
    m.def("readInputs2P", &SimpleFM2::ReadInputs2P);
    m.def("inputToString", &SimpleFM2::InputToString);
    //TODO: later, do the rest.
    return m.ptr();
}
