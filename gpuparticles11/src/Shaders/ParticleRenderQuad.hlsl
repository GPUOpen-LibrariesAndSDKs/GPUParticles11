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
	float4 	Position : SV_POSITION;
};

// The UAV for tiled rendering
Buffer<float4>				g_RenderBuffer		: register( t0 );


VS_OUTPUT QuadVS( uint VertexId : SV_VertexID )
{
    VS_OUTPUT Output;

	float2 corner = float2( (VertexId << 1) & 2, VertexId & 2 );
    Output.Position = float4( corner * float2( 2.0, -2.0 ) + float2( -1.0, 1.0 ), 0.0, 1.0 );
  
    return Output;    
}


float4 QuadPS( float4 Position : SV_POSITION ) : SV_Target
{
	float4 colour = 1;

	// Get XY coordinates for pixel
	float x = Position.x - (g_ScreenWidth / 2);
	float y = Position.y;

	// Get the pixel index into the UAV
	uint pixelIndex = x + (y * g_ScreenWidth);

	// Load the pixel value from the UAV
	float4 particleValue = g_RenderBuffer.Load( pixelIndex );
	
	colour = particleValue;

	return colour;
}