//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	high quality prandom source
//----------------------------------------------------------------------------------------------------------------------

#ifndef rnd_h_
#define rnd_h_

//----------------------------------------------------------------------------------------------------------------------

#include <algorithm>

//----------------------------------------------------------------------------------------------------------------------
// object representing PRNG twister state
//----------------------------------------------------------------------------------------------------------------------

typedef unsigned long U32;
typedef signed long S32;

class mersenne_twister
{
public:
	
	enum UniqueSeedTag
	{
		UniqueSeed
	};
	
	explicit inline mersenne_twister(U32 seed = 19650218UL)
		: lastVal_(0)
		, state_(new U32[kN])
		, p0_(NULL)
		, p1_(NULL)
		, pm_(NULL)
	{
		reset(seed);
	}

	mersenne_twister(UniqueSeedTag);

	mersenne_twister(const mersenne_twister & rhs)
		: lastVal_(rhs.lastVal_)
		, state_(new U32[kN])
	{
		memcpy(state_, rhs.state_, kN * sizeof(U32));
		p0_ = state_ + (rhs.p0_ - rhs.state_);
		p1_ = state_ + (rhs.p1_ - rhs.state_);
		pm_ = state_ + (rhs.pm_ - rhs.state_);
	}

	mersenne_twister & operator=(const mersenne_twister & rhs)
	{
		mersenne_twister tmp(rhs);
		swap(tmp);
		return *this;
	}

	inline ~mersenne_twister()
	{
		delete[] state_;
	}

	inline void swap(mersenne_twister & rhs)
	{
		std::swap(lastVal_, rhs.lastVal_);
		std::swap(state_, rhs.state_);
		std::swap(p0_, rhs.p0_);
		std::swap(p1_, rhs.p1_);
		std::swap(pm_, rhs.pm_);
	}

	inline void reset(U32 seed)
	{
		U32 * s = state_;
		U32 * r = state_;
		*s++ = seed & 0xffffffffUL;

		for(int i = 1; i < kN; ++i)
		{
			*s++ = (1812433253UL * (*r ^ (*r >> 30)) + i) & 0xffffffffUL;
			++r;
		}

		p0_ = state_;
		p1_ = state_ + 1;
		pm_ = state_ + kM;
	}

	void resetUnique();

	// non-const interface
	inline U32 genU32()
	{
		next();
		return lastVal_;
	}

	inline U32 genU32(U32 minVal, U32 maxVal)
	{
		next();
		if(minVal >= maxVal)
			return minVal;
		U32 v = U32((maxVal - minVal + 1) * genDouble()) + minVal;
		return v <= maxVal ? v : maxVal;
	}

	inline S32 genS32()
	{
		next();
		return S32(lastVal_);
	}

	inline S32 genS32(S32 minVal, S32 maxVal)
	{
		next();
		if(minVal >= maxVal)
			return minVal;
		S32 v = S32((maxVal - minVal + 1) * genDouble()) + minVal;
		return v <= maxVal ? v : maxVal;
	}

	inline float genFloat()
	{
		next();
		return float(lastVal_) * (1.0f / 4294967295.0f);
	}

	inline float genFloat(float minVal, float maxVal)
	{
		next();
		return (float(lastVal_) * ((maxVal - minVal) / 4294967295.0f)) + minVal;
	}

	inline double genDouble()
	{
		next();
		return double(lastVal_) * (1.0 / 4294967295.0);
	}

	inline double genDouble(double minVal, double maxVal)
	{
		next();
		return (double(lastVal_) * ((maxVal - minVal) / 4294967295.0)) + minVal;
	}

	// const interface

	inline U32 genU32() const
	{
		return lastVal_;
	}

	inline U32 genU32(U32 minVal, U32 maxVal) const
	{
		if(minVal >= maxVal)
			return minVal;
		mersenne_twister tmp(*this);
		U32 v = U32((maxVal - minVal + 1) * tmp.genDouble()) + minVal;
		return v <= maxVal ? v : maxVal;
	}

	inline S32 genS32() const
	{
		return S32(lastVal_);
	}

	inline S32 genS32(S32 minVal, S32 maxVal) const
	{
		if(minVal >= maxVal)
			return minVal;
		mersenne_twister tmp(*this);
		S32 v = S32((maxVal - minVal + 1) * tmp.genDouble()) + minVal;
		return v <= maxVal ? v : maxVal;
	}

	inline float genFloat() const
	{
		return float(lastVal_) * (1.0f / 4294967295.0f);
	}

	inline float genFloat(float minVal, float maxVal) const
	{
		return (float(lastVal_) * ((maxVal - minVal) / 4294967295.0f)) + minVal;
	}

	inline double genDouble() const
	{
		return double(lastVal_) * (1.0 / 4294967295.0);
	}

	inline double genDouble(double minVal, double maxVal) const
	{
		return (double(lastVal_) * ((maxVal - minVal) / 4294967295.0)) + minVal;
	}

	//

	inline void next()
	{
		U32 v = twist(*pm_++, *p0_, *p1_);
		*p0_ = v;
		p0_ = p1_++;
		if(pm_ == state_ + kN)
			pm_ = state_;
		if(p1_ == state_ + kN)
			p1_ = state_;

		v ^= v >> 11;
		v ^= (v << 7) & 0x9d2c5680UL;
		v ^= (v << 15) & 0xefc60000UL;
		lastVal_ = v ^ (v >> 18);
	}

private:

	inline U32 hiBit(U32 u) const
	{
		return u & 0x80000000UL;
	}

	inline U32 loBit(U32 u) const
	{
		return u & 0x00000001UL;
	}

	inline U32 loBits(U32 u) const
	{
		return u & 0x7fffffffUL;
	}

	inline U32 mixBits(U32 u, U32 v) const
	{
		return hiBit(u) | loBits(v);
	}

	inline U32 twist(U32 m, U32 s0, U32 s1 ) const
	{
		return m ^ (mixBits(s0,s1) >> 1) ^ (U32(-S32(loBit(s1))) & 0x9908b0dfUL);
	}

	enum
	{
		kN = 624,
		kM = 397
	};

	U32 lastVal_;
	U32 * state_;
	U32 * p0_;
	U32 * p1_;
	U32 * pm_;
};

//----------------------------------------------------------------------------------------------------------------------

#endif // rnd_h_
