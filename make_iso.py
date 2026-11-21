# make_iso.py - build a bootable El Torito ISO from build/ObsidianOS.img
import struct, os, sys

SECTOR = 2048
IMG_PATH = os.path.join(os.path.dirname(__file__), "build", "ObsidianOS.img")
OUT_PATH = os.path.join(os.path.dirname(__file__), "build", "ObsidianOS.iso")

boot_img = open(IMG_PATH, "rb").read()
assert len(boot_img) == 1474560, "expected 1.44MB floppy image"
img_sectors = len(boot_img) // SECTOR  # 720

# ---- Layout ----
SYS_AREA = 16
PVD_SEC = 16
BOOTREC_SEC = 17
TERM_SEC = 18
BOOTCAT_SEC = 19
BOOTIMG_SEC = 20
BOOTIMG_END = BOOTIMG_SEC + img_sectors  # 740
ROOTDIR_SEC = BOOTIMG_END
PATH_TBL_SEC = ROOTDIR_SEC + 1
TOTAL_SECS = PATH_TBL_SEC + 1

# pad image to sector boundary (already exact)

def pad(b, n):
    return b + b"\x00" * (n - len(b) % n) if len(b) % n else b

def le16(x): return struct.pack("<H", x)
def be16(x): return struct.pack(">H", x)
def le32(x): return struct.pack("<I", x)
def be32(x): return struct.pack(">I", x)
def both16(x): return le16(x) + be16(x)
def both32(x): return le32(x) + be32(x)

# ---- Directory record for root ----
def dir_record(loc, size, flags, name):
    # name: bytes for the identifier (1 for root "\x00", else filename)
    # build record
    name_bytes = name
    # pad to even length
    if len(name_bytes) % 2:
        name_bytes = name_bytes + b"\x00"
    rec_len = 33 + len(name_bytes)
    # round up to even? ISO requires record length even? Actually length can be odd; pad name to even.
    rec = bytearray()
    rec.append(rec_len)
    rec.append(0)  # XAR length
    rec += le32(loc) + be32(loc)  # location of extent
    rec += le32(size) + be32(size)  # data length
    # recording date/time: 7 bytes (1900-based). Use 2026? Just zeros-ish; use 2025.
    rec += bytes([0, 0, 0, 0, 0, 0, 0])  # Y-1900, M, D, H, M, S, TZ
    rec.append(flags)  # file flags
    rec.append(0)  # file unit size
    rec.append(0)  # interleave gap
    rec += le16(1) + be16(1)  # volume sequence number
    rec.append(len(name) if not (name == b"\x00") else 1)
    # length of file identifier: for root, 1
    # fix: length byte should be len of original name (before padding)
    # recompute properly:
    pass
    return rec

# Rebuild directory record cleanly
def make_dir_record(loc, size, flags, ident):
    # ident: bytes of the file identifier (without padding)
    idlen = len(ident)
    # pad identifier to even
    idpadded = ident if (idlen % 2 == 0) else ident + b"\x00"
    rec_len = 33 + len(idpadded)
    rec = bytearray()
    rec.append(rec_len)
    rec.append(0)  # XAR
    rec += struct.pack("<I", loc) + struct.pack(">I", loc)
    rec += struct.pack("<I", size) + struct.pack(">I", size)
    rec += bytes([125, 1, 1, 0, 0, 0, 0])  # date: 2025-01-01 00:00:00, TZ 0  (125 = 2025-1900)
    rec.append(flags)
    rec.append(0)  # unit size
    rec.append(0)  # interleave
    rec += struct.pack("<H", 1) + struct.pack(">H", 1)  # volume seq
    rec.append(idlen)
    rec += idpadded
    # ensure rec_len matches
    assert len(rec) == rec_len, (len(rec), rec_len)
    return bytes(rec)

# Root directory record content: "." and ".." entries
root_rec_dot = make_dir_record(ROOTDIR_SEC, 2048, 0x02, b"\x00")  # self
root_rec_dotdot = make_dir_record(ROOTDIR_SEC, 2048, 0x02, b"\x01")  # parent
# boot image file entry
boot_rec = make_dir_record(BOOTIMG_SEC, len(boot_img), 0x00, b"OBSIDIAN.IMG;1")

root_dir_data = root_rec_dot + root_rec_dotdot + boot_rec
# pad to 2048
root_dir_data = root_dir_data + b"\x00" * (SECTOR - len(root_dir_data))

# ---- Path table (type L) ----
# entries: root (parent 1), then... only root needed for minimal
# Path table entry: len_dir_id(1), xar_len(1), loc(4 LE), parent(2), dir_id (padded even)
def path_entry(name, loc, parent):
    nid = name.encode() if isinstance(name, str) else name
    # for root, name is "\x00"
    if nid == b"\x00":
        idlen = 1
        idbytes = b"\x00"
    else:
        idlen = len(nid)
        idbytes = nid
    if idlen % 2:
        idbytes = idbytes + b"\x00"
    entry = bytes([idlen, 0]) + struct.pack("<I", loc) + struct.pack("<H", parent) + idbytes
    return entry

path_tbl = path_entry(b"\x00", ROOTDIR_SEC, 1)
path_tbl = path_tbl + b"\x00" * (SECTOR - len(path_tbl))

# ---- PVD ----
pvd = bytearray(SECTOR)
pvd[0] = 1  # type
pvd[1:6] = b"CD001"
pvd[6] = 1  # version
# system id (32 bytes) at 8
pvd[8:40] = b" " * 32
# volume id (32 bytes) at 40
vol_id = b"OBSIDIANOS"
pvd[40:40+len(vol_id)] = vol_id
pvd[40+len(vol_id):72] = b" " * (72 - (40+len(vol_id)))
# unused 8 at 72
# volume space size at 80: both32
pvd[80:88] = both32(TOTAL_SECS)
# unused 32 at 88
# volume set size at 120
pvd[120:124] = both16(1)
# volume sequence number at 124
pvd[124:128] = both16(1)
# logical block size at 128
pvd[128:132] = both16(SECTOR)
# path table size at 132
pvd[132:140] = both32(len(path_tbl))
# type-L path table location at 140
pvd[140:144] = struct.pack("<I", PATH_TBL_SEC)
# type-M at 144 (4 bytes, zeros) - optional
# type-L optional at 148
pvd[148:152] = struct.pack("<I", PATH_TBL_SEC)
# root directory record at 156 (34 bytes)
root_rec_for_pvd = make_dir_record(ROOTDIR_SEC, 2048, 0x02, b"\x00")
pvd[156:156+len(root_rec_for_pvd)] = root_rec_for_pvd
# volume set id at 190 (128)
# publisher at 318
# data preparer at 446
# application at 574
app = b"OBSIDIANOS"
pvd[574:574+len(app)] = app
# copyright at 702 (37)
# abstract at 739 (37)
# bibliographic at 776 (37)
# dates at 813, 830, 847, 864 (17 bytes each): "0000000000000000" + 0? format: 16 digits + 1 offset byte
# Use a dummy date string: "2025010100000000" + 0 (GMT offset)
datestr = b"2025010100000000\x00"
for off in (813, 830, 847, 864):
    pvd[off:off+17] = datestr
# file structure version at 881
pvd[881] = 1
# reserved 882
# application use 883..1395 (512) zeros

# ---- Boot Record (El Torito) ----
bootrec = bytearray(SECTOR)
bootrec[0] = 0  # type
bootrec[1:6] = b"CD001"
bootrec[6] = 1
# boot system identifier at 7: "EL TORITO SPECIFICATION" padded to 32 bytes
bsi = b"EL TORITO SPECIFICATION"
bootrec[7:7+len(bsi)] = bsi
bootrec[7+len(bsi):39] = b" " * (39 - (7+len(bsi)))
# boot identifier at 39: 32 bytes (spaces)
bootrec[39:71] = b" " * 32
# boot catalog pointer at 71: 4 bytes LE + 4 bytes BE
bootrec[71:75] = struct.pack("<I", BOOTCAT_SEC)
bootrec[75:79] = struct.pack(">I", BOOTCAT_SEC)

# ---- Terminator ----
term = bytearray(SECTOR)
term[0] = 255
term[1:6] = b"CD001"
term[6] = 1

# ---- Boot catalog ----
bootcat = bytearray(SECTOR)
# Validation entry (32 bytes) at offset 0
bootcat[0] = 0x01  # header id
bootcat[1] = 0x00  # platform id (80x86)
bootcat[2:4] = b"\x00\x00"  # reserved
idstr = b"OBSIDIANOS"
bootcat[4:4+len(idstr)] = idstr
bootcat[4+len(idstr):28] = b" " * (28 - (4+len(idstr)))
# key bytes (must be set before checksum calculation)
bootcat[30] = 0x55
bootcat[31] = 0xAA
# checksum at 28: sum of all 16 words must be 0
words = []
for i in range(0, 32, 2):
    if i == 28:
        words.append(0)
    else:
        words.append(struct.unpack("<H", bootcat[i:i+2])[0])
s = sum(words)
checksum = (-s) & 0xFFFF
bootcat[28:30] = struct.pack("<H", checksum)

# Initial/Default entry (32 bytes) at offset 32
ie = bytearray(32)
ie[0] = 0x88  # boot indicator
ie[1] = 0x02  # media type: 1.44MB floppy
ie[2:4] = struct.pack("<H", 0x0000)  # load segment (0 = default 0x7C0)
ie[4] = 0x00  # system type
ie[5] = 0x00  # unused
ie[6:8] = struct.pack("<H", 1)  # sector count
ie[8:12] = struct.pack("<I", BOOTIMG_SEC)  # load RBA
# rest zeros
bootcat[32:64] = bytes(ie)

# ---- Assemble ISO ----
out = bytearray()
# system area
out += b"\x00" * (SYS_AREA * SECTOR)
# PVD
out += bytes(pvd)
# boot record
out += bytes(bootrec)
# terminator
out += bytes(term)
# boot catalog
out += bytes(bootcat)
# boot image (floppy)
out += boot_img
# root dir
out += root_dir_data
# path table
out += path_tbl

# pad total to multiple of SECTOR
while len(out) % SECTOR:
    out += b"\x00"

assert len(out) // SECTOR == TOTAL_SECS, (len(out)//SECTOR, TOTAL_SECS)

with open(OUT_PATH, "wb") as f:
    f.write(out)

print("ISO written:", OUT_PATH, len(out), "bytes,", len(out)//SECTOR, "sectors")
