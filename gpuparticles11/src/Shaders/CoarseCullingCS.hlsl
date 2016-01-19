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
#include "ShaderConstants.h"
#include "Globals.h"

// Shader inputs
// =============

// View space positions of the particles
StructuredBuffer<float4>			g_ViewSpacePositions				: register( t0 );

// The maximum radius in X & Y of each particle
StructuredBuffer<float>				g_MaxRadiusBuffer					: register( t1 );

// The alive particle list. X is the unused distance to the camera, Y stores the global particle index
StructuredBuffer<float2>			g_AliveIndexBuffer					: register( t2 );


// Shader outputs
// ==============

// The index buffer for each bin. This is allocated as NumBins * MaxParticles
RWBuffer<uint>						g_CoarseTiledIndexBuffer			: register( u0 );

// The per-bin counters of how many particles are in each bin
RWBuffer<uint>						g_CoarseTiledIndexBufferCounters	: register( u1 );



// LDS to store the frustum data per bin
groupshared float3 g_FrustumData[ NUM_COARSE_CULLING_TILES_X ][ NUM_COARSE_CULLING_TILES_Y ][ 4 ];


// Initialize the LDS
// Calculate the per-bin frusta using one thread to calc one bin's set of frustum planes.
// This could be sped up by using one thread per frustum plane, so using 4 threads per bin.
void initLDS( uint localIdx )
{
	// Only need to the first n threads to calculate the frusta for the n bins
	if ( localIdx < NUM_COARSE_TILES )
	{
		// Calculate the coarse tile dimensions to be a multiple of the fine-grained tile size
		uint coarseTileWidth = g_NumCullingTilesPerCoarseTileX * TILE_RES_X;
		uint coarseTileHeight = g_NumCullingTilesPerCoarseTileY * TILE_RES_Y;

		// Get the coarse tile index
		uint tileX = localIdx % NUM_COARSE_CULLING_TILES_X;
		uint tileY = localIdx / NUM_COARSE_CULLING_TILES_X;
			
		int pxm = tileX*coarseTileWidth;
		int pym = tileY*coarseTileHeight;
		int pxp = (tileX+1)*coarseTileWidth;
		int pyp = (tileY+1)*coarseTileHeight;

		// Generate the four side planes
		float3 frustum[4];
		frustum[0] = ConvertProjToView( float4( pxm/(float)g_ScreenWidth*2.f-1.f, (g_ScreenHeight-pym)/(float)g_ScreenHeight*2.f-1.f,1.f,1.f) );
		frustum[1] = ConvertProjToView( float4( pxp/(float)g_ScreenWidth*2.f-1.f, (g_ScreenHeight-pym)/(float)g_ScreenHeight*2.f-1.f,1.f,1.f) );
		frustum[2] = ConvertProjToView( float4( pxp/(float)g_ScreenWidth*2.f-1.f, (g_ScreenHeight-pyp)/(float)g_ScreenHeight*2.f-1.f,1.f,1.f) );
		frustum[3] = ConvertProjToView( float4( pxm/(float)g_ScreenWidth*2.f-1.f, (g_ScreenHeight-pyp)/(float)g_ScreenHeight*2.f-1.f,1.f,1.f) );
					
		for(int i=0; i<4; i++)
		{
			g_FrustumData[ tileX ][ tileY ][i] = CreatePlaneEquation( frustum[i], frustum[(i+1)&3] );
		}
	}
}


// Add a global particle index to a bin
void addToBuffer( uint bufferIndex, uint particleGlobalIndex )
{
	// Atomically increment the bin's counter
	uint dstIdx = 0;
	InterlockedAdd( g_CoarseTiledIndexBufferCounters[ bufferIndex ], 1, dstIdx );

	// The stride into this particular bin is not MaxParticles, but the current frame's number of alive particles. This *might* improve locallity of data
	dstIdx += bufferIndex * g_NumActiveParticles;

	// Write the global index to the bin
	g_CoarseTiledIndexBuffer[ dstIdx ] = particleGlobalIndex;
}


// Just work through the list of alive particles linearly. Each thread analyzes one particle to see which bins it should go into. ie n threads for n particles.
// An alternative strategy could be to use one thread to see if a particle lives in ONE bin. Therefore dispatch n * numBins threads for n particles.
[numthreads(COARSE_CULLING_THREADS, 1, 1)]
void CoarseCulling( uint3 localIdx : SV_GroupThreadID, uint3 groupIdx : SV_GroupID, uint3 globalIdx : SV_DispatchThreadID )
{
	// Pre-compute the per-bin frusta
	initLDS( localIdx.x );

	// Initialize the per-bin counters to zero
	if ( globalIdx.x < NUM_COARSE_TILES )
	{
		g_CoarseTiledIndexBufferCounters[ globalIdx.x ] = 0;
	}

	// Sync to this point so we know the counters are all set to zero
	GroupMemoryBarrierWithGroupSync();

	// Since the global thread id maps to the particle index into the alive list, make sure we don't read past the end of the list
	if ( globalIdx.x < g_NumActiveParticles )
	{
		// Get the global particle index
		uint index = (uint)g_AliveIndexBuffer[ globalIdx.x ].y;
			
		// Get the view space position of the particle
		float4 vsPosition = g_ViewSpacePositions[ index ];
		float3 center = vsPosition.xyz;

		// Get the maximum radius of the particle
		float r = g_MaxRadiusBuffer[ index ];
		
		// Near plane test
		if ( -center.z < r )
		{
			// For each bin in X and Y. Note that the particle could end up in multiple bins
			for ( int tileX = 0; tileX < NUM_COARSE_CULLING_TILES_X; tileX++ )
			{
				for ( int tileY = 0; tileY < NUM_COARSE_CULLING_TILES_Y; tileY++ )
				{
					// Do frustum plane tests
					if ( ( GetSignedDistanceFromPlane( center, g_FrustumData[ tileX ][ tileY ][0] ) < r ) &&
						 ( GetSignedDistanceFromPlane( center, g_FrustumData[ tileX ][ tileY ][1] ) < r ) &&
						 ( GetSignedDistanceFromPlane( center, g_FrustumData[ tileX ][ tileY ][2] ) < r ) &&
						 ( GetSignedDistanceFromPlane( center, g_FrustumData[ tileX ][ tileY ][3] ) < r ) )
					{
						// Add to the bin
						addToBuffer( tileY * NUM_COARSE_CULLING_TILES_X + tileX, index );
					}
				}
			}
		}
	}
}