# Little-endian binary reading and writing for the level file formats.

import struct


class FormatError(Exception):
    pass


class Reader:
    def __init__(self, data, offset=0, name="?"):
        self.data = data
        self.offset = offset
        self.name = name

    def read(self, fmt):
        fmt = "<" + fmt
        size = struct.calcsize(fmt)
        if self.offset + size > len(self.data):
            raise FormatError("%s: read of %d bytes at 0x%X past the end (0x%X)"
                              % (self.name, size, self.offset, len(self.data)))
        values = struct.unpack_from(fmt, self.data, self.offset)
        self.offset += size
        return values

    def one(self, fmt):
        return self.read(fmt)[0]

    def u8(self):
        return self.one("B")

    def u16(self):
        return self.one("H")

    def i16(self):
        return self.one("h")

    def u32(self):
        return self.one("I")

    def i32(self):
        return self.one("i")

    def f32(self):
        return self.one("f")

    def bytes(self, count):
        if count < 0 or self.offset + count > len(self.data):
            raise FormatError("%s: %d bytes at 0x%X past the end" % (self.name, count, self.offset))
        value = self.data[self.offset:self.offset + count]
        self.offset += count
        return value

    def at_end(self):
        return self.offset == len(self.data)

    def expect_end(self):
        if not self.at_end():
            raise FormatError("%s: stopped at 0x%X of 0x%X" % (self.name, self.offset, len(self.data)))


class Writer:
    def __init__(self):
        self.parts = bytearray()

    def write(self, fmt, *values):
        self.parts += struct.pack("<" + fmt, *values)

    def u8(self, v):
        self.write("B", v)

    def u16(self, v):
        self.write("H", v)

    def u32(self, v):
        self.write("I", v)

    def i32(self, v):
        self.write("i", v)

    def f32(self, v):
        self.write("f", v)

    def raw(self, data):
        self.parts += data

    def pad_to(self, alignment):
        while len(self.parts) % alignment:
            self.parts.append(0)

    def tell(self):
        return len(self.parts)

    def patch(self, offset, fmt, *values):
        struct.pack_into("<" + fmt, self.parts, offset, *values)

    def getvalue(self):
        return bytes(self.parts)
