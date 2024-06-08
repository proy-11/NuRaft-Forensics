import struct
import os
import sys
import base64
from datetime import datetime, timedelta


class leader_certificate:
    def __init__(self, buffer):
        # print(buffer.hex())
        self.num_servers, self.term, self.index, nsig = struct.unpack(
            "<iQQi", buffer[:24]
        )
        buffer = buffer[24:]
        self.certs = {}
        for i in range(nsig):
            id, sig_len = struct.unpack("ii", buffer[:8])
            self.certs[id] = base64.b64encode(buffer[8 : 8 + sig_len]).decode("utf-8")
            buffer = buffer[8 + sig_len :]

        # read request
        req_len = struct.unpack("i", buffer[:4])[0]
        self.request = buffer[4 : 4 + req_len]

    def get_request(self):
        return self.request

    def get_request_string(self):
        if len(self.request) < 40:
            return "No request for this term."
        
        last_log_term, last_log_idx, commit_idx, term, type_, src = struct.unpack(
            "<QQQQii", self.request[:40]
        )
        return f"Last log term: {last_log_term}, Last log index: {last_log_idx}, Commit index: {commit_idx}, Term: {term}, Type: {type_}, Src: {src}"

    def get_certs(self):
        return self.certs


class election_list:
    def __init__(self, filename):
        self.el = {}
        with open(filename, "rb") as file:
            while True:
                size_t_size_byte = file.read(1)
                if not size_t_size_byte:
                    break
                size_t_size = struct.unpack("B", size_t_size_byte)[0]
                size_t_format = "Q" if size_t_size == 8 else "I"
                key_bytes = file.read(size_t_size)
                key = struct.unpack(size_t_format, key_bytes)[0]

                # Read the length of the serialized data (size_t)
                len_bytes = file.read(size_t_size)
                length = struct.unpack(size_t_format, len_bytes)[0]

                # Read the serialized data
                serialized_data = file.read(length)

                self.el[key] = leader_certificate(serialized_data)

    def get_el(self):
        return self.el

    def __str__(self):
        s = f'{"Term":<8} {"Request":<90} {"Voter signatures":<20}\n'
        for k, v in self.el.items():
            s += f"{k:<8} {v.get_request_string():<90} {str(v.get_certs()):<20}\n"
        return s


if __name__ == "__main__":
    # Example usage
    for file in os.listdir(sys.argv[1]):
        if not file.startswith("el"):
            continue
        
        parts = file.split('_')
        timestamp = int(parts[1])
        date_time = datetime(1970, 1, 1) + timedelta(microseconds=timestamp)
        peer_number = parts[2].split('.')[0][1:]  # removes 'p' from 'p1' and '.dat'
        
        print(f"File: {file}, Timestamp: {date_time}, Peer: {peer_number}")
        el = election_list(f"{sys.argv[1]}/{file}")
        print(el)
