Block size: One word is 8 bytes. BlockInfo is 7 bytes. 7 bytes(56 bits) + 8 bytes (64 bits) = 120 bytes. 

A word is 8 bytes.

Mem address are in in bits.
Ex:
``` python
Block:
----------
Size: 120               # 120 bits
SAddr: 1198866432        
DAddr: 1198866488       # 56 bits (7 bytes) ahead of header start
InUse: 1
PUsed: 1
Next: 0
Prev: 0
```