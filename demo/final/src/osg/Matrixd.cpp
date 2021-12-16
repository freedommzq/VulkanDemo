/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
*/

#include "Matrixd.h"
#include "Matrixf.h"

// specialise Matrix_implementaiton to be Matrixd
#define  Matrix_implementation Matrixd

/*
osg::Matrixd::Matrixd( const osg::Matrixf& mat )
{
    set(mat.ptr());
}

osg::Matrixd& osg::Matrixd::operator = (const osg::Matrixf& rhs)
{
    set(rhs.ptr());
    return *this;
}

void osg::Matrixd::set(const osg::Matrixf& rhs)
{
    set(rhs.ptr());
}
*/

// now compile up Matrix via Matrix_implementation
//#include "Matrix_implementation.cpp"

using namespace osg;

#define SET_ROW(row, v1, v2, v3, v4 )    \
    _mat[(row)][0] = (v1); \
    _mat[(row)][1] = (v2); \
    _mat[(row)][2] = (v3); \
    _mat[(row)][3] = (v4);

#define INNER_PRODUCT(a,b,r,c) \
     ((a)._mat[r][0] * (b)._mat[0][c]) \
    +((a)._mat[r][1] * (b)._mat[1][c]) \
    +((a)._mat[r][2] * (b)._mat[2][c]) \
    +((a)._mat[r][3] * (b)._mat[3][c])


Matrix_implementation::Matrix_implementation(value_type a00, value_type a01, value_type a02, value_type a03,
	value_type a10, value_type a11, value_type a12, value_type a13,
	value_type a20, value_type a21, value_type a22, value_type a23,
	value_type a30, value_type a31, value_type a32, value_type a33)
{
	SET_ROW(0, a00, a01, a02, a03)
		SET_ROW(1, a10, a11, a12, a13)
		SET_ROW(2, a20, a21, a22, a23)
		SET_ROW(3, a30, a31, a32, a33)
}

#undef SET_ROW