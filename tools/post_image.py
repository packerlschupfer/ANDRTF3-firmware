"""PlatformIO post-build step: finalise a slot image (see firmware/include/flash_layout.h).

Patches app_header_t.size, appends the CRC32 and writes <build>/andrtf3_<env>.bin.
Also usable stand-alone: post_image.py firmware.bin out.bin
"""
import struct, sys, zlib

HEADER_OFFSET, MAGIC = 0xC0, 0x474D4941


def finalise(src, dst):
    img = bytearray(open(src, "rb").read())
    img += b"\0" * (-len(img) % 4)
    magic, link, size, ver, hver = struct.unpack_from("<IIIHH", img, HEADER_OFFSET)
    if magic != MAGIC:
        raise SystemExit("post_image: no image header at 0x%X in %s" % (HEADER_OFFSET, src))
    struct.pack_into("<I", img, HEADER_OFFSET + 8, len(img))
    img += struct.pack("<I", zlib.crc32(img) & 0xFFFFFFFF)
    open(dst, "wb").write(img)
    print("post_image: %s  link 0x%08X  fw 0x%04X  %d bytes + CRC" % (dst, link, ver, len(img) - 4))


try:
    Import("env")                                         # noqa: F821 (PlatformIO)

    def _after_bin(target, source, env):
        b = str(target[0])
        finalise(b, b.replace("firmware.bin", "andrtf3_%s.bin" % env["PIOENV"]))

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _after_bin)   # noqa: F821
except NameError:
    if __name__ == "__main__":
        finalise(sys.argv[1], sys.argv[2])
