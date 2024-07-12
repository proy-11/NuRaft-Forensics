import struct
import os
import sys
import base64
from datetime import datetime, timedelta



class log_entries:
    def __init__(self, filename):
        self.log_entries = []
        with open(filename, "rb") as file:
            while True:
                # Read the log_idx
                log_idx_bytes = file.read(struct.calcsize('Q'))
                if not log_idx_bytes:
                    break  # End of file reached
                log_idx = struct.unpack('Q', log_idx_bytes)[0]

                # Read the term
                term_bytes = file.read(struct.calcsize('Q'))
                if not term_bytes:
                    break  # End of file reached
                term = struct.unpack('Q', term_bytes)[0]

                # Read the type
                type_byte = file.read(1)
                if not type_byte:
                    break  # End of file reached
                log_type = struct.unpack('B', type_byte)[0]

                # Read the len
                len_bytes = file.read(struct.calcsize('Q'))  # Assuming size_t is 8 bytes
                if not len_bytes:
                    break  # End of file reached
                data_len = struct.unpack('Q', len_bytes)[0]

                # Read the data
                data = file.read(data_len)
                if len(data) != data_len:
                    break  # End of file reached or corrupted file
                
                self.log_entries.append((log_idx, term, log_type, data))

    def __str__(self):
        s = f'{"Log_idx":<8} {"Term":<10} {"Log_type":<10}\n'
        for idx, term, type, _ in self.log_entries:
            s += f"{idx:<8} {term:<10} {type:<10}\n"
        return s


if __name__ == "__main__":
    # Example usage
    for file in os.listdir(sys.argv[1]):
        if not file.startswith("log_entries"):
            continue
        
        parts = file.split('_')
        peer_number = parts[-1].split('.')[0][1:]  # removes 'p' from 'p1' and '.dat'
        
        print(f"File: {file}, Peer: {peer_number}")
        el = log_entries(f"{sys.argv[1]}/{file}")
        print(el)
