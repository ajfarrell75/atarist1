# Résolveur DNS local pour l'ST émulé : lié à 0.0.0.0:53 (exemption macOS
# INADDR_ANY, pas de root), il résout par l'hôte (process autorisé du pare-feu)
# et répond des A records au ST via le NAT SLIRP (NAMESERVER = 10.0.2.2).
import socket, sys

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('0.0.0.0', 53))
print('dns: listening 0.0.0.0:53', flush=True)

def qname(data):
    # Décode le nom de la question (offset 12), rend (nom, offset après).
    parts, i = [], 12
    while data[i]:
        n = data[i]; parts.append(data[i+1:i+1+n].decode('latin-1')); i += 1 + n
    return '.'.join(parts), i + 1

while True:
    data, peer = s.recvfrom(512)
    if len(data) < 17:
        continue
    try:
        name, qend = qname(data)
        try:
            socket.gethostbyname(name)          # le nom existe-t-il vraiment ?
            ip = '10.0.2.2'                     # → passerelle loopback (gateway80.py)
            ipb = bytes(int(x) for x in ip.split('.'))
            flags, an = b'\x81\x80', b'\x00\x01'
            answer = (b'\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x3c\x00\x04' + ipb)
            print('dns:', name, '->', ip, flush=True)
        except OSError:
            flags, an, answer = b'\x81\x83', b'\x00\x00', b''   # NXDOMAIN
            print('dns:', name, '-> NXDOMAIN', flush=True)
        resp = (data[:2] + flags + data[4:6] + an + b'\x00\x00\x00\x00'
                + data[12:qend + 4] + answer)
        s.sendto(resp, peer)
    except Exception as e:
        print('dns err:', e, flush=True)
