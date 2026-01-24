from cryptography.hazmat.primitives.asymmetric import ec

# === P-256 curve parameters ===
P = 0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff
A = 0xffffffff00000001000000000000000000000000fffffffffffffffffffffffc
B = 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b
N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551
Gx = 0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296
Gy = 0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5


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


def is_on_curve(point) -> bool:
	if point is INF:
		return True
	x, y = point
	return (y * y - (x * x * x + A * x + B)) % P == 0


# === Use cryptography only to generate a random key, then verify with pure Python math ===

# Generate new EC key pair on secp256r1 (aka P-256)
private_key = ec.generate_private_key(ec.SECP256R1())

nums = private_key.private_numbers()
pub_nums = nums.public_numbers

d = nums.private_value      # private scalar as int
x = pub_nums.x              # public x-coordinate as int
y = pub_nums.y              # public y-coordinate as int

print("d =", d)
print("x (cryptography) =", x)
print("y (cryptography) =", y)

G = (Gx, Gy)
assert is_on_curve(G), "Base point G is not on curve"

calc_x, calc_y = scalar_mult(d, G)

print("x (pure Python) =", calc_x)
print("y (pure Python) =", calc_y)
print("x matches:", calc_x == x)
print("y matches:", calc_y == y)


