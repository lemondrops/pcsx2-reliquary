// SPDX-FileCopyrightText: 2002-2024 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include <stdexcept>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <bit>
#include "common/Pcsx2Defs.h"
#include "common/BitUtils.h"
#include "PS2Float.h"
#include "Common.h"

//****************************************************************
// Radix Divisor
// Algorithm reference: DOI 10.1109/ARITH.1995.465363
//****************************************************************

struct CSAResult
{
	u32 sum;
	u32 carry;
};

static __fi CSAResult CSA(u32 a, u32 b, u32 c)
{
	u32 u = a ^ b;
	u32 h = (a & b) | (u & c);
	u32 l = u ^ c;
	return {l, h << 1};
}

static __fi s32 quotientSelect(CSAResult current)
{
	// Note: Decimal point is between bits 24 and 25
	constexpr u32 mask = (1 << 24) - 1; // Bit 23 needs to be or'd in instead of added
	const s32 test = ((current.sum & ~mask) + current.carry) | (current.sum & mask);
	return (test >= (1 << 23)) - (test < static_cast<s32>(~0u << 24));
}

static __fi u32 mantissa(u32 x)
{
	return (x & 0x7fffff) | 0x800000;
}

static __fi u32 exponent(u32 x)
{
	return (x >> 23) & 0xff;
}

//****************************************************************
// Float Processor
//****************************************************************

PS2Float PS2Float::MulAdd(PS2Float opsend, PS2Float optend)
{
	PS2Float mulres = opsend.Mul(optend);
	return AddMulResult(mulres);
}

PS2Float PS2Float::AddMulResult(PS2Float mulres)
{
	PS2Float addres = Add(mulres);
	u32 rawres = addres.raw;
	bool oflw = addres.HasOverflow();
	bool uflw = addres.HasUnderflow();
	DetermineMacException(3, raw, HasOverflow(), mulres.HasOverflow(), mulres.Sign() ? 1 : 0, rawres, oflw, uflw);
	PS2Float result = PS2Float(rawres);
	result.SetOverflow(oflw);
	result.SetUnderflow(uflw);
	return result;
}

PS2Float PS2Float::MulAddAcc(PS2Float opsend, PS2Float optend)
{
	PS2Float mulres = opsend.Mul(optend);
	PS2Float addres = Add(mulres);
	u32 rawres = addres.raw;
	bool oflw = addres.HasOverflow();
	bool uflw = addres.HasUnderflow();
	DetermineMacException(8, raw, HasOverflow(), mulres.HasOverflow(), mulres.Sign() ? 1 : 0, rawres, oflw, uflw);
	raw = rawres;
	SetOverflow(oflw);
	PS2Float result = PS2Float(rawres);
	result.SetOverflow(oflw);
	result.SetUnderflow(uflw);
	return result;
}

PS2Float PS2Float::MulSub(PS2Float opsend, PS2Float optend)
{
	PS2Float mulres = opsend.Mul(optend);
	return SubMulResult(mulres);
}

PS2Float PS2Float::SubMulResult(PS2Float mulres)
{
	PS2Float subres = Sub(mulres);
	u32 rawres = subres.raw;
	bool oflw = subres.HasOverflow();
	bool uflw = subres.HasUnderflow();
	DetermineMacException(4, raw, HasOverflow(), mulres.HasOverflow(), mulres.Sign() ? 1 : 0, rawres, oflw, uflw);
	PS2Float result = PS2Float(rawres);
	result.SetOverflow(oflw);
	result.SetUnderflow(uflw);
	return result;
}

PS2Float PS2Float::MulSubAcc(PS2Float opsend, PS2Float optend)
{
	PS2Float mulres = opsend.Mul(optend);
	PS2Float subres = Sub(mulres);
	u32 rawres = subres.raw;
	bool oflw = subres.HasOverflow();
	bool uflw = subres.HasUnderflow();
	DetermineMacException(9, raw, HasOverflow(), mulres.HasOverflow(), mulres.Sign() ? 1 : 0, rawres, oflw, uflw);
	raw = rawres;
	SetOverflow(oflw);
	PS2Float result = PS2Float(rawres);
	result.SetOverflow(oflw);
	result.SetUnderflow(uflw);
	return result;
}

PS2Float PS2Float::Div(PS2Float divend)
{
	u32 a = raw;
	u32 b = divend.raw;
	if (((a & 0x7F800000) == 0) && ((b & 0x7F800000) != 0))
	{
		u32 floatResult = 0;
		floatResult &= PS2Float::MAX_FLOATING_POINT_VALUE;
		floatResult |= (u32)(((s32)(b >> 31) != (s32)(a >> 31)) ? 1 : 0 & 1) << 31;
		return PS2Float(floatResult);
	}
	if (((a & 0x7F800000) != 0) && ((b & 0x7F800000) == 0))
	{
		u32 floatResult = PS2Float::MAX_FLOATING_POINT_VALUE;
		floatResult &= PS2Float::MAX_FLOATING_POINT_VALUE;
		floatResult |= (u32)(((s32)(b >> 31) != (s32)(a >> 31)) ? 1 : 0 & 1) << 31;
		PS2Float result = PS2Float(floatResult);
		result.SetDivideByZero();
		return result;
	}
	if (((a & 0x7F800000) == 0) && ((b & 0x7F800000) == 0))
	{
		u32 floatResult = PS2Float::MAX_FLOATING_POINT_VALUE;
		floatResult &= PS2Float::MAX_FLOATING_POINT_VALUE;
		floatResult |= (u32)(((s32)(b >> 31) != (s32)(a >> 31)) ? 1 : 0 & 1) << 31;
		PS2Float result = PS2Float(floatResult);
		result.SetInvalid();
		return result;
	}
	u32 sign = ((a ^ b) & 0x80000000);
	u32 Dvdtexp = exponent(a);
	u32 Dvsrexp = exponent(b);
	s32 cexp = Dvdtexp - Dvsrexp + 126;
	if (cexp > 255)
	{
		PS2Float result = PS2Float(sign | PS2Float::MAX_FLOATING_POINT_VALUE);
		result.SetOverflow();
		return result;
	}
	else if (cexp < 0)
	{
		PS2Float result = PS2Float(sign);
		result.SetUnderflow();
		return result;
	}
	u32 am = mantissa(a) << 2;
	u32 bm = mantissa(b) << 2;
	struct CSAResult current = {am, 0};
	u32 quotient = 0;
	int quotientBit = 1;
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
	for (int i = 0; i < 25; i++)
	{
		quotient = (quotient << 1) + quotientBit;
		u32 add = quotientBit > 0 ? ~bm : quotientBit < 0 ? bm : 0;
		current.carry += quotientBit > 0;
		struct CSAResult csa = CSA(current.sum, current.carry, add);
		quotientBit = quotientSelect(quotientBit ? csa : current);
		current.sum = csa.sum << 1;
		current.carry = csa.carry << 1;
	}
	if (quotient >= (1 << 24))
	{
		cexp += 1;
		quotient >>= 1;
	}
	if (Dvdtexp == 0 && Dvsrexp == 0)
	{
		PS2Float result = PS2Float(sign | PS2Float::MAX_FLOATING_POINT_VALUE);
		result.SetInvalid();
		return result;
	}
	else if (Dvdtexp == 0 || Dvsrexp != 0)
	{
		if (Dvdtexp == 0 && Dvsrexp != 0) { return PS2Float(sign); }
	}
	else
	{
		PS2Float result = PS2Float(sign | PS2Float::MAX_FLOATING_POINT_VALUE);
		result.SetDivideByZero();
		return result;
	}
	if (cexp > 255)
	{
		PS2Float result = PS2Float(sign | PS2Float::MAX_FLOATING_POINT_VALUE);
		result.SetOverflow();
		return result;
	}
	else if (cexp < 1)
	{
		PS2Float result = PS2Float(sign);
		result.SetUnderflow();
		return result;
	}
	return (quotient & 0x7fffff) | (cexp << 23) | sign;
}

PS2Float PS2Float::Sqrt()
{
	u32 a = raw;
	if ((a & 0x7F800000) == 0)
	{
		PS2Float result = PS2Float(0);
		result.SetInvalid(((a >> 31) & 1) != 0);
		return result;
	}
	u32 m = mantissa(a) << 1;
	if (!(a & 0x800000)) // If exponent is odd after subtracting bias of 127
		m <<= 1;
	struct CSAResult current = {m, 0};
	u32 quotient = 0;
	s32 quotientBit = 1;
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
	for (s32 i = 0; i < 25; i++)
	{
		// Adding n to quotient adds n * (2*quotient + n) to quotient^2
		// (which is what we need to subtract from the remainder)
		u32 adjust = quotient + (quotientBit << (24 - i));
		quotient += quotientBit << (25 - i);
		u32 add = quotientBit > 0 ? ~adjust : quotientBit < 0 ? adjust : 0;
		current.carry += quotientBit > 0;
		struct CSAResult csa = CSA(current.sum, current.carry, add);
		quotientBit = quotientSelect(quotientBit ? csa : current);
		current.sum = csa.sum << 1;
		current.carry = csa.carry << 1;
	}
	s32 Dvdtexp = exponent(a);
	if (Dvdtexp == 0)
		return PS2Float(0);
	Dvdtexp = (Dvdtexp + 127) >> 1;
	PS2Float result = PS2Float(((quotient >> 2) & 0x7fffff) | (Dvdtexp << 23));
	if (Sign())
	{
		if (result.Sign())
			result = result.Negate();
		result.SetInvalid();
	}
	return result;
}

PS2Float PS2Float::Rsqrt(PS2Float other)
{
	PS2Float sqrt = PS2Float(false, other.Exponent(), other.Mantissa()).Sqrt();
	PS2Float div = Div(sqrt);
	PS2Float result = PS2Float(div.raw);
	result.SetDivideByZero(sqrt.HasDivideByZero() || div.HasDivideByZero());
	result.SetInvalid(sqrt.HasInvalid() || div.HasInvalid());
	result.SetOverflow(div.HasOverflow());
	result.SetUnderflow(div.HasUnderflow());
	return result;
}

PS2Float PS2Float::ERCPR()
{
	return PS2Float(ONE).Div(*this);
}

PS2Float PS2Float::ESQRT()
{
	return Sqrt();
}

PS2Float PS2Float::ESQUR()
{
	return Mul(*this);
}

PS2Float PS2Float::ERSQRT()
{
	return PS2Float(ONE).Rsqrt(*this);
}

s32 PS2Float::CompareTo(PS2Float other)
{
	s32 selfTwoComplementVal = (s32)Abs();
	if (Sign())
		selfTwoComplementVal = -selfTwoComplementVal;

	s32 otherTwoComplementVal = (s32)other.Abs();
	if (other.Sign())
		otherTwoComplementVal = -otherTwoComplementVal;

	if (selfTwoComplementVal < otherTwoComplementVal)
		return -1;
	else if (selfTwoComplementVal == otherTwoComplementVal)
		return 0;
	else
		return 1;
}

s32 PS2Float::CompareOperands(PS2Float other)
{
	u32 selfTwoComplementVal = Abs();
	u32 otherTwoComplementVal = other.Abs();

	if (selfTwoComplementVal < otherTwoComplementVal)
		return -1;
	else if (selfTwoComplementVal == otherTwoComplementVal)
		return 0;
	else
		return 1;
}

double PS2Float::ToDouble()
{
	return std::bit_cast<double>(((u64)Sign() << 63) | ((((u64)Exponent() - BIAS) + 1023ULL) << 52) | ((u64)Mantissa() << 29));
}

std::string PS2Float::ToString()
{
	double res = ToDouble();

	u32 value = raw;
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(6);

	if (IsDenormalized())
	{
		oss << "Denormalized(" << res << ")";
	}
	else if (value == MAX_FLOATING_POINT_VALUE)
	{
		oss << "Fmax(" << res << ")";
	}
	else if (value == MIN_FLOATING_POINT_VALUE)
	{
		oss << "-Fmax(" << res << ")";
	}
	else
	{
		oss << "PS2Float(" << res << ")";
	}

	return oss.str();
}

u8 PS2Float::Clip(u32 f1, u32 f2, bool& cplus, bool& cminus)
{
	bool resultPlus = false;
	bool resultMinus = false;
	u32 a;

	if ((f1 & 0x7F800000) == 0)
	{
		f1 &= 0xFF800000;
	}

	a = f1;

	if ((f2 & 0x7F800000) == 0)
	{
		f2 &= 0xFF800000;
	}

	f1 = f1 & MAX_FLOATING_POINT_VALUE;
	f2 = f2 & MAX_FLOATING_POINT_VALUE;

	if ((-1 < (int)a) && (f2 < f1))
		resultPlus = true;

	cplus = resultPlus;

	if (((int)a < 0) && (f2 < f1))
		resultMinus = true;

	cminus = resultMinus;

	return 0;
}

bool PS2Float::DetermineMultiplicationDivisionOperationSign(PS2Float a, PS2Float b)
{
	return a.Sign() ^ b.Sign();
}

bool PS2Float::DetermineAdditionOperationSign(PS2Float a, PS2Float b)
{
	return a.CompareOperands(b) >= 0 ? a.Sign() : b.Sign();
}

bool PS2Float::DetermineSubtractionOperationSign(PS2Float a, PS2Float b)
{
	return a.CompareOperands(b) >= 0 ? a.Sign() : !b.Sign();
}

u8 PS2Float::DetermineMacException(u8 mode, u32 acc, bool acc_oflw, bool moflw, s32 msign, u32& addsubres, bool& oflw, bool& uflw)
{
	bool roundToMax;

	if ((mode == 3) || (mode == 8))
		roundToMax = msign == 0;
	else
	{
		if ((mode != 4) && (mode != 9))
		{
			Console.Error("Unhandled MacFlag operation flags");
			return 1;
		}

		roundToMax = msign != 0;
	}

	if (!acc_oflw)
	{
		if (moflw)
		{
			if (roundToMax)
			{
				addsubres = MAX_FLOATING_POINT_VALUE;
				uflw = false;
				oflw = true;
			}
			else
			{
				addsubres = MIN_FLOATING_POINT_VALUE;
				uflw = false;
				oflw = true;
			}
		}
	}
	else if (!moflw)
	{
		addsubres = acc;
		uflw = false;
		oflw = true;
	}
	else if (roundToMax)
	{
		addsubres = MAX_FLOATING_POINT_VALUE;
		uflw = false;
		oflw = true;
	}
	else
	{
		addsubres = MIN_FLOATING_POINT_VALUE;
		uflw = false;
		oflw = true;
	}

	return 0;
}
