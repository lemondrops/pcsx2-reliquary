// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

namespace PS2FloatDetail
{
struct BoothRecode
{
	u32 data;
	u32 negate;
};

struct AddResult
{
	u32 lo;
	u32 hi;
};

static __fi BoothRecode Booth(u32 a, u32 b, u32 bit)
{
	u32 test = (bit ? b >> (bit * 2 - 1) : b << 1) & 7;
	a <<= (bit * 2);
	a += (test == 3 || test == 4) ? a : 0;
	u32 neg = (test >= 4 && test <= 6) ? ~0u : 0;
	u32 pos = 1 << (bit * 2);
	a ^= (neg & -pos);
	a &= (test >= 1 && test <= 6) ? ~0u : 0;
	return {a, neg & pos};
}

static __fi AddResult Add3(u32 a, u32 b, u32 c)
{
	u32 u = a ^ b;
	return {u ^ c, ((u & c) | (a & b)) << 1};
}

static __fi u64 MulMantissa(u32 a, u32 b)
{
	u64 full = static_cast<u64>(a) * static_cast<u64>(b);
	BoothRecode b0 = Booth(a, b, 0);
	BoothRecode b1 = Booth(a, b, 1);
	BoothRecode b2 = Booth(a, b, 2);
	BoothRecode b3 = Booth(a, b, 3);
	BoothRecode b4 = Booth(a, b, 4);
	BoothRecode b5 = Booth(a, b, 5);
	BoothRecode b6 = Booth(a, b, 6);
	BoothRecode b7 = Booth(a, b, 7);

	AddResult t0 = Add3(b1.data, b2.data, b3.data);
	AddResult t1 = Add3(b4.data & ~0x7ffu, b5.data & ~0xfffu, b6.data);
	t1.hi |= b6.negate | (b5.data & 0x800);
	b7.data |= (b5.data & 0x400) + b5.negate;

	AddResult t2 = Add3(b0.data, t0.lo, t0.hi);
	AddResult t3 = Add3(b7.data, t1.lo, t1.hi);
	AddResult t4 = Add3(t2.hi, t3.lo, t3.hi);
	AddResult t5 = Add3(t2.lo, t4.lo, t4.hi);

	t5.hi += b7.negate;
	t5.lo &= ~0x7fffu;
	t5.hi &= ~0x7fffu;
	u32 ps2lo = t5.lo + t5.hi;
	return full - ((ps2lo ^ full) & 0x8000);
}
} // namespace PS2FloatDetail

inline __fi PS2Float PS2Float::Mul(PS2Float mulend)
{
	if (IsDenormalized() || mulend.IsDenormalized() || IsZero() || mulend.IsZero())
		return PS2Float(DetermineMultiplicationDivisionOperationSign(*this, mulend), 0, 0);

	return DoMul(mulend);
}

inline __fi PS2Float PS2Float::DoMul(PS2Float other)
{
	u8 selfExponent = Exponent();
	u8 otherExponent = other.Exponent();
	u32 selfMantissa = Mantissa() | 0x800000;
	u32 otherMantissa = other.Mantissa() | 0x800000;
	u32 sign = (raw ^ other.raw) & SIGNMASK;

	s32 resExponent = selfExponent + otherExponent - 127;
	u32 resMantissa = (u32)(PS2FloatDetail::MulMantissa(selfMantissa, otherMantissa) >> MANTISSA_BITS);

	if (resMantissa > 0xFFFFFF)
	{
		resMantissa >>= 1;
		resExponent++;
	}

	if (resExponent > 255)
	{
		PS2Float result = PS2Float(sign | MAX_FLOATING_POINT_VALUE);
		result.SetOverflow();
		return result;
	}
	else if (resExponent < 1)
	{
		PS2Float result = PS2Float(sign);
		result.SetUnderflow();
		return result;
	}

	return PS2Float(sign | (u32)(resExponent << MANTISSA_BITS) | (resMantissa & 0x7FFFFF));
}
