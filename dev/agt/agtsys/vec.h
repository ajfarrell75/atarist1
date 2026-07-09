//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2020
//======================================================================================================================
// 	scalar, point & rectangle fixedpoint types
//----------------------------------------------------------------------------------------------------------------------

#ifndef vec_h_
#define vec_h_

// ---------------------------------------------------------------------------------------------------------------------

static agt_inline s16 as_i(const s32 &_f) { return (_f >> 16); }
static agt_inline s32 as_f(const s16 &_i) { return (s32(_i) << 16); }

// ---------------------------------------------------------------------------------------------------------------------
//	vec1_t: scalar/1D value type
// ---------------------------------------------------------------------------------------------------------------------

class vec1_t
{
public:

	vec1_t()
	{
	}

	vec1_t(const vec1_t &_o)
		: f(_o.f)
	{
	}

	explicit vec1_t(s32 _f)
		: f(_f)
	{
	}

	explicit vec1_t(s16 _i)
		: f(s32(_i)<<16)
	{
	}

	// direct assignment allowed from basic types, in vec1_t (but not vec2_t)
	vec1_t & operator =(const s16 &_o)    { f  = as_f(_o); return *this; } 
	vec1_t & operator =(const s32 &_o)    { f  = _o; return *this; } 

	vec1_t & operator =(const vec1_t &_o) { f  = _o.f; f  = _o.f; return *this; } 
	vec1_t & operator+=(const vec1_t &_o) { f += _o.f; f += _o.f; return *this; } 
	vec1_t & operator-=(const vec1_t &_o) { f -= _o.f; f -= _o.f; return *this; } 
	vec1_t   operator+ (const vec1_t &_o) const { return vec1_t(f + _o.f); } 
	vec1_t   operator- (const vec1_t &_o) const { return vec1_t(f - _o.f); } 

	void addi(s16 _i) { i += _i; }
	void subi(s16 _i) { i -= _i; }
	void addf(s32 _f) { f += _f; }
	void subf(s32 _f) { f -= _f; }

	void neg() { f = -f; }

	void seti(s16 _i) { f = s32(_i)<<16; }
	void setf(s32 _f) { f = _f; }

	union { s16 i; s32 f; };
};

static agt_inline s16 i_distance(const vec1_t &_a, const vec1_t &_b) 
{
	return _b.i - _a.i;
}

// ---------------------------------------------------------------------------------------------------------------------
//	vec2_t: vector/2D value type
// ---------------------------------------------------------------------------------------------------------------------

class vec2_t
{
public:

	vec2_t()
	{
	}

	vec2_t(const vec2_t &_o)
		: fx(_o.fx), fy(_o.fy)
	{
	}

	vec2_t(const vec1_t &_ox, const vec1_t &_oy)
		: fx(_ox.f), fy(_oy.f)
	{
	}

	explicit vec2_t(s32 _fx, s32 _fy)
		: fx(_fx), fy(_fy)
	{
	}

	explicit vec2_t(s16 _ix, s16 _iy)
		: fx(s32(_ix)<<16), fy(s32(_iy)<<16)
	{
	}

	vec2_t & operator =(const vec2_t &_o) { fx  = _o.fx; fy  = _o.fy; return *this; } 
	vec2_t & operator+=(const vec2_t &_o) { fx += _o.fx; fy += _o.fy; return *this; } 
	vec2_t & operator-=(const vec2_t &_o) { fx -= _o.fx; fy -= _o.fy; return *this; } 
	vec2_t   operator+ (const vec2_t &_o) const { return vec2_t(fx + _o.fx, fy + _o.fy); } 
	vec2_t   operator- (const vec2_t &_o) const { return vec2_t(fx - _o.fx, fy - _o.fy); } 

	void addi(s16 _ix, s16 _iy) { ix += _ix; iy += _iy; }
	void subi(s16 _ix, s16 _iy) { ix -= _ix; iy -= _iy; }
	void addf(s32 _fx, s32 _fy) { fx += _fx; fy += _fy; }
	void subf(s32 _fx, s32 _fy) { fx -= _fx; fy -= _fy; }

	void add(const vec1_t &_ox, const vec1_t &_oy) { fx += _ox.f; fy += _oy.f; }
	void sub(const vec1_t &_ox, const vec1_t &_oy) { fx -= _ox.f; fy -= _oy.f; }

	void neg() { fx = -fx; fy = -fy; }
	void negx() { fx = -fx; }
	void negy() { fy = -fy; }

	void seti(s16 _ix, s16 _iy) { fx = s32(_ix)<<16; fy = s32(_iy)<<16; }
	void setf(s32 _fx, s32 _fy) { fx = _fx; fy = _fy; }

	void set(const vec1_t &_ox, const vec1_t &_oy) { fx = _ox.f; fy = _oy.f; }

	union { s16 ix; s32 fx; };
	union { s16 iy; s32 fy; };
};

static agt_inline s16 ix_distance(const vec2_t &_a, const vec2_t &_b) 
{
	return _b.ix - _a.ix;
}

static agt_inline s16 iy_distance(const vec2_t &_a, const vec2_t &_b) 
{
	return _b.iy - _a.iy;
}

// ---------------------------------------------------------------------------------------------------------------------
//	rect_t: 2D rectangle (x,y,w,h) type
// ---------------------------------------------------------------------------------------------------------------------

class rect_t
{
public:

	rect_t()
	{
	}

	rect_t(const vec2_t &_p, const vec2_t &_s)
		: p(_p), s(_s)
	{
	}

	rect_t(s32 _fx, s32 _fy, s32 _fw, s32 _fh)
		: p(_fx, _fy), s(_fw, _fh)
	{
	}

	rect_t(s16 _ix, s16 _iy, s16 _iw, s16 _ih)
		: p(_ix, _iy), s(_iw, _ih)
	{
	}
/*
	bool containsf(const vec2_t &_p)
	{
		// todo
	}

	bool containsf(const rect_t &_r)
	{
		// todo
	}

	bool containsi(const vec2_t &_p)
	{
		// todo
	}

	bool containsi(const rect_t &_r)
	{
		// todo
	}
*/
	agt_inline void from_points(const vec2_t &_p1, const vec2_t &_p2)
	{
		p = _p1;
		s = _p2 - _p1;
	}

	agt_inline void to_points(vec2_t &_p1, vec2_t &_p2) const
	{
		_p1 = p;
		_p2 = p + s;
	}

	vec2_t p;
	vec2_t s;
};

// ---------------------------------------------------------------------------------------------------------------------

#endif // vec_h_
