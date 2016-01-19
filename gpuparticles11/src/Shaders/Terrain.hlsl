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


struct VS_INPUT
{
	float3 Position			: POSITION;
	float3 Normal			: NORMAL;
	float2 TexCoord			: TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 	Position		: SV_POSITION;
	float3 Normal			: NORMAL;
};


VS_OUTPUT TerrainVS( VS_INPUT input )
{
    VS_OUTPUT Output;
    
    Output.Position = mul( float4( input.Position, 1 ), g_mViewProjection );
	Output.Normal = input.Normal;
    
    return Output;
}


float4 TerrainPS( VS_OUTPUT In ) : SV_TARGET
{ 
	float ndotl = saturate( dot( In.Normal, g_SunDirection ) );
	float4 albedo = float4( 0.5, 0.5, 0.6, 1 );

	float4 color = 1;

	color.rgb = albedo.rgb * ( ndotl * g_SunColor.rgb + g_AmbientColor.rgb );
	return color;
}