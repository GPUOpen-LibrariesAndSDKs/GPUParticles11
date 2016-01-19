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

//
// This shader takes the per-tile particle lists and renders the particles in tiles to a screenspace UAV
//

#include "ShaderConstants.h"
#include "Globals.h"

// The particle buffer (at least part of it)
StructuredBuffer<GPUParticlePartA>	g_ParticleBuffer				: register( t0 );

// The pre-computed viewspace positions of the particles
StructuredBuffer<float4>			g_ViewSpacePositions			: register( t1 );

// The depth buffer
Texture2D<float>					g_DepthTexture					: register( t2 );

// The fine-grained per-tile particle lists
Buffer<uint>						g_TiledIndexBuffer				: register( t3 );

// The number of particles in the coarse culling bins. Only used for debug visualization
Buffer<uint>						g_CoarseBufferCounters			: register( t4 );

// The particle texture atlas
Texture2D 							g_ParticleTexture				: register( t6 );

// The screen space out UAV
RWBuffer<float4>					g_OutputBuffer					: register( u0 );


#define NUM_THREADS_X TILE_RES_X
#define NUM_THREADS_Y TILE_RES_Y
#define NUM_THREADS_PER_TILE (NUM_THREADS_X*NUM_THREADS_Y)

// The number of particles we render can be less than the number of particles stored in the tile after culling. 
// As the particles are cached to LDS for efficiency we want to strike a balance between not using too much LDS and 
// making sure we render enough particles to the tile without causing visual artefacts caused by omitting particles
// from the render. The particles are already sorted front-to-back so we are only losing particles that are most likely 
// occluded by particles nearer to the camera.
#define	MAX_PARTICLES_PER_TILE_FOR_RENDERING		500

// Cached values for the particles
groupshared float4				g_ParticleTint[ MAX_PARTICLES_PER_TILE_FOR_RENDERING ];					// We could probably compress this
groupshared uint				g_ParticleEmitterProperties[ MAX_PARTICLES_PER_TILE_FOR_RENDERING ];	// This could be compressed further
groupshared float				g_ParticleEmitterNdotL[ MAX_PARTICLES_PER_TILE_FOR_RENDERING ];			// This could be compressed as we don't need much precision for the lighting term

#if defined (STREAKS)
groupshared float2				g_ParticleVelocity[ MAX_PARTICLES_PER_TILE_FOR_RENDERING ];
groupshared float				g_ParticleStreakLength[ MAX_PARTICLES_PER_TILE_FOR_RENDERING ];
#endif

groupshared float3				g_ParticlePosition[ MAX_PARTICLES_PER_TILE_FOR_RENDERING ];
groupshared float				g_ParticleRadius[ MAX_PARTICLES_PER_TILE_FOR_RENDERING ];

groupshared float				g_ParticleRotation[ MAX_PARTICLES_PER_TILE_FOR_RENDERING ];

groupshared uint				g_ldsNumParticles;


// Initialize the LDS to store the particles we want to render in the tile
void InitLDS( uint3 localIdx, uint3 globalIdx )
{
	uint localIdxFlattened = localIdx.x + ( localIdx.y * NUM_THREADS_X );

	// Find the culling tile this rendering tile corresponds to
	uint2 screenCoords = globalIdx.xy;
	uint2 cullingTileId =  screenCoords / uint2( TILE_RES_X, TILE_RES_Y );

	uint tileIdxFlattened = cullingTileId.x + cullingTileId.y * g_NumTilesX;
	uint tiledStartOffset = PARTICLES_TILE_BUFFER_SIZE * tileIdxFlattened;
	
	// Clamp the number of particles to the number we can store in LDS. Backmost particles will get chopped off the list
	if ( localIdxFlattened == 0 )
	{
		// The first element in the index list is the number of particles in that list
		g_ldsNumParticles = min( MAX_PARTICLES_PER_TILE_FOR_RENDERING, g_TiledIndexBuffer[ tiledStartOffset ] );
	}

	GroupMemoryBarrierWithGroupSync();
	
	// Each thread in the thread group will load some particles from the buffer into LDS
	uint numParticlesToCache = g_ldsNumParticles;
	for ( uint i = localIdxFlattened; i < numParticlesToCache; i += NUM_THREADS_PER_TILE )
	{
		uint globalParticleIndex = g_TiledIndexBuffer[ tiledStartOffset + 1 + i ]; // skip past the first element which is the number of particles
		
		// Load the particle data into LDS
		g_ParticleTint[ i ] = g_ParticleBuffer[ globalParticleIndex ].m_TintAndAlpha;
		g_ParticleEmitterProperties[ i ] = g_ParticleBuffer[ globalParticleIndex ].m_EmitterProperties;
		g_ParticleEmitterNdotL[ i ] = g_ParticleBuffer[ globalParticleIndex ].m_EmitterNdotL;

		g_ParticlePosition[ i ].xyz = g_ViewSpacePositions[ globalParticleIndex ].xyz;
		g_ParticleRadius[ i ] = g_ViewSpacePositions[ globalParticleIndex ].w;

		g_ParticleRotation[ i ] = g_ParticleBuffer[ globalParticleIndex ].m_Rotation;
		
#if defined (STREAKS)
		g_ParticleVelocity[ i ] = normalize( g_ParticleBuffer[ globalParticleIndex ].m_VelocityXY );
		g_ParticleStreakLength[ i ] = calcEllipsoidRadius( g_ViewSpacePositions[ globalParticleIndex ].w, g_ParticleBuffer[ globalParticleIndex ].m_VelocityXY ).y;
#endif
	}

	// Sync to make sure all our particles are loaded before going to the rendering phase
	GroupMemoryBarrierWithGroupSync();
}


// Helper function to determine the intersection point of a ray on a plane
float3 CalcPointOnViewPlane( float3 pointOnPlane, float3 rayDir )
{
	float t = pointOnPlane.z / rayDir.z;
	float3 p = t * rayDir;
	return p;
}


// Calculate the particle contribution to this pixel
float4 calcBillboardParticleColor( uint particleIndex, float3 rayDir, float viewSpaceDepth )
{
	// Retrieve the particle data from LDS
	uint emitterProperties = g_ParticleEmitterProperties[ particleIndex ];
	bool usesStreaks = IsStreakEmitter( emitterProperties );
	float textureOffset = GetTextureOffset( emitterProperties );

	float emitterNdotL = g_ParticleEmitterNdotL[ particleIndex ];
	float4 tintAndAlpha = g_ParticleTint[ particleIndex ];

#if defined (STREAKS)
	float2 particleVelocity = g_ParticleVelocity[ particleIndex ];
	float  particleStreakLength = g_ParticleStreakLength[ particleIndex ];
#endif	

	float3 particleCenter = g_ParticlePosition[ particleIndex ];
	float  particleRadius = g_ParticleRadius[ particleIndex ];

	float rotationAngle = g_ParticleRotation[ particleIndex ];
	

	float3 viewSpacePos = particleCenter;
		
	// This particle has no contribution if it is behind the opaque scene
	[branch]
	if ( viewSpacePos.z > viewSpaceDepth )
		return 0;

	// Calculate the depth fade factor for soft particle blending with the opaque scene
	float depthFade = saturate( ( viewSpaceDepth - viewSpacePos.z ) / particleRadius );

	// Apply the particle tint and opacity
	float4 color = 1;
	color *= tintAndAlpha;
	
	// Multiply by depth fade
	color.a *= depthFade;
		
	// Get the point on the plane for this pixel
	float3 pointOnPlane = CalcPointOnViewPlane( viewSpacePos, rayDir );
	
	// Calculate the vector from the particle centre to this pixel normalized by the radius
	float2 vecToSurface;
	float2 rotatedVecToSurface;
#if defined (STREAKS)
	if ( usesStreaks )
	{
		float2 extrusionVector = particleVelocity;
		float2 tangentVector = float2( extrusionVector.y, -extrusionVector.x );

		float2x2 transform = float2x2( tangentVector, extrusionVector );
			
		float2 vecToCentre = pointOnPlane.xy - viewSpacePos.xy;
		vecToCentre = mul( transform, vecToCentre );
		
		float2 radius = float2( particleRadius, particleStreakLength );
		vecToSurface = vecToCentre / radius;

		rotatedVecToSurface = vecToSurface;
	}
	else
#endif
	{
		vecToSurface = (pointOnPlane.xy - viewSpacePos.xy) / particleRadius;

		float s, c;
		sincos( rotationAngle, s, c );
		float2x2 rotation = { float2( c, s ), float2( -s, c ) };
		
		rotatedVecToSurface = mul( vecToSurface, rotation );
		//rotatedVecToSurface = vecToSurface; // comment in to see cost of particle rotations
	}
	
	// Scale and bias into UV space
	float2 rotatedUV = 0.5 * rotatedVecToSurface + 0.5;
	
	// If the UVs are out of range then this pixel does not fall without the particle billboard so no contribution
	[branch]
	if ( rotatedUV.x < 0 || rotatedUV.y < 0 || rotatedUV.x > 1 || rotatedUV.y > 1 )
	{
		color = 0;
	}
	else
	{
		// Shift UVs for texture atlassing
		float2 texCoord = rotatedUV;
		texCoord.x *= 0.5;
		texCoord.x += textureOffset;

		// Multiply the texture with the color
		color *= g_ParticleTexture.SampleLevel( g_samClampLinear, texCoord, 0 );
		
#if defined (NOLIGHTING)
		// For the NOLIGHTING case do no further lighting
#else
#if defined (CHEAP)
		// For CHEAP lighting do no per-particle lighting
		float ndotl = 0.7;
#else
		// Generate a normal by modelling the particle as a sphere
		float pi = 3.1415926535897932384626433832795;

		// The unrotated vector to the surface
		float2 uv = 0.5 * vecToSurface + 0.5;
	
		float3 n;
		n.x = -cos( pi * uv.x );
		n.y = -cos( pi * uv.y );
		n.z = sin( pi * length( uv ) );
		n = normalize( n );
	
		// Particle spherical lighting contribution
		float ndotl = saturate( dot( g_SunDirectionVS.xyz, n ) );
#endif

		// Mix the per-particle lihting with the per-emitter term
		ndotl = lerp( ndotl, emitterNdotL, 0.5 );

		// Light the particle using a bit of ambient plus directional
		float3 lighting = g_AmbientColor.rgb + ndotl * g_SunColor.rgb;

		color.rgb *= lighting;
#endif
	}

	return color;
}


// Blend all the particles together from front to back for this pixel
float4 blendParticlesFrontToBack( float3 viewRay, float viewSpaceDepth )
{
	// Fetch the number of particles to consider for rendering
	uint numParticles = g_ldsNumParticles;
	
	// Initialize the accumulation color to zero
	float4 fcolor = float4(0,0,0,0);
	
	// Loop through all the particles from front to back
	for ( uint i = 0; i < numParticles; i++ )
	{	
		// Get this particle's contribution (might be zero if the pixel does not intersect the billboard)
		float4 color = calcBillboardParticleColor( i, viewRay, viewSpaceDepth );
		
		// Manually blend the color and alpha to the accumlation color
		fcolor.xyz = (1-fcolor.w) * (color.w*color.xyz) + fcolor.xyz;
		fcolor.w = color.w + (1-color.w) * fcolor.w;
		
		// If we are close enough to being opaque then let's bail out now
		if ( fcolor.w > g_AlphaThreshold )
		{
			// Just force the alpha to 1
			fcolor.w = 1;
			break;
		}
	}

	return fcolor;
}


// Debug visualization to display how populated each fine-grained tile is
float4 displayTileSortComplexity()
{
	static const float4 kRadarColors[14] = 
	{
		{0,0.9255,0.9255,1},   // cyan
		{0,0.62745,0.9647,1},  // light blue
		{0,0,0.9647,1},        // blue
		{0,1,0,1},             // bright green
		{0,0.7843,0,1},        // green
		{0,0.5647,0,1},        // dark green
		{1,1,0,1},             // yellow
		{0.90588,0.75294,0,1}, // yellow-orange
		{1,0.5647,0,1},        // orange
		{1,0,0,1},             // bright red
		{0.8392,0,0,1},        // red
		{0.75294,0,0,1},       // dark red
		{1,0,1,1},             // magenta
		{0.6,0.3333,0.7882,1}, // purple
	};

	bool useRadarColors = true;

	uint numParticles = g_ldsNumParticles;
	
	float4 fcolor = float4(0,0,0,0.2);

	if ( numParticles > 0 )
	{
		if ( useRadarColors )
		{
			if ( numParticles >= MAX_PARTICLES_PER_TILE_FOR_RENDERING )
			{
				fcolor = float4( 1, 1, 1, 1 );
			}
			else if ( numParticles > 0 )
			{
				// use a log scale to provide more detail when the number of lights is smaller

				// want to find the base b such that the logb of MAX_PARTICLES_PER_TILE_FOR_RENDERING is 14
				// (because we have 14 radar colors)
				float fLogBase = exp2(0.07142857f*log2((float)MAX_PARTICLES_PER_TILE_FOR_RENDERING));

				// change of base
				// logb(x) = log2(x) / log2(b)
				uint nColorIndex = floor(log2((float)numParticles) / log2(fLogBase));
				fcolor = kRadarColors[nColorIndex];
				fcolor.a = 0.5;
			}
		}
		else
		{
			numParticles = max( numParticles, 20 );
			fcolor.r = saturate( ((float)numParticles * 1.0) / (float)MAX_PARTICLES_PER_TILE_FOR_RENDERING );

			if ( numParticles >= MAX_PARTICLES_PER_TILE_FOR_RENDERING - 1 )
			{
				fcolor.a = 0.4;
			}
		}
	}
	
	return fcolor;
}

// Debug visualization of coarse culling complexity
float4 displayCoarseTileComplexity( uint2 screenCoords )
{
	uint2 groupIdx = screenCoords / uint2( TILE_RES_X, TILE_RES_Y );

	uint tileX = groupIdx.x / g_NumCullingTilesPerCoarseTileX;
	uint tileY = groupIdx.y / g_NumCullingTilesPerCoarseTileY;

	uint coarseTileIdx = tileX + tileY * g_NumCoarseCullingTilesX;

	uint numParticles = g_CoarseBufferCounters[ coarseTileIdx ];
	
	
	float4 fcolor = float4( 0.1,0.0,0.0,0.7);

	fcolor.g = saturate( ((float)numParticles * 10.0) / (float)g_NumActiveParticles );

	return fcolor;
}


// Given a pixel in screen space, evaluate the pixel color by walking through all the pixels in the tile
// and blending their contributions together
void EvaluateColorAtScreenCoord( uint2 screenSpaceCoord )
{
	// Load the depth of the opaque scene at this point in screen space
	float depth = g_DepthTexture.Load( uint3( screenSpaceCoord.x, screenSpaceCoord.y, 0 ) ).x;

	float2 screenCoord = (float2)screenSpaceCoord;
	screenCoord += 0.5;

	// Generate the view space position
	float4 viewSpacePos;
	viewSpacePos.x = ( screenCoord.x ) / (float)g_ScreenWidth;
	viewSpacePos.y = 1 - ( screenCoord.y / (float)g_ScreenHeight );
	viewSpacePos.xy = (2*viewSpacePos.xy) - 1;
	viewSpacePos.z = depth;
	viewSpacePos.w = 1;
	
	viewSpacePos = mul( viewSpacePos, g_mProjectionInv );
	viewSpacePos.xyz /= viewSpacePos.w;
	float viewSpaceDepth = viewSpacePos.z;

	// Generate a view ray into the screen
	float3 viewRay = normalize( viewSpacePos.xyz );
	
	// Evaluate the pixel color
	float4 color = blendParticlesFrontToBack( viewRay, viewSpaceDepth );
	
	// Clamp the screen coords when the screen width is not a multiple of the tile size
	if ( screenSpaceCoord.x > g_ScreenWidth )
		screenSpaceCoord.x = g_ScreenWidth;

	// Calculate the index into the UAV of this screen space pixel
	uint pixelLocation = screenSpaceCoord.x + (screenSpaceCoord.y * g_ScreenWidth);
	
	// Write the color out to the UAV
	g_OutputBuffer[ pixelLocation ] = color;
}


// Entry point for tiled rendering. Each thread maps to a pixel in screen space
[numthreads(NUM_THREADS_X, NUM_THREADS_Y, 1)]
void FrontToBack( uint3 localIdx : SV_GroupThreadID, uint3 groupIdx : SV_GroupID, uint3 globalIdx : SV_DispatchThreadID )
{
	// Load the particle data into LDS
	InitLDS( localIdx, globalIdx );

	// Evaluate the pixel
	EvaluateColorAtScreenCoord( globalIdx.xy );
}

/*
// Debug visualization for the tiloe complexity
[numthreads(NUM_THREADS_X, NUM_THREADS_Y, 1)]
void Overdraw( uint3 localIdx : SV_GroupThreadID, uint3 groupIdx : SV_GroupID, uint3 globalIdx : SV_DispatchThreadID )
{
	uint2 screenSpaceCoord = globalIdx.xy;

	// Calculate the index into the UAV of this screen space pixel
	uint pixelLocation = screenSpaceCoord.x + (screenSpaceCoord.y * g_ScreenWidth);
	
	uint3 forcedGroupIdx = uint3( 30, 15, 0 );
	uint3 forcedGlobalIdx = forcedGroupIdx * 32 + localIdx;

	float4 color = 0;

	InitLDS( localIdx, forcedGlobalIdx );
	
	uint numParticles = g_ldsNumParticles;
	
	float depth = g_DepthTexture.Load( uint3( screenSpaceCoord.x, screenSpaceCoord.y, 0 ) ).x;


	float4 viewSpacePos;
	viewSpacePos.x = ( screenSpaceCoord.x ) / (float)g_ScreenWidth;
	viewSpacePos.y = 1 - ( screenSpaceCoord.y / (float)g_ScreenHeight );
	viewSpacePos.xy = (2*viewSpacePos.xy) - 1;
	viewSpacePos.z = depth;
	viewSpacePos.w = 1;
	
	viewSpacePos = mul( viewSpacePos, g_mProjectionInv );
	viewSpacePos.xyz /= viewSpacePos.w;
	float viewSpaceDepth = viewSpacePos.z;


	// Generate a view ray into the screen
	float3 viewRay = normalize( viewSpacePos.xyz );

	for ( uint i = 0; i < numParticles; i++ )
	{
		float3 particleCenter = g_ParticlePosition[ i ];
		float  particleRadius = g_ParticleRadius[ i ];

		float rotationAngle = g_ParticleRotation[ i ];
	
		// Calculate the vector from the particle centre to this pixel normalized by the radius
		float2 vecToSurface;
		float2 rotatedVecToSurface;
		float3 viewSpacePos = particleCenter;
		
	
		
		// Get the point on the plane for this pixel
		float3 pointOnPlane = CalcPointOnViewPlane( viewSpacePos, viewRay );
	


		{
			vecToSurface = (pointOnPlane.xy - viewSpacePos.xy) / particleRadius;

			float s, c;
			sincos( rotationAngle, s, c );
			float2x2 rotation = { float2( c, s ), float2( -s, c ) };
		
			rotatedVecToSurface = mul( vecToSurface, rotation );
			//rotatedVecToSurface = vecToSurface; // comment in to see cost of particle rotations
		}
	
		// Scale and bias into UV space
		float2 rotatedUV = 0.5 * rotatedVecToSurface + 0.5;
	
		// If the UVs are out of range then this pixel does not fall without the particle billboard so no contribution
		[branch]
		if ( rotatedUV.x < 0 || rotatedUV.y < 0 || rotatedUV.x > 1 || rotatedUV.y > 1 )
		{
			//color = 0;
			
		}
		else
		{
			color += 0.01f;
		}
	}

	if ( groupIdx.x == forcedGroupIdx.x && groupIdx.y == forcedGroupIdx.y )
	{
		color = 0;
	}
	
	g_OutputBuffer[ pixelLocation ] = color;
}*/






// Debug visualization for the tiloe complexity
[numthreads(NUM_THREADS_X, NUM_THREADS_Y, 1)]
void Overdraw( uint3 localIdx : SV_GroupThreadID, uint3 groupIdx : SV_GroupID, uint3 globalIdx : SV_DispatchThreadID )
{
	InitLDS( localIdx, globalIdx );
		
	uint2 screenSpaceCoord = globalIdx.xy / 2;

	uint2 coarseTileCoords = screenSpaceCoord;

	float4 color = displayCoarseTileComplexity( globalIdx.xy );
	
	uint pixelLocation = coarseTileCoords.x + (coarseTileCoords.y * g_ScreenWidth ); 
	g_OutputBuffer[ pixelLocation ] = color;
	
	screenSpaceCoord.x += g_ScreenWidth / 2;
	if ( screenSpaceCoord.x > g_ScreenWidth )
		screenSpaceCoord.x = g_ScreenWidth;
	color = displayTileSortComplexity();
	
	pixelLocation = screenSpaceCoord.x + (screenSpaceCoord.y * g_ScreenWidth); 
	g_OutputBuffer[ pixelLocation ] = color;
	
	if ( globalIdx.y >= g_ScreenHeight / 2 )
	{
		EvaluateColorAtScreenCoord( globalIdx.xy );
	}
}

