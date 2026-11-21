"""Add Revulting to Steam as a non-Steam game (run with Steam CLOSED).

Appends an entry to userdata/<id>/config/shortcuts.vdf (binary VDF),
keeping the existing ones, after backing the file up.  Idempotent: an
existing "Revulting" entry is left alone.
"""
import binascii, os, shutil, struct, sys, time

STEAM = r'C:\Program Files (x86)\Steam'
GAME = r'C:\Users\dalla\Vulture\VultureNetHack'
EXE = GAME + r'\NetHackV.exe'
NAME = 'Revulting'


def parse(b, i):
    """binary VDF map starting at i -> (dict(list of (k, v)), i)"""
    out = []
    while True:
        t = b[i]; i += 1
        if t == 0x08:
            return out, i
        j = b.index(b'\x00', i); key = b[i:j].decode('latin-1'); i = j + 1
        if t == 0x00:
            v, i = parse(b, i)
        elif t == 0x01:
            j = b.index(b'\x00', i); v = b[i:j].decode('latin-1'); i = j + 1
        elif t == 0x02:
            v = struct.unpack('<I', b[i:i + 4])[0]; i += 4
        else:
            raise ValueError('vdf type %d' % t)
        out.append((key, t, v))


def dump(entries):
    o = b''
    for key, t, v in entries:
        # latin-1 both ways = byte-exact (existing names may not be UTF-8)
        o += bytes([t]) + key.encode('latin-1') + b'\x00'
        if t == 0x00:
            o += dump(v)
        elif t == 0x01:
            o += v.encode('latin-1') + b'\x00'
        else:
            o += struct.pack('<I', v & 0xFFFFFFFF)
    return o + b'\x08'


def main():
    import subprocess
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq steam.exe'], capture_output=True, text=True)
    if 'steam.exe' in r.stdout.lower():
        sys.exit('Steam is running - close it first (it rewrites this file).')
    ud = os.path.join(STEAM, 'userdata')
    done = 0
    for uid in os.listdir(ud):
        f = os.path.join(ud, uid, 'config', 'shortcuts.vdf')
        if not os.path.isfile(f):
            continue
        b = open(f, 'rb').read()
        root, _ = parse(b, 0)
        sc = [v for k, t, v in root if k.lower() == 'shortcuts'][0]
        names = [dict((kk, vv) for kk, tt, vv in e).get('AppName', dict((kk, vv) for kk, tt, vv in e).get('appname', '')) for k, t, e in sc]
        if NAME in names:
            print(uid, 'already has', NAME)
            continue
        shutil.copy2(f, f + '.bak-revulting-' + time.strftime('%Y%m%d%H%M%S'))
        exe_q = '"%s"' % EXE
        appid = (binascii.crc32((exe_q + NAME).encode('utf-8')) | 0x80000000) & 0xFFFFFFFF
        entry = [
            ('appid', 0x02, appid), ('AppName', 0x01, NAME), ('Exe', 0x01, exe_q),
            ('StartDir', 0x01, '"%s\\"' % GAME), ('icon', 0x01, GAME + r'\nh_icon.ico'),
            ('ShortcutPath', 0x01, ''), ('LaunchOptions', 0x01, ''),
            ('IsHidden', 0x02, 0), ('AllowDesktopConfig', 0x02, 1),
            ('AllowOverlay', 0x02, 1), ('OpenVR', 0x02, 0), ('Devkit', 0x02, 0),
            ('DevkitGameID', 0x01, ''), ('DevkitOverrideAppID', 0x02, 0),
            ('LastPlayTime', 0x02, 0), ('FlatpakAppID', 0x01, ''),
            ('sortas', 0x01, ''), ('tags', 0x00, []),
        ]
        sc.append((str(len(sc)), 0x00, entry))
        open(f, 'wb').write(dump(root))  # dump() already ends the root map
        print(uid, 'added', NAME, 'appid', appid)
        done += 1
    print('done', done)


if __name__ == '__main__':
    main()
