//---------------------------------------------------------------------------------------------------------------------
//	[A]tari [G]ame [T]ools / dml 2016
//---------------------------------------------------------------------------------------------------------------------
//	random source
//---------------------------------------------------------------------------------------------------------------------

#ifndef tools_rnd_h_
#define tools_rnd_h_

#if !defined(tools_common_h_)
#error "requires include: common/common.h"
#endif

// --------------------------------------------------------------------

#include <algorithm>

// --------------------------------------------------------------------
// object representing PRNG twister state
// --------------------------------------------------------------------

//==================================================================================================
//==================================================================================================

class mersenne_twister
{
public:
	
	enum UniqueSeedTag
	{
		UniqueSeed
	};
	
	explicit inline mersenne_twister(uint32_t seed = 19650218UL)
		: lastVal_(0)
		, state_(new uint32_t[kN])
		, p0_(NULL)
		, p1_(NULL)
		, pm_(NULL)
	{
		reset(seed);
	}

	mersenne_twister(UniqueSeedTag);

	mersenne_twister(const mersenne_twister & rhs)
		: lastVal_(rhs.lastVal_)
		, state_(new uint32_t[kN])
	{
		memcpy(state_, rhs.state_, kN * sizeof(uint32_t));
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

	inline void reset(uint32_t seed)
	{
		uint32_t * s = state_;
		uint32_t * r = state_;
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
	inline uint32_t genU32()
	{
		next();
		return lastVal_;
	}

	inline uint32_t genU32(uint32_t minVal, uint32_t maxVal)
	{
		next();
		if(minVal >= maxVal)
			return minVal;
		uint32_t v = uint32_t((maxVal - minVal + 1) * genDouble()) + minVal;
		return v <= maxVal ? v : maxVal;
	}

	inline int32_t genS32()
	{
		next();
		return int32_t(lastVal_);
	}

	inline int32_t genS32(int32_t minVal, int32_t maxVal)
	{
		next();
		if(minVal >= maxVal)
			return minVal;
		int32_t v = int32_t((maxVal - minVal + 1) * genDouble()) + minVal;
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

	inline uint32_t genU32() const
	{
		return lastVal_;
	}

	inline uint32_t genU32(uint32_t minVal, uint32_t maxVal) const
	{
		if(minVal >= maxVal)
			return minVal;
		mersenne_twister tmp(*this);
		uint32_t v = uint32_t((maxVal - minVal + 1) * tmp.genDouble()) + minVal;
		return v <= maxVal ? v : maxVal;
	}

	inline int32_t genS32() const
	{
		return int32_t(lastVal_);
	}

	inline int32_t genS32(int32_t minVal, int32_t maxVal) const
	{
		if(minVal >= maxVal)
			return minVal;
		mersenne_twister tmp(*this);
		int32_t v = int32_t((maxVal - minVal + 1) * tmp.genDouble()) + minVal;
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
		uint32_t v = twist(*pm_++, *p0_, *p1_);
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

	inline uint32_t hiBit(uint32_t u) const
	{
		return u & 0x80000000UL;
	}

	inline uint32_t loBit(uint32_t u) const
	{
		return u & 0x00000001UL;
	}

	inline uint32_t loBits(uint32_t u) const
	{
		return u & 0x7fffffffUL;
	}

	inline uint32_t mixBits(uint32_t u, uint32_t v) const
	{
		return hiBit(u) | loBits(v);
	}

	inline uint32_t twist(uint32_t m, uint32_t s0, uint32_t s1 ) const
	{
		return m ^ (mixBits(s0,s1) >> 1) ^ (uint32_t(-int32_t(loBit(s1))) & 0x9908b0dfUL);
	}

	enum
	{
		kN = 624,
		kM = 397
	};

	uint32_t lastVal_;
	uint32_t * state_;
	uint32_t * p0_;
	uint32_t * p1_;
	uint32_t * pm_;
};

// --------------------------------------------------------------------

#endif // tools_rnd_h_
