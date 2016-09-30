import fceulib
from fceulib import VectorBytes
from PIL import Image


def go():
    emu = fceulib.runGame("mario.nes")

    for i in range(0, 1000):
        emu.stepFull(0xFF, 0xFF)
    saved = emu.saveUncompressed()

    for i in range(0, 3000):
        emu.stepFull(0xFF, 0xFF)

    emu.loadUncompressed(saved)
    emu.stepFull(0xFF, 0xFF)

    outBytes = VectorBytes()
    emu.imageInto(outBytes)
    outImg = Image.frombytes("RGBA", (256, 256), str(bytearray(outBytes)))
    outImg.save("run.png")
