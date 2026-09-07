---
name: reference-apollo-pdf-docs
description: AEGIS_Internals_and_Data_Structures_Jan86.pdf in ~/src/domainos-archeology/docs/apollo-docs names on-disk record fields the kernel never names - and how to extract its text without pdftotext
metadata:
  type: reference
---

`~/src/domainos-archeology/docs/apollo-docs/AEGIS_Internals_and_Data_Structures_Jan86.pdf`
is Apollo's own internals manual. It describes the PV label, the LV label,
the BAT header, the VTOC, the AST/MST/PMAP/MMAP tables, the FIM, SYSBOOT and
the ring, **naming fields in the order the on-disk records store them** and
carrying a glossary that defines the terms. That is exactly what a copied-out
word needs when the kernel does no arithmetic with it. It settled
`disk_$volume_t.bat_step` (bead source-zot4).

The other PDFs in that directory are hardware references (Engineering
Handbook, Series 3000/4000 Technical Reference, Hardware Architecture
Handbook addendum).

**Extracting the text.** This machine has no `pdftotext`, `mutool` or
`pypdf`. The OCR layer comes out fine with stdlib only -- inflate every
`stream ... endstream` that contains `Tj`/`TJ`, then pull the parenthesised
strings:

```python
import re, zlib
d = open(path,'rb').read()
parts = []
for m in re.finditer(rb'stream\r?\n?(.*?)endstream', d, re.S):
    try: t = zlib.decompress(m.group(1))
    except Exception: continue
    if b'Tj' in t or b'TJ' in t: parts.append(t)
blob = b'\n'.join(parts)
text = b' '.join(m.group(0)[1:-1]
                 for m in re.finditer(rb'\((?:\\.|[^\\()])*\)', blob)).decode('latin-1')
```

Search the result case-insensitively and print wide windows: the OCR is
lossy (`f` -> `C`/`r`, `fl` -> `n`, so "flag" reads "nag" and "first" reads
"rll'St"), so match on short distinctive stems and read around them. Words
are not reliably separated by newlines, so grep the whole blob, not lines.

Related: [[reference-sr104-userspace-binaries]] (invol/salvol/netmain
strings), [[reference-sr104-domain-os-map]].

## Disk images are the third check

`~/src/domainos-archeology/sr103.awd` and the three
`~/src/domainos-archeology/disk-images/harddrive/*.awd` are real installed
volumes. Blocks are 1056 bytes = a 32-byte header + 1024 of data, the PV
label is block 0 and the LV label block 1, so a candidate field's real value
is a few lines of Python away. `~/src/domainos-archeology/apollofs` (same
author, Go) already models both labels, the BAT header and the VTOC -- useful
as a cross-check, but it is reverse engineering too, not documentation, and
its field widths can be off (it types the BAT step `uint32` at +0x40, where
the kernel reads a `uint16` and the images show 1).
