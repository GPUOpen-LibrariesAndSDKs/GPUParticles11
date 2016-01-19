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


// Per-frame constant buffer
cbuffer PerFrameConstantBuffer : register( b0 )
{
	float4	g_StartColor[ NUM_EMITTERS ];
	float4	g_EndColor[ NUM_EMITTERS ];

	float4	g_EmitterLightingCenter[ NUM_EMITTERS ];

	matrix	g_mViewProjection;
	matrix	g_mViewProjInv;
	matrix	g_mView;
	matrix	g_mViewInv;
	matrix  g_mProjection;
	matrix  g_mProjectionInv;

	float4	g_EyePosition;
	float4	g_SunDirection;
	float4	g_SunColor;
	float4	g_AmbientColor;

	float4	g_SunDirectionVS;
	float4	pads2[ 3 ];

	float	g_fFrameTime;
	int		g_ScreenWidth;
	int		g_ScreenHeight;
	int		g_FrameIndex;
	
	float	g_AlphaThreshold;
	float	g_CollisionThickness;
	float	g_ElapsedTime;
	int		g_CollideParticles;

	int		g_ShowSleepingParticles;
	int		g_EnableSleepState;
	int		g_Pads[ 2 ];
};


// The number of dead particles in the system
cbuffer DeadListCount : register( b2 )
{
	uint	g_NumDeadParticles;
	uint3	DeadListCount_pad;
};


// The number of alive particles this frame
cbuffer ActiveListCount : register( b3 )
{
	uint	g_NumActiveParticles;
	uint3	ActiveListCount_pad;
};


// Tiling constants that are dependant on the screen resolution
cbuffer TilingConstantBuffer : register( b5 )
{
	uint g_NumTilesX;
	uint g_NumTilesY;
	uint g_NumCoarseCullingTilesX;
	uint g_NumCoarseCullingTilesY;

	uint g_NumCullingTilesPerCoarseTileX;
	uint g_NumCullingTilesPerCoarseTileY;
	uint g_TilingCBPads[ 2 ];
};


// Particle structures
// ===================

struct GPUParticlePartA
{
	float4	m_TintAndAlpha;			// The color and opacity
	float2	m_VelocityXY;			// View space velocity XY used for streak extrusion
	float	m_EmitterNdotL;			// The lighting term for the while emitter
	uint	m_EmitterProperties;	// The index of the emitter in 0-15 bits, 16-23 for atlas index, 24th bit is whether or not the emitter supports velocity-based streaks

	float	m_Rotation;				// The rotation angle
	uint	m_IsSleeping;			// Whether or not the particle is sleeping (ie, don't update position)
	uint	m_CollisionCount;		// Keep track of how many times the particle has collided
	float	m_pads[ 1 ];
};

struct GPUParticlePartB
{
	float3	m_Position;				// World space position
	float	m_Mass;					// Mass of particle

	float3	m_Velocity;				// World space velocity
	float	m_Lifespan;				// Lifespan of the particle.
	
	float	m_DistanceToEye;		// The distance from the particle to the eye
	float	m_Age;					// The current age counting down from lifespan to zero
	float	m_StartSize;			// The size at spawn time
	float	m_EndSize;				// The time at maximum age
};


uint GetEmitterIndex( uint emitterProperties )
{
	return emitterProperties & 0xffff;
}


float GetTextureOffset( uint emitterProperties )
{
	uint index = (emitterProperties & 0x000f0000) >> 16;

	return (float)index / 2.0f;
}


bool IsStreakEmitter( uint emitterProperties )
{
	return ( emitterProperties >> 24 ) & 0x01 ? true : false;
}


uint WriteEmitterProperties( uint emitterIndex, uint textureIndex, bool isStreakEmitter )
{
	uint properties = emitterIndex & 0xffff;

	properties |= textureIndex << 16;

	if ( isStreakEmitter )
	{
		properties |= 1 << 24;
	}

	return properties;
}



// Function to calculate the streak radius in X and Y given the particles radius and velocity
float2 calcEllipsoidRadius( float radius, float2 viewSpaceVelocity )
{
	float minRadius = radius * max( 1.0, 0.1*length( viewSpaceVelocity ) );
	return float2( radius, minRadius );
}


// this creates the standard Hessian-normal-form plane equation from three points, 
// except it is simplified for the case where the first point is the origin
float3 CreatePlaneEquation( float3 b, float3 c )
{
	return normalize(cross( b, c ));
}


// point-plane distance, simplified for the case where 
// the plane passes through the origin
float GetSignedDistanceFromPlane( float3 p, float3 eqn )
{
    // dot( eqn.xyz, p.xyz ) + eqn.w, , except we know eqn.w is zero 
    // (see CreatePlaneEquation above)
    return dot( eqn, p );
}


// convert a point from post-projection space into view space
float3 ConvertProjToView( float4 p )
{
	p = mul( p, g_mProjectionInv );
	p /= p.w;
    
    return p.xyz;
}


// Declare the global samplers
SamplerState g_samWrapLinear		: register( s0 );
SamplerState g_samClampLinear		: register( s1 );

