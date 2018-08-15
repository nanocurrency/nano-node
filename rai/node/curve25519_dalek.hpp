extern "C" {
struct curve25519_dalek_scalar;
struct curve25519_dalek_curve_point;

curve25519_dalek_scalar * curve25519_dalek_scalar_zero ();
curve25519_dalek_scalar * curve25519_dalek_expand_scalar (uint8_t const *);
curve25519_dalek_scalar * curve25519_dalek_expand_scalar_wide (uint8_t const *);
curve25519_dalek_scalar * curve25519_dalek_add_scalars (curve25519_dalek_scalar const *, curve25519_dalek_scalar const *);
curve25519_dalek_scalar * curve25519_dalek_multiply_scalars (curve25519_dalek_scalar const *, curve25519_dalek_scalar const *);
curve25519_dalek_scalar * curve25519_dalek_clone_scalar (curve25519_dalek_scalar const *);
void curve25519_dalek_compress_scalar (uint8_t *, curve25519_dalek_scalar const *);
void curve25519_dalek_destroy_scalar (curve25519_dalek_scalar *);

curve25519_dalek_curve_point * curve25519_dalek_multiply_scalar_by_basepoint (curve25519_dalek_scalar const *);
curve25519_dalek_curve_point * curve25519_dalek_expand_curve_point (uint8_t const *);
curve25519_dalek_curve_point * curve25519_dalek_add_curve_points (curve25519_dalek_curve_point const *, curve25519_dalek_curve_point const *);
curve25519_dalek_curve_point * curve25519_dalek_multiply_curve_point_by_scalar (curve25519_dalek_curve_point const *, curve25519_dalek_scalar const *);
curve25519_dalek_curve_point * curve25519_dalek_clone_curve_point (curve25519_dalek_curve_point const *);
void curve25519_dalek_compress_curve_point (uint8_t *, curve25519_dalek_curve_point const *);
void curve25519_dalek_destroy_curve_point (curve25519_dalek_curve_point *);
}

namespace rai
{
class curve25519_curve_point;

class curve25519_scalar
{
	friend curve25519_curve_point;

protected:
	curve25519_dalek_scalar * inner;
	curve25519_scalar (curve25519_dalek_scalar * inner_a) :
	inner (inner_a)
	{
	}

public:
	curve25519_scalar () :
	inner (curve25519_dalek_scalar_zero ())
	{
	}
	curve25519_scalar (uint8_t const * bytes, size_t width = 32) :
	inner (width == 64 ? curve25519_dalek_expand_scalar_wide (bytes) : curve25519_dalek_expand_scalar (bytes))
	{
		assert (width == 32 || width == 64);
		assert (inner != nullptr);
	}
	curve25519_scalar (const curve25519_scalar & other) :
	inner (curve25519_dalek_clone_scalar (other.inner))
	{
		assert (inner != nullptr);
	}
	~curve25519_scalar ()
	{
		curve25519_dalek_destroy_scalar (inner);
	}
	curve25519_scalar & operator= (const curve25519_scalar & other)
	{
		if (inner != other.inner)
		{
			curve25519_dalek_destroy_scalar (inner);
			inner = curve25519_dalek_clone_scalar (other.inner);
			assert (inner != nullptr);
		}
		return *this;
	}
	curve25519_scalar operator+ (const curve25519_scalar & other) const
	{
		return curve25519_scalar (curve25519_dalek_add_scalars (inner, other.inner));
	}
	curve25519_scalar operator* (const curve25519_scalar & other) const
	{
		return curve25519_scalar (curve25519_dalek_multiply_scalars (inner, other.inner));
	}
	std::array<uint8_t, 32> to_bytes () const
	{
		std::array<uint8_t, 32> result;
		curve25519_dalek_compress_scalar (result.data (), inner);
		return result;
	}
};

class curve25519_curve_point
{
protected:
	curve25519_dalek_curve_point * inner;
	curve25519_curve_point (curve25519_dalek_curve_point * inner_a) :
	inner (inner_a)
	{
	}
	// Warning: inner may be null after this function.
	// Use the builder from_bytes instead
	curve25519_curve_point (uint8_t const * bytes) :
	inner (curve25519_dalek_expand_curve_point (bytes))
	{
	}

public:
	curve25519_curve_point (const curve25519_curve_point & other) :
	inner (curve25519_dalek_clone_curve_point (other.inner))
	{
		assert (inner != nullptr);
	}
	curve25519_curve_point (const curve25519_scalar & scalar) :
	inner (curve25519_dalek_multiply_scalar_by_basepoint (scalar.inner))
	{
		assert (inner != nullptr);
	}
	~curve25519_curve_point ()
	{
		curve25519_dalek_destroy_curve_point (inner);
	}
	curve25519_curve_point & operator= (const curve25519_curve_point & other)
	{
		if (inner != other.inner)
		{
			curve25519_dalek_destroy_curve_point (inner);
			inner = curve25519_dalek_clone_curve_point (other.inner);
			assert (inner != nullptr);
		}
		return *this;
	}
	static boost::optional<curve25519_curve_point> from_bytes (uint8_t const * bytes)
	{
		boost::optional<curve25519_curve_point> result;
		curve25519_curve_point obj (bytes);
		if (obj.inner)
		{
			result = obj;
		}
		return result;
	}
	curve25519_curve_point operator+ (const curve25519_curve_point & other) const
	{
		return curve25519_curve_point (curve25519_dalek_add_curve_points (inner, other.inner));
	}
	curve25519_curve_point operator* (const curve25519_scalar & other) const
	{
		return curve25519_curve_point (curve25519_dalek_multiply_curve_point_by_scalar (inner, other.inner));
	}
	std::array<uint8_t, 32> to_bytes () const
	{
		std::array<uint8_t, 32> result;
		curve25519_dalek_compress_curve_point (result.data (), inner);
		return result;
	}
};
}
