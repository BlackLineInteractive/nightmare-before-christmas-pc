#!/usr/bin/env python3
"""
Capcom LZ Decompressor & STM Archive Unpacker
For 'Tim Burton's The Nightmare Before Christmas: Oogie's Revenge' (Xbox / PS2)
and other Capcom games using this engine format.
"""

import os
import sys
import struct

class BitReader:
    def __init__(self, data, start_pos=5):
        self.data = data
        self.pos = start_pos
        self.bit_pos = 0
        if self.pos + 4 <= len(self.data):
            self.cur_word = struct.unpack('<I', self.data[self.pos:self.pos+4])[0]
            self.pos += 4
        else:
            self.cur_word = 0

    def read_bit(self):
        bit = (self.cur_word >> self.bit_pos) & 1
        self.bit_pos = (self.bit_pos + 1) & 31
        if self.bit_pos == 0:
            if self.pos + 4 <= len(self.data):
                self.cur_word = struct.unpack('<I', self.data[self.pos:self.pos+4])[0]
                self.pos += 4
            elif self.pos < len(self.data):
                rem = self.data[self.pos:]
                self.cur_word = struct.unpack('<I', rem.ljust(4, b'\x00'))[0]
                self.pos = len(self.data)
            else:
                self.cur_word = 0
        return bit

    def read_bits(self, n):
        val = 0
        for i in range(n):
            val |= (self.read_bit() << i)
        return val


def decompress_capcom_lz(lz_data):
    """
    Decompresses Capcom LZ data (Type 1 or Type 2).
    Header:
      byte 0: compression type (1 or 2)
      bytes 1..4: uint32 uncompressed size
      bytes 5..: bitstream
    """
    comp_type = lz_data[0]
    uncomp_size = struct.unpack('<I', lz_data[1:5])[0]
    
    if comp_type == 0:
        return lz_data[5:5+uncomp_size]
    
    reader = BitReader(lz_data, start_pos=5)
    table = [2, 4, 6, 10]
    out = bytearray()
    
    while len(out) < uncomp_size:
        flag = reader.read_bit()
        if flag == 1:
            # Literal byte (8 bits)
            out.append(reader.read_bits(8))
        else:
            # Match (distance and length)
            if comp_type == 1:
                offset = reader.read_bits(10)
            else:
                # Type 2: variable length offset
                count_0 = 0
                while reader.read_bit() == 0:
                    count_0 += 1
                if count_0 >= len(table):
                    break
                offset = reader.read_bits(table[count_0])

            # Match length
            count_0_len = 0
            while reader.read_bit() == 0:
                count_0_len += 1
            if count_0_len >= len(table):
                break
            length = reader.read_bits(table[count_0_len])

            dist = offset + 1
            for _ in range(length):
                out.append(out[-dist])

    return bytes(out)


def unpack_stm_archive(stm_data, output_dir):
    """
    Recursively unpacks Capcom @STM archive.
    """
    if len(stm_data) < 32 or stm_data[:4] != b'@STM':
        raise ValueError("Invalid STM header (magic @STM not found)")

    first_file_offset, num_top_folders = struct.unpack('<II', stm_data[24:32])
    os.makedirs(output_dir, exist_ok=True)

    def process_dir(header_offset, count, current_dir):
        os.makedirs(current_dir, exist_ok=True)
        for i in range(count):
            ent_pos = header_offset + i * 32
            ent = stm_data[ent_pos : ent_pos + 32]
            name = ent[:24].split(b'\x00')[0].decode('ascii', errors='ignore')
            off, sz = struct.unpack('<II', ent[24:32])
            target_path = os.path.join(current_dir, name)

            if sz & 0x80000000:
                # Subdirectory
                sub_count = sz & 0x7FFFFFFF
                process_dir(off, sub_count, target_path)
            else:
                # File
                file_bytes = stm_data[off : off + sz]
                with open(target_path, 'wb') as f:
                    f.write(file_bytes)

    # Process all top level folders
    for i in range(1, num_top_folders + 1):
        ent = stm_data[i * 32 : (i + 1) * 32]
        folder_name = ent[:24].split(b'\x00')[0].decode('ascii', errors='ignore')
        off, sz = struct.unpack('<II', ent[24:32])
        count = sz & 0x7FFFFFFF
        process_dir(off, count, os.path.join(output_dir, folder_name))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.lz | file.stm> [output_directory]")
        sys.exit(1)

    in_file = sys.argv[1]
    with open(in_file, 'rb') as f:
        data = f.read()

    base_name = os.path.splitext(os.path.basename(in_file))[0]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else base_name + "_unpacked"

    if in_file.endswith('.lz'):
        print(f"Decompressing Capcom LZ: {in_file}...")
        stm_data = decompress_capcom_lz(data)
        stm_path = base_name + ".stm"
        with open(stm_path, 'wb') as f:
            f.write(stm_data)
        print(f"Saved decompressed stream to {stm_path} ({len(stm_data):,} bytes)")

        if stm_data[:4] == b'@STM':
            print(f"Unpacking STM archive into {out_dir}/...")
            unpack_stm_archive(stm_data, out_dir)
            print("Done!")
    elif data[:4] == b'@STM':
        print(f"Unpacking STM archive: {in_file} into {out_dir}/...")
        unpack_stm_archive(data, out_dir)
        print("Done!")
    else:
        print("Unknown file format (neither .lz nor @STM).")

