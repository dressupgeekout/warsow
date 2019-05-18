/*
Copyright (C) 2002-2007 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// r_math.c

#include "r_math.h"

const mat4_t mat4x4_identity =
{
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
};

void Matrix4_Identity( mat4_t m )
{
	m[0] = 1, m[1] = m[2] = m[3] = 0;
	m[4] = 0, m[5] = 1, m[6] = m[7] = 0;
	m[8] = m[9] = 0, m[10] = 1, m[11] = 0;
	m[12] = m[13] = m[14] = 0, m[15] = 1;
}

void Matrix4_Copy( const mat4_t m1, mat4_t m2 )
{
	memcpy( m2, m1, sizeof( mat4_t ) );
}

bool Matrix4_Compare( const mat4_t m1, const mat4_t m2 )
{
	int i;

	for( i = 0; i < 16; i++ )
		if( m1[i] != m2[i] )
			return false;

	return true;
}

void Matrix4_Multiply( const mat4_t m1, const mat4_t m2, mat4_t out )
{
	out[0]  = m1[0] * m2[0] + m1[4] * m2[1] + m1[8] * m2[2] + m1[12] * m2[3];
	out[1]  = m1[1] * m2[0] + m1[5] * m2[1] + m1[9] * m2[2] + m1[13] * m2[3];
	out[2]  = m1[2] * m2[0] + m1[6] * m2[1] + m1[10] * m2[2] + m1[14] * m2[3];
	out[3]  = m1[3] * m2[0] + m1[7] * m2[1] + m1[11] * m2[2] + m1[15] * m2[3];
	out[4]  = m1[0] * m2[4] + m1[4] * m2[5] + m1[8] * m2[6] + m1[12] * m2[7];
	out[5]  = m1[1] * m2[4] + m1[5] * m2[5] + m1[9] * m2[6] + m1[13] * m2[7];
	out[6]  = m1[2] * m2[4] + m1[6] * m2[5] + m1[10] * m2[6] + m1[14] * m2[7];
	out[7]  = m1[3] * m2[4] + m1[7] * m2[5] + m1[11] * m2[6] + m1[15] * m2[7];
	out[8]  = m1[0] * m2[8] + m1[4] * m2[9] + m1[8] * m2[10] + m1[12] * m2[11];
	out[9]  = m1[1] * m2[8] + m1[5] * m2[9] + m1[9] * m2[10] + m1[13] * m2[11];
	out[10] = m1[2] * m2[8] + m1[6] * m2[9] + m1[10] * m2[10] + m1[14] * m2[11];
	out[11] = m1[3] * m2[8] + m1[7] * m2[9] + m1[11] * m2[10] + m1[15] * m2[11];
	out[12] = m1[0] * m2[12] + m1[4] * m2[13] + m1[8] * m2[14] + m1[12] * m2[15];
	out[13] = m1[1] * m2[12] + m1[5] * m2[13] + m1[9] * m2[14] + m1[13] * m2[15];
	out[14] = m1[2] * m2[12] + m1[6] * m2[13] + m1[10] * m2[14] + m1[14] * m2[15];
	out[15] = m1[3] * m2[12] + m1[7] * m2[13] + m1[11] * m2[14] + m1[15] * m2[15];
}

void Matrix4_MultiplyFast( const mat4_t m1, const mat4_t m2, mat4_t out )
{
	out[0]  = m1[0] * m2[0] + m1[4] * m2[1] + m1[8] * m2[2];
	out[1]  = m1[1] * m2[0] + m1[5] * m2[1] + m1[9] * m2[2];
	out[2]  = m1[2] * m2[0] + m1[6] * m2[1] + m1[10] * m2[2];
	out[3]  = 0.0f;
	out[4]  = m1[0] * m2[4] + m1[4] * m2[5] + m1[8] * m2[6];
	out[5]  = m1[1] * m2[4] + m1[5] * m2[5] + m1[9] * m2[6];
	out[6]  = m1[2] * m2[4] + m1[6] * m2[5] + m1[10] * m2[6];
	out[7]  = 0.0f;
	out[8]  = m1[0] * m2[8] + m1[4] * m2[9] + m1[8] * m2[10];
	out[9]  = m1[1] * m2[8] + m1[5] * m2[9] + m1[9] * m2[10];
	out[10] = m1[2] * m2[8] + m1[6] * m2[9] + m1[10] * m2[10];
	out[11] = 0.0f;
	out[12] = m1[0] * m2[12] + m1[4] * m2[13] + m1[8] * m2[14] + m1[12];
	out[13] = m1[1] * m2[12] + m1[5] * m2[13] + m1[9] * m2[14] + m1[13];
	out[14] = m1[2] * m2[12] + m1[6] * m2[13] + m1[10] * m2[14] + m1[14];
	out[15] = 1.0f;
}

// Taken from Darkplaces source code
// Adapted from code contributed to Mesa by David Moore (Mesa 7.6 under SGI Free License B - which is MIT/X11-type)
// added helper for common subexpression elimination by eihrul, and other optimizations by div0
bool Matrix4_Invert( const mat4_t in, mat4_t out )
{
	vec_t det;

	// note: orientation does not matter, as transpose(invert(transpose(m))) == invert(m), proof:
	//   transpose(invert(transpose(m))) * m
	// = transpose(invert(transpose(m))) * transpose(transpose(m))
	// = transpose(transpose(m) * invert(transpose(m)))
	// = transpose(identity)
	// = identity

	// this seems to help gcc's common subexpression elimination, and also makes the code look nicer
	vec_t m00 = in[0], m01 = in[1], m02 = in[2], m03 = in[3],
		m10 = in[4], m11 = in[5], m12 = in[6], m13 = in[7],
		m20 = in[8], m21 = in[9], m22 = in[10], m23 = in[11],
		m30 = in[12], m31 = in[13], m32 = in[14], m33 = in[15];

	// calculate the adjoint
	out[0] =  (m11*(m22*m33 - m23*m32) - m21*(m12*m33 - m13*m32) + m31*(m12*m23 - m13*m22));
	out[1] = -(m01*(m22*m33 - m23*m32) - m21*(m02*m33 - m03*m32) + m31*(m02*m23 - m03*m22));
	out[2] =  (m01*(m12*m33 - m13*m32) - m11*(m02*m33 - m03*m32) + m31*(m02*m13 - m03*m12));
	out[3] = -(m01*(m12*m23 - m13*m22) - m11*(m02*m23 - m03*m22) + m21*(m02*m13 - m03*m12));
	out[4] = -(m10*(m22*m33 - m23*m32) - m20*(m12*m33 - m13*m32) + m30*(m12*m23 - m13*m22));
	out[5] =  (m00*(m22*m33 - m23*m32) - m20*(m02*m33 - m03*m32) + m30*(m02*m23 - m03*m22));
	out[6] = -(m00*(m12*m33 - m13*m32) - m10*(m02*m33 - m03*m32) + m30*(m02*m13 - m03*m12));
	out[7] =  (m00*(m12*m23 - m13*m22) - m10*(m02*m23 - m03*m22) + m20*(m02*m13 - m03*m12));
	out[8] =  (m10*(m21*m33 - m23*m31) - m20*(m11*m33 - m13*m31) + m30*(m11*m23 - m13*m21));
	out[9] = -(m00*(m21*m33 - m23*m31) - m20*(m01*m33 - m03*m31) + m30*(m01*m23 - m03*m21));
	out[10] =  (m00*(m11*m33 - m13*m31) - m10*(m01*m33 - m03*m31) + m30*(m01*m13 - m03*m11));
	out[11] = -(m00*(m11*m23 - m13*m21) - m10*(m01*m23 - m03*m21) + m20*(m01*m13 - m03*m11));
	out[12] = -(m10*(m21*m32 - m22*m31) - m20*(m11*m32 - m12*m31) + m30*(m11*m22 - m12*m21));
	out[13] =  (m00*(m21*m32 - m22*m31) - m20*(m01*m32 - m02*m31) + m30*(m01*m22 - m02*m21));
	out[14] = -(m00*(m11*m32 - m12*m31) - m10*(m01*m32 - m02*m31) + m30*(m01*m12 - m02*m11));
	out[15] =  (m00*(m11*m22 - m12*m21) - m10*(m01*m22 - m02*m21) + m20*(m01*m12 - m02*m11));

	// calculate the determinant (as inverse == 1/det * adjoint, adjoint * m == identity * det,
	// so this calculates the det)
	det = m00*out[0] + m10*out[1] + m20*out[2] + m30*out[3];
	if (det == 0.0f)
		return false;

	// multiplications are faster than divisions, usually
	det = 1.0f / det;

	// manually unrolled loop to multiply all matrix elements by 1/det
	out[0] *= det; out[1] *= det; out[2] *= det; out[3] *= det;
	out[4] *= det; out[5] *= det; out[6] *= det; out[7] *= det;
	out[8] *= det; out[9] *= det; out[10] *= det; out[11] *= det;
	out[12] *= det; out[13] *= det; out[14] *= det; out[15] *= det;

	return true;
}

void Matrix4_FromQuaternion( const quat_t q, mat4_t out )
{
	mat3_t m;

	Quat_ToMatrix3( q, m );

	out[0 ] = m[0], out[1 ] = m[2], out[2 ] = m[6], out[3 ] = 0;
	out[4 ] = m[1], out[5 ] = m[4], out[6 ] = m[7], out[7 ] = 0;
	out[8 ] = m[2], out[9 ] = m[5], out[10] = m[8], out[11] = 0;
	out[12] = 0,    out[13] = 0,    out[14] = 0,    out[15] = 1;

}

void Matrix4_FromDualQuaternion( const dualquat_t dq, mat4_t out )
{
	vec3_t v;
	mat3_t m;

	DualQuat_ToMatrix3AndVector( dq, m, v );

	out[0 ] = m[0], out[1 ] = m[3], out[2 ] = m[6], out[3 ] = 0;
	out[4 ] = m[1], out[5 ] = m[4], out[6 ] = m[7], out[7 ] = 0;
	out[8 ] = m[2], out[9 ] = m[5], out[10] = m[8], out[11] = 0;
	out[12] = v[0], out[13] = v[1], out[14] = v[2], out[15] = 1;
}

void Matrix4_Rotate( mat4_t m, vec_t angle, vec_t x, vec_t y, vec_t z )
{
	mat4_t t, b;
	vec_t c = cos( DEG2RAD( angle ) );
	vec_t s = sin( DEG2RAD( angle ) );
	vec_t mc = 1 - c, t1, t2;

	t[0]  = ( x * x * mc ) + c;
	t[5]  = ( y * y * mc ) + c;
	t[10] = ( z * z * mc ) + c;

	t1 = y * x * mc;
	t2 = z * s;
	t[1] = t1 + t2;
	t[4] = t1 - t2;

	t1 = x * z * mc;
	t2 = y * s;
	t[2] = t1 - t2;
	t[8] = t1 + t2;

	t1 = y * z * mc;
	t2 = x * s;
	t[6] = t1 + t2;
	t[9] = t1 - t2;

	t[3] = t[7] = t[11] = t[12] = t[13] = t[14] = 0;
	t[15] = 1;

	Matrix4_Copy( m, b );
	Matrix4_MultiplyFast( b, t, m );
}

void Matrix4_Translate( mat4_t m, vec_t x, vec_t y, vec_t z )
{
	m[12] = m[0] * x + m[4] * y + m[8]  * z + m[12];
	m[13] = m[1] * x + m[5] * y + m[9]  * z + m[13];
	m[14] = m[2] * x + m[6] * y + m[10] * z + m[14];
	m[15] = m[3] * x + m[7] * y + m[11] * z + m[15];
}

void Matrix4_Scale( mat4_t m, vec_t x, vec_t y, vec_t z )
{
	m[0] *= x; m[4] *= y; m[8]  *= z;
	m[1] *= x; m[5] *= y; m[9]  *= z;
	m[2] *= x; m[6] *= y; m[10] *= z;
	m[3] *= x; m[7] *= y; m[11] *= z;
}

void Matrix4_Transpose( const mat4_t m, mat4_t out )
{
	out[0] = m[0]; out[1] = m[4]; out[2] = m[8]; out[3] = m[12];
	out[4] = m[1]; out[5] = m[5]; out[6] = m[9]; out[7] = m[13];
	out[8] = m[2]; out[9] = m[6]; out[10] = m[10]; out[11] = m[14];
	out[12] = m[3]; out[13] = m[7]; out[14] = m[11]; out[15] = m[15];
}

void Matrix4_Matrix( const mat4_t in, vec3_t out[3] )
{
	out[0][0] = in[0];
	out[0][1] = in[4];
	out[0][2] = in[8];

	out[1][0] = in[1];
	out[1][1] = in[5];
	out[1][2] = in[9];

	out[2][0] = in[2];
	out[2][1] = in[6];
	out[2][2] = in[10];
}

void Matrix4_Multiply_Vector( const mat4_t m, const vec4_t v, vec4_t out )
{
	out[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12] * v[3];
	out[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13] * v[3];
	out[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3];
	out[3] = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3];
}

//============================================================================

void Matrix4_Copy2D( const mat4_t m1, mat4_t m2 )
{
	m2[0] = m1[0];
	m2[1] = m1[1];
	m2[4] = m1[4];
	m2[5] = m1[5];
	m2[12] = m1[12];
	m2[13] = m1[13];
}

void Matrix4_Multiply2D( const mat4_t m1, const mat4_t m2, mat4_t out )
{
	out[0]  = m1[0] * m2[0] + m1[4] * m2[1];
	out[1]  = m1[1] * m2[0] + m1[5] * m2[1];
	out[4]  = m1[0] * m2[4] + m1[4] * m2[5];
	out[5]  = m1[1] * m2[4] + m1[5] * m2[5];
	out[12] = m1[0] * m2[12] + m1[4] * m2[13] + m1[12];
	out[13] = m1[1] * m2[12] + m1[5] * m2[13] + m1[13];
}

void Matrix4_Scale2D( mat4_t m, vec_t x, vec_t y )
{
	m[0] *= x;
	m[1] *= x;
	m[4] *= y;
	m[5] *= y;
}

void Matrix4_Translate2D( mat4_t m, vec_t x, vec_t y )
{
	m[12] += x;
	m[13] += y;
}

void Matrix4_Stretch2D( mat4_t m, vec_t s, vec_t t )
{
	m[0] *= s;
	m[1] *= s;
	m[4] *= s;
	m[5] *= s;
	m[12] = s * m[12] + t;
	m[13] = s * m[13] + t;
}

//============================================================================

/*
* Matrix4_OrthogonalProjection
*/
void Matrix4_OrthogonalProjection( vec_t left, vec_t right, vec_t bottom, vec_t top,
	vec_t near, vec_t far, mat4_t m )
{
	m[0] = 2.0f / ( right - left );
	m[1] = 0.0f;
	m[2] = 0.0f;
	m[3] = 0.0f;

	m[4] = 0.0f;
	m[5] = 2.0f / ( top - bottom );
	m[6] = 0.0f;
	m[7] = 0.0f;

	m[8] = 0.0f;
	m[9] = 0.0f;
	m[10] = -2.0f / ( far - near );
	m[11] = 0.0f;

	m[12] = - (right + left) / (right - left);
	m[13] = - (top + bottom) / (top - bottom);
	m[14] = - (far + near) / (far - near);
	m[15] = 1.0f;
}

/*
* Matrix4_InfiniteOrthogonalProjection
*/
void Matrix4_InfiniteOrthogonalProjection( vec_t left, vec_t right, vec_t bottom, vec_t top, 
	mat4_t m )
{
	m[0] = 2.0f / ( right - left );
	m[1] = 0.0f;
	m[2] = 0.0f;
	m[3] = 0.0f;

	m[4] = 0.0f;
	m[5] = 2.0f / ( top - bottom );
	m[6] = 0.0f;
	m[7] = 0.0f;

	m[8] = 0.0f;
	m[9] = 0.0f;
	m[10] = 0.0f;
	m[11] = 0.0f;

	m[12] = - (right + left) / (right - left);
	m[13] = - (top + bottom) / (top - bottom);
	m[14] = -1.0f;
	m[15] = 1.0f;
}

/*
* Matrix4_PerspectiveProjection
*/
void Matrix4_PerspectiveProjection( vec_t fov_x, vec_t fov_y, 
	vec_t near, vec_t far, vec_t stereoSeparation, mat4_t m )
{
	m[0] = 1.0f / tan( fov_x * M_PI / 360.0 );
	m[1] = 0.0f;
	m[2] = 0.0f;
	m[3] = 0.0f;
	m[4] = 0.0f;
	m[5] = 1.0f / tan( fov_y * M_PI / 360.0 );
	m[6] = 0.0f;
	m[7] = 0.0f;
	m[8] = 0.0f;
	m[9] = 0.0f;
	m[10] = -( far + near ) / ( far - near );
	m[11] = -1.0f;
	m[12] = 0.0f;
	m[13] = 0.0f;
	m[14] = -( 2.0 * far * near ) / ( far - near );
	m[15] = 0.0f;
}

/*
* Matrix4_InfinitePerspectiveProjection
*/
void Matrix4_InfinitePerspectiveProjection( vec_t fov_x, vec_t fov_y, 
	vec_t near, vec_t stereoSeparation, mat4_t m, vec_t epsilon )
{
	m[0] = 1.0f / tan( fov_x * M_PI / 360.0 );
	m[1] = 0.0f;
	m[2] = 0.0f;
	m[3] = 0.0f;
	m[4] = 0.0f;
	m[5] = 1.0f / tan( fov_y * M_PI / 360.0 );
	m[6] = 0.0f;
	m[7] = 0.0f;
	m[8] = 0.0f;
	m[9] = 0.0f;
	m[10] = epsilon - 1.0f;
	m[11] = -1.0f;
	m[12] = 0.0f;
	m[13] = 0.0f;
	m[14] = (epsilon - 2.0f) * near;
	m[15] = 0.0f;
}

/*
* Matrix4_PerspectiveProjectionToInfinity
*/
void Matrix4_PerspectiveProjectionToInfinity( vec_t near, mat4_t m, vec_t epsilon )
{
	m[10] = epsilon - 1.0f;
	m[14] = (epsilon - 2.0f) * near;
}

/*
* Matrix4_Modelview
*/
void Matrix4_Modelview( const vec3_t viewOrg, const mat3_t viewAxis, mat4_t m )
{
	mat3_t axis;
	mat4_t flip, view;

#if 0
	Matrix4_Identity( flip );
	Matrix4_Rotate( flip, -90, 1, 0, 0 );
	Matrix4_Rotate( flip, 90, 0, 0, 1 );
#else
	Vector4Set( &flip[0], 0, 0, -1, 0 );
	Vector4Set( &flip[4], -1, 0, 0, 0 );
	Vector4Set( &flip[8], 0, 1, 0, 0 );
	Vector4Set( &flip[12], 0, 0, 0, 1 );
#endif

	Matrix3_Copy( viewAxis, axis );

	view[0 ] = axis[0];
	view[4 ] = axis[1];
	view[8 ] = axis[2];
	view[12] = -viewOrg[0] * view[0] + -viewOrg[1] * view[4] + -viewOrg[2] * view[8];

	view[1 ] = axis[3];
	view[5 ] = axis[4];
	view[9 ] = axis[5];
	view[13] = -viewOrg[0] * view[1] + -viewOrg[1] * view[5] + -viewOrg[2] * view[9];

	view[2 ] = axis[6];
	view[6 ] = axis[7];
	view[10] = axis[8];
	view[14] = -viewOrg[0] * view[2] + -viewOrg[1] * view[6] + -viewOrg[2] * view[10];

	view[3] = 0;
	view[7] = 0;
	view[11] = 0;
	view[15] = 1;

	Matrix4_Multiply( flip, view, m );
}

/*
* Matrix4_ObliqueNearClipping
*
* Lengyel, Eric. "Oblique View Frustum Depth Projection and Clipping"
* Journal of Game Development, Vol. 1, No. 2 (2005), Charles River Media, pp. 5–16.
*
* Version that works both for perspective and orthographic projections.
*/
void Matrix4_ObliqueNearClipping( const vec3_t normal, const vec_t dist, const mat4_t cm, mat4_t pm )
{
	mat4_t tm1, tm2;
    vec4_t c, q, p;
	vec_t d;

	// The clipping plane in object coordinates
	q[0] = normal[0];
	q[1] = normal[1];
	q[2] = normal[2];
	q[3] = dist;

	// The plane is transformed by the transpose of the inverse of the
	// camera matrix and stored in the resulting eye coordinates
	Matrix4_Invert( cm, tm1 );
	Matrix4_Transpose( tm1, tm2 );
	Matrix4_Multiply_Vector( tm2, q, c );

	if( c[3] >= 0.0f ) {
		// the techinique only works when the w-coordinate is negative
		// (corresponding to the camera being on the negative side of the plane)
		return;
	}

	// Calculate the clip-space corner point opposite the clipping plane
	// as (sgn(clipPlane.x), sgn(clipPlane.y), 1, 1) and
	// transform it into camera space by multiplying it
	// by the inverse of the projection matrix

	p[0] = Q_sign(c[0]);
	p[1] = Q_sign(c[1]);
	p[2] = 1.0f;
	p[3] = 1.0f;

    // Calculate the scaled plane vector
	Matrix4_Invert( pm, tm1 );
	Matrix4_Multiply_Vector( tm1, p, q );
	d = 2.0f / (c[0] * q[0] + c[1] * q[1] + c[2] * q[2] + c[3] * q[3]);

    // Replace the third row of the projection matrix
    pm[ 2] = c[0] * d - pm[ 3];
    pm[ 6] = c[1] * d - pm[ 7];
    pm[10] = c[2] * d - pm[11];
    pm[14] = c[3] * d - pm[15];
}
