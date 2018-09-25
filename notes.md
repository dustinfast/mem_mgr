Block size: One word is 8 bytes. BlockInfo is 7 bytes. 7 bytes(56 bits) + 8 bytes (64 bits) = 120 bytes. 

A word is 8 bytes.

Mem address are in in bits.
Ex:
``` python

Heap
----------
Size: 20971520
SAddr: 3308511232
End: 3329482752
FFree: 3287539712

Block:
----------
Size: 3287539712        # Heap size - 4 bytes of Heap header
SAddr: 3287539712       # Ahead of Heap 
DAddr: 3287539760       # 6 bytes (48 bits) ahead of SADDR
InUse: 1
PUsed: 1
Next: 0
Prev: 0
```