import hashlib

def dsha(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()

c1 = bytes.fromhex('02000000010000000000000000000000000000000000000000000000000000000000000000ffffffff1b03deaf0e5075626c69632d506f6f6c')
en1 = bytes.fromhex('1e09f3dc')
en2 = bytes.fromhex('0000000000000005')

# coinb2 loaded from separate file (very long)
with open('coinb2_hex.txt') as f:
    c2 = bytes.fromhex(f.read().strip())

branches_hex = [
'dd659f113396da37a86b5641c7232f86343772780482bace1a445b0412379e79',
'1f81963e867b221966af098c7e281a88a809187f4cc2f21236b0a6b3ff3d6165',
'c454bdd07789d29e8f22695594f379e5c96dfaf13867daf2603f586982072cf5',
'0c2747de1d4500568ae49620033e2e7223f0ed86ec6ea69b56bf22efa33958b5',
'9f61f045a367f6854d47e9db5af50a9ae92fdb1fe35389fbbe81ab2eb1513cef',
'e757ecaef100567c58d55dc5e545e87143bb390c1eaec49c22d6108e0c2978cf',
'60f20a652b1bab1f8710fc4a5522f03ca85747cff7ed17a04a8e58a97e999899',
'59e1f299890d38a7250be2f4ac5079b445c8d2fa58ab9eba904799e3639217fc',
'080ffa34dfe11a2bee766e890a70b6d9b45932518626ca3721186dc5ed1ef5bd',
'f537bdf7ed428ae48e9262dcadf48a289c4214ef55e52216aaedfe3f840fd036',
'f89f3624ab847db0b85255f8b00847bbc96444c389c02ce2e32124e91493fd8f',
'2f16ec960ca35cc0413bda4fd87df1c4364f8c3a6eb4dc9459e59ed119fc31c7',
'ed75295b68f6344d18907ae3a6ee0d9e32cadaca48f9385e7ba1d506ef01b4c6',
]
branches = [bytes.fromhex(b) for b in branches_hex]

coinbase = c1 + en1 + en2 + c2
cth = dsha(coinbase)
print('coinbase_tx_hash (BE):', cth.hex())

# Variant A: bitcoin-standard LE merkle tree
cur = cth[::-1]
for br in branches:
    h = dsha(cur + br)
    cur = h[::-1]
print('A LE tree root:', cur.hex())

# Variant B: BE tree
cur = cth
for br in branches:
    cur = dsha(cur + br)
print('B BE tree root:', cur.hex())

# Variant C: LE start, BE output
cur = cth[::-1]
for br in branches:
    cur = dsha(cur + br)
print('C LE-start BE-out:', cur.hex())

print('expected root:   84b522ad0b80083c219f1a09c230200f21543aee451a435fcc9a18fa1aa6dfbc')
