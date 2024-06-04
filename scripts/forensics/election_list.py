import struct
import os
import sys


class leader_certificate:
    def __init__(self, buffer):
        print(buffer.hex())
        self.num_servers, self.term, self.index, nsig = struct.unpack(
            "<iQQi", buffer[:24]
        )
        buffer = buffer[24:]
        self.certs = {}
        for i in range(nsig):
            id, sig_len = struct.unpack("ii", buffer[:8])
            self.certs[id] = buffer[8 : 8 + sig_len]
            buffer = buffer[8 + sig_len :]

        # read request
        req_len = struct.unpack("i", buffer[:4])[0]
        self.request = buffer[4 : 4 + req_len]

    def get_request(self):
        return self.request

    def get_request_string(self):
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
            size_t_size = struct.unpack("B", file.read(1))[0]
            size_t_format = "Q" if size_t_size == 8 else "I"

            while True:
                key_bytes = file.read(size_t_size)
                if not key_bytes:
                    break
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
        s = f'{"Term":<8} {"Request":<80} {"Voter signatures":<20}\n'
        for k, v in self.el.items():
            s += f"{k:<8} {v.get_request_string():<80} {str(v.get_certs()):<20}\n"
        return s


if __name__ == "__main__":
    # Example usage
    for file in os.listdir(sys.argv[1]):
        try:
            print(f"File: {file}")
            el = election_list(f"{sys.argv[1]}/{file}")
            print(el)
        except:
            pass
