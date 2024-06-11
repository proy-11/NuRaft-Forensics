import struct
import os
import sys
import base64
from datetime import datetime, timedelta



class leader_signatures:
    def __init__(self, filename):
        self.ls = []
        with open(filename, "rb") as file:
            while True:
                size_t_size_byte = file.read(1)
                if not size_t_size_byte:
                    break
                size_t_size = struct.unpack("B", size_t_size_byte)[0]
                size_t_format = "Q" if size_t_size == 8 else "I"
                key_bytes = file.read(size_t_size)
                key = struct.unpack(size_t_format, key_bytes)[0]
                idx = struct.unpack("Q", file.read(8))[0]
                
                # Read the length of the serialized data (size_t)
                len_bytes = file.read(size_t_size)
                length = struct.unpack(size_t_format, len_bytes)[0]

                # Read the serialized data
                serialized_data = file.read(length)

                self.ls.append((key, idx, base64.b64encode(serialized_data).decode("utf-8")))

    def __str__(self):
        s = f'{"Term":<8} {"Idx":<10} {"Leader Sigs":<90}\n'
        for k, idx, v in self.ls:
            s += f"{k:<8} {idx:<10} {v:<90}\n"
        return s


if __name__ == "__main__":
    # Example usage
    for file in os.listdir(sys.argv[1]):
        if not file.startswith("ls"):
            continue
        
        parts = file.split('_')
        timestamp = int(parts[1])
        date_time = datetime(1970, 1, 1) + timedelta(microseconds=timestamp)
        peer_number = parts[2].split('.')[0][1:]  # removes 'p' from 'p1' and '.dat'
        
        print(f"File: {file}, Timestamp: {date_time}, Peer: {peer_number}")
        el = leader_signatures(f"{sys.argv[1]}/{file}")
        print(el)
