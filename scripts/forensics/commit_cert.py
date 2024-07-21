import struct
import os
import sys
import base64
from datetime import datetime, timedelta



class commit_cert:
    def __init__(self, filename):
        self.cc = []
        with open(filename, "rb") as file:
            while True:
                size_t_size_byte = file.read(1)
                if not size_t_size_byte:
                    break
                size_t_size = struct.unpack("B", size_t_size_byte)[0]
                size_t_format = "Q" if size_t_size == 8 else "I"
                
                size_of_cert = struct.unpack(size_t_format, file.read(size_t_size))[0]
                cert = file.read(size_of_cert)
                
                self.cc.append(self.deserialize_cc(cert))

    def __str__(self):
        s = f'{"Term":<8} {"Idx":<10} {"Num Svr":<8} {"Num Sigs":<8} {"Sigs":<80}\n'
        for ns, t, idx, num_sigs, sigs in self.cc:
            s += f"{t:<8} {idx:<10} {ns:<8} {num_sigs:<8} {sigs.hex():<80}\n"
        return s
    
    def deserialize_cc(self, cert):
        return struct.unpack("=iQQi", cert[:4 + 2 * 8 + 4]) + (cert[4 + 2 * 8 + 4:],)
    
    def add_to_dict(self, d):
        for ns, t, idx, num_sigs, sigs in self.cc:
            if (t, idx) not in d:
                d[(t, idx)] = [(num_sigs, ns, sigs)]
            else:
                d[(t, idx)].append((num_sigs, ns, sigs))


if __name__ == "__main__":
    # Example usage
    combined = {}
    for file in os.listdir(sys.argv[1]):
        if not file.startswith("cc"):
            continue
        
        parts = file.split('_')
        timestamp = int(parts[1])
        date_time = datetime(1970, 1, 1) + timedelta(microseconds=timestamp)
        peer_number = parts[2].split('.')[0][1:]  # removes 'p' from 'p1' and '.dat'
        
        print(f"File: {file}, Timestamp: {date_time}, Peer: {peer_number}")
        cc = commit_cert(f"{sys.argv[1]}/{file}")
        print(cc)
        cc.add_to_dict(combined)
    
    for k, v in combined.items():
        if not all([x == v[0] for x in v]):
            print("Commit certs are different.")
            exit()
    
    print("All commit certs are the same.")
    