#!/usr/bin/env python3
"""
Original Xbox (XDVDFS) ISO Extractor
Supports standard XDVDFS and Redump Xbox ISO images.
"""

import os
import sys
import struct

SECTOR_SIZE = 2048

def parse_xdvdfs_dir(f, xdvdfs_offset, sector, size):
    entries = []
    f.seek(xdvdfs_offset + sector * SECTOR_SIZE)
    data = f.read(size)
    
    def read_node(offset):
        if offset >= len(data) or offset == 0xffff * 4:
            return
        left, right = struct.unpack('<HH', data[offset:offset+4])
        sec, filesize = struct.unpack('<II', data[offset+4:offset+12])
        attrs = data[offset+12]
        name_len = data[offset+13]
        name = data[offset+14:offset+14+name_len].decode('ascii', errors='replace')
        
        if left != 0 and left != 0xffff:
            read_node(left * 4)
            
        entries.append((name, sec, filesize, attrs))
        
        if right != 0 and right != 0xffff:
            read_node(right * 4)

    read_node(0)
    return entries

def find_xdvdfs_offset(f):
    # Standard offsets for Xbox ISO:
    # 0x00000000 (XISO standard: volume descriptor at 0x10000)
    # 0x18300000 (Redump standard: volume descriptor at 0x18310000)
    # 0x0FD90000 (Alternative Redump)
    candidates = [0x0, 0x18300000, 0x0FD90000, 0x18304000]
    for base in candidates:
        f.seek(base + 0x10000)
        magic = f.read(20)
        if magic == b'MICROSOFT*XBOX*MEDIA':
            return base
            
    # Fallback brute search for magic
    f.seek(0, os.SEEK_END)
    file_size = f.tell()
    f.seek(0)
    step = 0x100000 # 1MB step
    pos = 0
    while pos < min(file_size, 0x30000000):
        f.seek(pos + 0x10000)
        magic = f.read(20)
        if magic == b'MICROSOFT*XBOX*MEDIA':
            return pos
        pos += step
    return None

def extract_iso(iso_path, output_dir):
    print(f"Opening ISO: {iso_path}")
    with open(iso_path, 'rb') as f:
        base_offset = find_xdvdfs_offset(f)
        if base_offset is None:
            print("Error: Could not find XDVDFS header (MICROSOFT*XBOX*MEDIA).")
            return False
            
        print(f"Found XDVDFS volume at base offset: {hex(base_offset)}")
        f.seek(base_offset + 0x10000)
        vol_desc = f.read(SECTOR_SIZE)
        root_sector, root_size = struct.unpack('<II', vol_desc[20:28])
        print(f"Root directory: sector {root_sector}, size {root_size} bytes")
        
        os.makedirs(output_dir, exist_ok=True)
        
        def extract_dir(sector, size, current_path):
            entries = parse_xdvdfs_dir(f, base_offset, sector, size)
            for name, entry_sec, entry_sz, attrs in entries:
                target_path = os.path.join(current_path, name)
                if attrs & 0x10: # Directory
                    os.makedirs(target_path, exist_ok=True)
                    if entry_sz > 0:
                        extract_dir(entry_sec, entry_sz, target_path)
                else: # File
                    print(f"Extracting: {target_path} ({entry_sz:,} bytes)...")
                    f.seek(base_offset + entry_sec * SECTOR_SIZE)
                    with open(target_path, 'wb') as out_f:
                        bytes_left = entry_sz
                        chunk_size = 1024 * 1024
                        while bytes_left > 0:
                            to_read = min(bytes_left, chunk_size)
                            chunk = f.read(to_read)
                            if not chunk:
                                break
                            out_f.write(chunk)
                            bytes_left -= len(chunk)
                            
        extract_dir(root_sector, root_size, output_dir)
        print("\nExtraction completed successfully!")
        return True

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path_to_iso> [output_directory]")
        sys.exit(1)
    iso_file = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "extracted_game"
    extract_iso(iso_file, out_dir)

