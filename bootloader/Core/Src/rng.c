#include "rng.h"

static uint32_t rngState0 = 0xA341316CU;
static uint32_t rngState1 = 0xC8013EA4U;
static uint32_t rngState2 = 0x9E3779B9U;

static uint32_t rotl32(uint32_t x, uint8_t k)
{
	return (x << k) | (x >> (32U - k));
}

uint16_t rngGet(void)
{
	uint32_t s0 = rngState0;
	uint32_t s1 = rngState1;
	uint32_t outXoroshiro = s0 + s1;
	uint32_t outLcg;

	/* xoroshiro64-style state transition: fast and better quality than LCG */
	s1 ^= s0;
	rngState0 = rotl32(s0, 26U) ^ s1 ^ (s1 << 9U);
	rngState1 = rotl32(s1, 13U);

	/* Classic 32-bit LCG step (wraparound arithmetic in uint32_t). */
	rngState2 = rngState2 * 1664525U + 1013904223U;
	outLcg = rngState2;

	/* Mix two generators: xoroshiro-style stream XOR LCG stream. */
	outXoroshiro ^= outLcg;

	/* Fold 32-bit output into 16-bit while keeping both halves mixed. */
	outXoroshiro ^= outXoroshiro >> 16U;
	return (uint16_t)outXoroshiro;
}

void rngAdd(uint32_t value)
{
	/* Jenkins-style integer mixing with only shifts/xors/adds. */
	value += value << 10U;
	value ^= value >> 6U;
	value += value << 3U;
	value ^= value >> 11U;
	value += value << 15U;

	rngState0 ^= value;
	rngState1 += rotl32(value, 16U) ^ 0x9E3779B9U;
	rngState2 ^= rotl32(value, 7U) + 0x85EBCA6BU;

	if ((rngState0 | rngState1 | rngState2) == 0U)
	{
		rngState1 = 1U;
		rngState2 = 0x9E3779B9U;
	}

	/* Diffuse newly added entropy through the state. */
	(void)rngGet();
}
