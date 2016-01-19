//
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#include "Globals.h"

struct VS_OUTPUT
{
	float2	TexCoord		: TEXCOORD0;
	float4 	Position		: SV_POSITION;
};

Texture2D				g_Texture			: register( t0 );


VS_OUTPUT FSQuadVS( uint VertexId : SV_VertexID )
{
	VS_OUTPUT Output;

	float2 corner = float2( (VertexId << 1) & 2, VertexId & 2 );
	Output.Position = float4( corner * float2( 2.0, -2.0 ) + float2( -1.0, 1.0 ), 0.0, 1.0 );
	Output.TexCoord = corner;

	return Output;
}


float4 FSQuadPS( float2 TexCoord : TEXCOORD0 ) : SV_Target
{
	return g_Texture.Sample( g_samWrapLinear, TexCoord );
}