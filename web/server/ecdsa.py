
# === P-256 curve parameters ===
P = 0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff
A = 0xffffffff00000001000000000000000000000000fffffffffffffffffffffffc
B = 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b
N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551
Gx = 0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296
Gy = 0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5

import hashlib


# === Pure-Python elliptic curve operations over P-256 ===

INF = None  # point at infinity representation


def inv_mod(k: int, p: int) -> int:
	"""Modular inverse using extended Euclidean algorithm."""
	if k == 0:
		raise ZeroDivisionError("division by zero")
	if k < 0:
		return p - inv_mod(-k, p)

	s, old_s = 0, 1
	t, old_t = 1, 0
	r, old_r = p, k

	while r != 0:
		q = old_r // r
		old_r, r = r, old_r - q * r
		old_s, s = s, old_s - q * s
		old_t, t = t, old_t - q * t

	# old_r is gcd(k, p); for a prime field and nonzero k, this must be 1
	if old_r != 1:
		raise ValueError("inverse does not exist")

	return old_s % p


def point_double(point):
	"""Point doubling on the curve y^2 = x^3 + A*x + B over field P."""
	if point is INF:
		return INF

	x1, y1 = point
	if y1 % P == 0:
		return INF

	m = ((3 * x1 * x1 + A) * inv_mod(2 * y1, P)) % P
	x3 = (m * m - 2 * x1) % P
	y3 = (m * (x1 - x3) - y1) % P
	return x3, y3


def point_add(p1, p2):
	"""Point addition on the curve."""
	if p1 is INF:
		return p2
	if p2 is INF:
		return p1

	x1, y1 = p1
	x2, y2 = p2

	# P + (-P) = INF
	if x1 == x2 and (y1 + y2) % P == 0:
		return INF

	if x1 == x2 and y1 == y2:
		return point_double(p1)

	m = ((y2 - y1) * inv_mod((x2 - x1) % P, P)) % P
	x3 = (m * m - x1 - x2) % P
	y3 = (m * (x1 - x3) - y1) % P
	return x3, y3


def scalar_mult(k: int, point):
	"""Multiply a point by an integer k using double-and-add."""
	if point is INF:
		return INF

	k = k % N
	if k == 0:
		return INF

	result = INF
	addend = point

	while k:
		if k & 1:
			result = point_add(result, addend)
		addend = point_double(addend)
		k >>= 1

	return result


def scalar_mult_int(k: int, point):
	"""Multiply a point by an integer k without reducing k modulo N.

	Used where the exact multiple matters (e.g. order checks).
	"""
	if point is INF:
		return INF
	if k == 0:
		return INF
	if k < 0:
		raise ValueError("negative scalar not supported in scalar_mult_int")

	result = INF
	addend = point

	while k:
		if k & 1:
			result = point_add(result, addend)
		addend = point_double(addend)
		k >>= 1

	return result


def is_on_curve(point) -> bool:
	if point is INF:
		return True
	x, y = point
	return (y * y - (x * x * x + A * x + B)) % P == 0


def hash_to_int_p256(h: bytes) -> int:
	"""Convert a hash to an integer using FIPS 186-4 truncation for P-256.

	For P-256, n = bitlength(N) = 256. This returns the leftmost n bits of the
	hash interpreted as a big-endian bit string. If the hash is exactly 32 bytes
	(256 bits), this is just its big-endian integer value.
	"""
	if not isinstance(h, (bytes, bytearray)):
		raise TypeError("hash must be bytes or bytearray")
	if len(h) == 0:
		raise ValueError("hash must not be empty")

	n_bits = N.bit_length()
	h_bits = len(h) * 8
	z = int.from_bytes(h, "big")
	if h_bits > n_bits:
		z >>= (h_bits - n_bits)
	return z


def ecdsa_verify(x: int, y: int, r: int, s: int, e) -> bool:
	"""Verify an ECDSA signature (r, s) over P-256 for hash e with public key (x, y).

	e can be either an integer (already FIPS-truncated hash value) or bytes/
	bytearray (in which case SHA-256 is applied first, then FIPS truncation).
	"""
	# Check signature range
	if not (1 <= r < N and 1 <= s < N):
		return False

	# Validate public key coordinates
	if not (0 <= x < P and 0 <= y < P):
		return False

	Q = (x, y)
	if not is_on_curve(Q):
		return False

	# Optional: check that Q has the correct order
	if scalar_mult_int(N, Q) is not INF:
		return False

	try:
		w = inv_mod(s % N, N)
	except (ZeroDivisionError, ValueError):
		return False

	# Derive integer hash value from e.
	if isinstance(e, (bytes, bytearray)):
		# Hash message with SHA-256, then apply FIPS 186-4 truncation.
		h = hashlib.sha256(e).digest()
		e_int = hash_to_int_p256(h)
	elif isinstance(e, int):
		e_int = e
	else:
		raise TypeError("e must be int or bytes-like")

	# FIPS 186-4: e_int is the integer from the leftmost n bits of the hash.
	u1 = (e_int * w) % N
	u2 = (r * w) % N

	G = (Gx, Gy)
	P1 = scalar_mult(u1, G)
	P2 = scalar_mult(u2, Q)
	X = point_add(P1, P2)

	if X is INF:
		return False

	x1, _ = X
	v = x1 % N
	return v == (r % N)


def test():
	"""Basic self-test for ecdsa_verify using the cryptography library.

	Generates a random P-256 key pair, signs a random 32-byte hash, and verifies
	the signature using this module's pure-Python implementation.
	"""
	import os

	try:
		from cryptography.hazmat.primitives.asymmetric import ec, utils
		from cryptography.hazmat.primitives import hashes
	except ImportError as exc:
		print("cryptography module not available, skipping test()", exc)
		return

	# Generate P-256 key pair
	private_key = ec.generate_private_key(ec.SECP256R1())
	public_key = private_key.public_key()
	pub_numbers = public_key.public_numbers()
	x = pub_numbers.x
	y = pub_numbers.y

	# Create random message bytes (128 bytes)
	msg = os.urandom(128)

	# Sign the message using ECDSA with SHA-256
	signature = private_key.sign(
		msg,
		ec.ECDSA(hashes.SHA256()),
	)

	# Extract (r, s) from the ASN.1-encoded ECDSA signature
	r, s = utils.decode_dss_signature(signature)

	# Verify using our pure-Python implementation, passing the message bytes
	ok = ecdsa_verify(x, y, r, s, msg)
	print("ecdsa_verify result:", ok)
	if not ok:
		raise AssertionError("ecdsa_verify failed on a valid signature")

	print("ECDSA verification test passed.")


if __name__ == "__main__":
    test()
