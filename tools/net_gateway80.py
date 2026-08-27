# Passerelle HTTP transparente pour l'ST émulé : liée à 0.0.0.0:80 (exemption
# macOS INADDR_ANY). localdns.py répond 10.0.2.2 (= loopback hôte via le NAT)
# pour tout nom ; la PREMIÈRE requête donne le vrai serveur (en-tête Host), puis
# la connexion est pompée dans les deux sens (keep-alive HTTP/1.1 intact) —
# HTTP authentique de bout en bout côté ST, sortie hôte par un process autorisé.
import socket, threading, re

def pump(src, dst):
    try:
        while True:
            d = src.recv(16384)
            if not d:
                break
            dst.sendall(d)
    except OSError:
        pass
    try:
        dst.shutdown(socket.SHUT_WR)
    except OSError:
        pass

def handle(c):
    try:
        c.settimeout(120)
        head = b''
        while b'\r\n\r\n' not in head:
            d = c.recv(4096)
            if not d: return
            head += d
        m = re.search(rb'\r\nHost:[ \t]*([^\r\n:]+)', head, re.IGNORECASE)
        host = m.group(1).decode('latin-1').strip() if m else 'theoldnet.com'
        print('gw:', host, head.split(b'\r\n', 1)[0].decode('latin-1'), flush=True)
        up = socket.create_connection((host, 80), timeout=25)
        up.settimeout(120)
        up.sendall(head)
        t = threading.Thread(target=pump, args=(c, up), daemon=True)
        t.start()
        pump(up, c)
        t.join(timeout=130)
        up.close()
    except Exception as e:
        print('gw err:', e, flush=True)
    finally:
        c.close()

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', 80)); s.listen(16)
print('gw: listening 0.0.0.0:80', flush=True)
while True:
    conn, _ = s.accept()
    threading.Thread(target=handle, args=(conn,), daemon=True).start()
