extern crate curve25519_dalek;

use curve25519_dalek::scalar::Scalar;
use curve25519_dalek::edwards::{CompressedEdwardsY, EdwardsPoint};
use curve25519_dalek::constants::ED25519_BASEPOINT_TABLE;
use std::ptr;

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_scalar_zero() -> *mut Scalar {
    Box::into_raw(Box::new(Scalar::zero()))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_expand_scalar(bytes: *const [u8; 32]) -> *mut Scalar {
    Box::into_raw(Box::new(Scalar::from_bytes_mod_order(*bytes)))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_expand_scalar_wide(bytes: *const [u8; 64]) -> *mut Scalar {
    Box::into_raw(Box::new(Scalar::from_bytes_mod_order_wide(&*bytes)))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_add_scalars(scalar1: *const Scalar, scalar2: *const Scalar) -> *mut Scalar {
    Box::into_raw(Box::new(&*scalar1 + &*scalar2))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_multiply_scalars(scalar1: *const Scalar, scalar2: *const Scalar) -> *mut Scalar {
    Box::into_raw(Box::new(&*scalar1 * &*scalar2))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_clone_scalar(scalar: *const Scalar) -> *mut Scalar {
    if scalar.is_null () {
        return ptr::null_mut ();
    }
    Box::into_raw(Box::new((&*scalar).clone()))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_compress_scalar(out_bytes: *mut [u8; 32], scalar: *const Scalar) {
    (&mut *out_bytes).copy_from_slice((&*scalar).as_bytes())
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_destroy_scalar(scalar: *mut Scalar) {
    Box::from_raw(scalar);
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_multiply_scalar_by_basepoint(scalar: *const Scalar) -> *mut EdwardsPoint {
    Box::into_raw(Box::new(&*scalar * &ED25519_BASEPOINT_TABLE))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_expand_curve_point(bytes: *const [u8; 32]) -> *mut EdwardsPoint {
    match CompressedEdwardsY(*bytes).decompress() {
        Some(point) => Box::into_raw(Box::new(point)),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_add_curve_points(curve_point1: *const EdwardsPoint, curve_point2: *const EdwardsPoint) -> *mut EdwardsPoint {
    Box::into_raw(Box::new(&*curve_point1 + &*curve_point2))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_multiply_curve_point_by_scalar(curve_point: *const EdwardsPoint, scalar: *const Scalar) -> *mut EdwardsPoint {
    Box::into_raw(Box::new(&*curve_point * &*scalar))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_clone_curve_point(curve_point: *const EdwardsPoint) -> *mut EdwardsPoint {
    if curve_point.is_null () {
        return ptr::null_mut ();
    }
    Box::into_raw(Box::new((&*curve_point).clone()))
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_compress_curve_point(out_bytes: *mut [u8; 32], curve_point: *const EdwardsPoint) {
    *out_bytes = (&*curve_point).compress().to_bytes();
}

#[no_mangle]
pub unsafe extern "C" fn curve25519_dalek_destroy_curve_point(curve_point: *mut EdwardsPoint) {
    Box::from_raw(curve_point);
}
