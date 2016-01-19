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
StructuredBuffer<float4>			g_ViewSpacePositions			: register( t0 );

// The maximum radius in X & Y of each particle
StructuredBuffer<float>				g_MaxRadiusBuffer				: register( t1 );

// The alive particle list. X is the distance to the camera, Y stores the global particle index. Only used for the non-coarse culling path
StructuredBuffer<float2>			g_AliveIndexBuffer				: register( t2 );

// The depth of the opaque scene
Texture2D<float>					g_DepthTexture					: register( t3 );

// The coarse culling buffer
Buffer<uint>						g_CoarseBuffer					: register( t4 );
Buffer<uint>						g_CoarseBufferCounters			: register( t5 );


// Shader outputs
// =============

// The tiled buffer containing the indices of the particles in that tile. The particles are written out in sorted front to back order.
// The stride between tiles is PARTICLES_TILE_BUFFER_SIZE elements
RWBuffer<uint>						g_TiledIndexBuffer				: register( u0 );



// Helper functions for calculating the far Z plane value of this tile from the depth buffer texture
#if defined (CULLMAXZ)
groupshared uint				g_ldsZMax;

float ConvertProjDepthToView( float z )
{
	z = 1.f / (z*g_mProjectionInv._34 + g_mProjectionInv._44);
	return z;
}


void CalculateMinMaxDepthInLds( uint3 globalIdx )
{
	float opaqueDepth = g_DepthTexture.Load( uint3(globalIdx.x,globalIdx.y,0) ).x;
	float opaqueViewPosZ = ConvertProjDepthToView( opaqueDepth );
	uint opaqueZ = asuint( opaqueViewPosZ );

	if( opaqueDepth != 0.f )
	{
		InterlockedMax( g_ldsZMax, opaqueZ );
	}	
}
#endif


// The maximum number of particles we want to hold in LDS during the culling phase.
// Ideally it should be all the particles visible in the tile, however in reality we must cap this limit.
// The limit could be much higher than the number we write out to memory since they are sorted front to back so we might
// get away with leaving out the backmost particles
#define	MAX_PARTICLES_PER_TILE_FOR_SORTING			2*NUM_PARTICLES_PER_TILE

// The LDS members for storing the particles that we want to sort and write back out to a UAV
groupshared uint				g_ldsParticleIdx[ MAX_PARTICLES_PER_TILE_FOR_SORTING ];
groupshared float				g_ldsParticleDistances[ MAX_PARTICLES_PER_TILE_FOR_SORTING ];
groupshared uint				g_ldsNumParticles;


#if defined (USE_VIEW_FRUSTUM_PLANES)
// Function to generate the frustum planes
void CalcFrustumPlanes( in uint2 groupIdx, out float3 frustumEqn[ 4 ] )
{
	// construct frustum for this tile
	int pxm = TILE_RES_X*groupIdx.x;
	int pym = TILE_RES_Y*groupIdx.y;
	int pxp = TILE_RES_X*(groupIdx.x+1);
	int pyp = TILE_RES_Y*(groupIdx.y+1);

	// four corners of the tile, clockwise from top-left
	float3 frustum[4];
	frustum[0] = ConvertProjToView( float4( pxm/(float)g_ScreenWidth*2.f-1.f, (g_ScreenHeight-pym)/(float)g_ScreenHeight*2.f-1.f,1.f,1.f) );
	frustum[1] = ConvertProjToView( float4( pxp/(float)g_ScreenWidth*2.f-1.f, (g_ScreenHeight-pym)/(float)g_ScreenHeight*2.f-1.f,1.f,1.f) );
	frustum[2] = ConvertProjToView( float4( pxp/(float)g_ScreenWidth*2.f-1.f, (g_ScreenHeight-pyp)/(float)g_ScreenHeight*2.f-1.f,1.f,1.f) );
	frustum[3] = ConvertProjToView( float4( pxm/(float)g_ScreenWidth*2.f-1.f, (g_ScreenHeight-pyp)/(float)g_ScreenHeight*2.f-1.f,1.f,1.f) );

	// create plane equations for the four sides of the frustum, 
	// with the positive half-space outside the frustum (and remember, 
	// view space is left handed, so use the left-hand rule to determine 
	// cross product direction)
	for ( uint i = 0; i < 4; i++ )
	{
		frustumEqn[i] = CreatePlaneEquation( frustum[i], frustum[(i+1)&3] );
	}
}
#endif


// Bitonic sort function that runs on our LDS buffers
void BitonicSort( in uint localIdxFlattened )
{
	uint numParticles = g_ldsNumParticles;
	
	// Round the number of particles up to the nearest power of two
	uint numParticlesPowerOfTwo = 1;
	while ( numParticlesPowerOfTwo < numParticles ) 
		numParticlesPowerOfTwo <<= 1;

	// The wait is required for the flow control
	GroupMemoryBarrierWithGroupSync();

	for( uint nMergeSize=2; nMergeSize <= numParticlesPowerOfTwo; nMergeSize=nMergeSize*2 )
	{
		for( uint nMergeSubSize=nMergeSize>>1; nMergeSubSize>0; nMergeSubSize=nMergeSubSize>>1 ) 
		{		
			uint tmp_index = localIdxFlattened;
			uint index_low = tmp_index & (nMergeSubSize-1);
			uint index_high = 2*(tmp_index-index_low);
			uint index = index_high + index_low;

			uint nSwapElem = nMergeSubSize==nMergeSize>>1 ? index_high + (2*nMergeSubSize-1) - index_low : index_high + nMergeSubSize + index_low;
			if ( nSwapElem < numParticles && index < numParticles )
			{
				if ( g_ldsParticleDistances[ index ] > g_ldsParticleDistances[ nSwapElem ] )
				{ 
					uint uTemp = g_ldsParticleIdx[ index ];
					float vTemp = g_ldsParticleDistances[ index ];

					g_ldsParticleIdx[ index ] = g_ldsParticleIdx[ nSwapElem ];
					g_ldsParticleDistances[ index ] = g_ldsParticleDistances[ nSwapElem ];

					g_ldsParticleIdx[ nSwapElem ] = uTemp;
					g_ldsParticleDistances[ nSwapElem ] = vTemp;
				}
			}
			GroupMemoryBarrierWithGroupSync();
		}
	}
}


#if defined (COARSE_CULLING_ENABLED)

// Get the number of particles in this coarse bin
uint GetNumParticlesInCoarseTile( uint tile )
{
	return g_CoarseBufferCounters[ tile ];
}

// Get the global particle index from this bin
uint getParticleIndexFromCoarseBuffer( uint binIndex, uint listIndex )
{
	uint offset = binIndex * g_NumActiveParticles;
	return g_CoarseBuffer[ offset + listIndex ];
}
#endif


void AddParticleToVisibleList( uint index, float distance )
{
	// do a thread-safe increment of the list counter 
	// and put the index of this particle into the list
	uint dstIdx = 0;
	InterlockedAdd( g_ldsNumParticles, 1, dstIdx );
					
	// Can we write to LDS?
	if ( dstIdx < MAX_PARTICLES_PER_TILE_FOR_SORTING )
	{
		g_ldsParticleIdx[ dstIdx ] = index;
		g_ldsParticleDistances[ dstIdx ] = distance;
	}
}


[numthreads(TILE_RES_X, TILE_RES_Y, 1)]
void Culling( uint3 localIdx : SV_GroupThreadID, uint3 groupIdx : SV_GroupID, uint3 globalIdx : SV_DispatchThreadID )
{
	uint localIdxFlattened = localIdx.x + (localIdx.y*TILE_RES_X);

	uint i;

	// Initialize our LDS values
	if( localIdxFlattened == 0 )
	{
		g_ldsNumParticles = 0;
#if defined (CULLMAXZ)
		g_ldsZMax = 0;
#endif
	} 
	
	GroupMemoryBarrierWithGroupSync();
	
	// Calculate the maxZ value of the tile if required
#if defined (CULLMAXZ)
	CalculateMinMaxDepthInLds( globalIdx );

	GroupMemoryBarrierWithGroupSync();
	float maxZ = asfloat( g_ldsZMax );
#endif
	
#if defined (USE_VIEW_FRUSTUM_PLANES)
	// Generate the side frustum planes
	float3 frustumEqn[4];
	CalcFrustumPlanes( groupIdx.xy, frustumEqn );
#else
	// Generate the tile extents in screen space
	int2 tileSize = int2( TILE_RES_X, TILE_RES_Y );

	int2 tileP0 = groupIdx.xy * tileSize;
	int2 tileP1 = (groupIdx.xy + int2( 1, 1 )) * tileSize;
#endif

	// For coarse culling, retreive the bin index and get the number of particles in that bin
#if defined (COARSE_CULLING_ENABLED)
	uint tileX = groupIdx.x / g_NumCullingTilesPerCoarseTileX;
	uint tileY = groupIdx.y / g_NumCullingTilesPerCoarseTileY;

	uint coarseTileIdx = tileX + tileY * g_NumCoarseCullingTilesX;
	
	// Get the number of particles in the bin this tile lives in
    uint uNumParticles = GetNumParticlesInCoarseTile( coarseTileIdx );
#else

	// No coarse culling, so just loop through ALL the alive particles
	uint uNumParticles = g_NumActiveParticles;
#endif

	// Each thread needs to look at a particle and determine whether it is visible in this tile
	// In the thread group TILE_RES_X * TILE_RES_Y particles are processed in parallel
	for ( i = localIdxFlattened; i < uNumParticles; i += TILE_RES_X*TILE_RES_Y )
	{
		// Fetch the global particle index
#if defined (COARSE_CULLING_ENABLED)
		uint index = getParticleIndexFromCoarseBuffer( coarseTileIdx, i );
#else
		uint index = (uint)g_AliveIndexBuffer[ i ].y;
#endif
		
		// Fetch the maximum radius of the particle
		float r = g_MaxRadiusBuffer[ index ];

		// Fetch the view space position of the particle
		float4 vsPosition = g_ViewSpacePositions[ index ];
		float3 center = vsPosition.xyz;
		
		// Optionally cull using the tile's far plane
#if defined (CULLMAXZ)
		if ( center.z - maxZ < r )
#endif
		{
			// Cull against near plane if we aren't doing coarse culling. The coarse culling stage has done this already
#if !defined (COARSE_CULLING_ENABLED)
			if ( -center.z < r )
#endif
			{
#if defined (USE_VIEW_FRUSTUM_PLANES)
				// Cull against the side frustum planes
				if( ( GetSignedDistanceFromPlane( center, frustumEqn[0] ) < r ) &&
					( GetSignedDistanceFromPlane( center, frustumEqn[1] ) < r ) &&
					( GetSignedDistanceFromPlane( center, frustumEqn[2] ) < r ) &&
					( GetSignedDistanceFromPlane( center, frustumEqn[3] ) < r ) )
				{
					AddParticleToVisibleList( index, vsPosition.z );
				}
#else
				// Cull the particle in screen space by projecting the top left and bottom right points on the view space AABB of the particle
				float2 screenDimensions = float2( g_ScreenWidth, g_ScreenHeight );

				float4 screenSpacePosition0 = mul( float4( center + float3( -r, r, 0.0 ), 1.0 ), g_mProjection );
				screenSpacePosition0.xy /= screenSpacePosition0.w;

				float4 screenSpacePosition1 = mul( float4( center + float3( r, -r, 0.0 ), 1.0 ), g_mProjection );
				screenSpacePosition1.xy /= screenSpacePosition1.w;

				screenSpacePosition0 = screenSpacePosition0 * 0.5 + 0.5;
				screenSpacePosition0.y = 1 - screenSpacePosition0.y;

				screenSpacePosition1 = screenSpacePosition1 * 0.5 + 0.5;
				screenSpacePosition1.y = 1 - screenSpacePosition1.y;
				
				int2 pos0 = (int2)( screenSpacePosition0 * screenDimensions );
				int2 pos1 = (int2)( screenSpacePosition1 * screenDimensions );
				
				if ( pos1.x > tileP0.x && pos0.x < tileP1.x && pos1.y > tileP0.y && pos0.y < tileP1.y )
				{
					AddParticleToVisibleList( index, vsPosition.z );
				}
#endif
			}
		}
	}
	
	// Wait for all particles to be added to the lists
	GroupMemoryBarrierWithGroupSync();
	
	// Perform the Bitonic sort
	BitonicSort( localIdxFlattened );
	
	// Clamp the number of particles fit into the UAV storage. As particles are now sorted front to back, this will cull the most occluded particles.
	if( localIdxFlattened == 0 )
	{
		g_ldsNumParticles = min( g_ldsNumParticles, NUM_PARTICLES_PER_TILE );
		
	}
	GroupMemoryBarrierWithGroupSync();
	
	// Write the sorted particles from LDS to main memory
	uint tileIdxFlattened = groupIdx.x + groupIdx.y * g_NumTilesX;
	uint tiledBufferStartOffset = PARTICLES_TILE_BUFFER_SIZE * tileIdxFlattened;

	uint numLDSParticleToCache = g_ldsNumParticles;
	
	for ( i = localIdxFlattened; i < numLDSParticleToCache; i += TILE_RES_X*TILE_RES_Y )
	{
		g_TiledIndexBuffer[ tiledBufferStartOffset + 1 + i ] = g_ldsParticleIdx[ i ];
	}

	if( localIdxFlattened == 0 )
	{
		g_TiledIndexBuffer[ tiledBufferStartOffset ] = numLDSParticleToCache;
	}
}