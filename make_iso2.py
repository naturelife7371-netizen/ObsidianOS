# make_iso2.py - use pycdlib for a proper El Torito bootable ISO
import os
import pycdlib

ROOT = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(ROOT, "build", "ObsidianOS.img")
OUT = os.path.join(ROOT, "build", "ObsidianOS.iso")

iso = pycdlib.PyCdlib()
iso.new(joliet=3, rock_ridge="1.09", vol_ident="OBSIDIANOS", sys_ident="OBSIDIANOS")

import io
with open(IMG, "rb") as f:
    data = f.read()
iso.add_fp(io.BytesIO(data), len(data), iso_path="/OBSIDIAN.IMG;1", rr_name="obsidian.img", joliet_path="/OBSIDIAN.IMG")

iso.add_eltorito("/OBSIDIAN.IMG;1", media_name="floppy", bootable=True, boot_load_seg=0)

iso.write(OUT)
iso.close()
print("done", OUT, os.path.getsize(OUT))
