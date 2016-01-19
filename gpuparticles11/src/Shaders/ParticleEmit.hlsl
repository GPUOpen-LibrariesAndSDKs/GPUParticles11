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


// A texture filled with random values for generating some variance in our particles when we spawn them
Texture2D								g_RandomBuffer			: register( t0 );

// The particle buffers to fill with new particles
RWStructuredBuffer<GPUParticlePartA>	g_ParticleBufferA		: register( u0 );
RWStructuredBuffer<GPUParticlePartB>	g_ParticleBufferB		: register( u1 );

// The dead list interpretted as a consume buffer. So every time we consume an index from this list, it automatically decrements the atomic counter (ie the number of dead particles)
ConsumeStructuredBuffer<uint>			g_DeadListToAllocFrom	: register( u2 );


cbuffer EmitterConstantBuffer : register( b1 )
{
	float4	g_vEmitterPosition;
	float4	g_vEmitterVelocity;
	float4	g_PositionVariance;
	
	int		g_MaxParticlesThisFrame;
	float	g_ParticleLifeSpan;	
	float	g_StartSize;
	float	g_EndSize;
	
	float	g_VelocityVariance;
	float	g_Mass;
	uint	g_EmitterIndex;
	uint	g_EmitterStreaks;

	uint	g_TextureIndex;
	uint	g_pads[ 3 ];
};


// Emit particles, one per thread, in blocks of 1024 at a time
[numthreads(1024,1,1)]
void CS_Emit( uint3 id : SV_DispatchThreadID )
{
	// Check to make sure we don't emit more particles than we specified
	if ( id.x < g_NumDeadParticles && id.x < g_MaxParticlesThisFrame )
	{
		// Initialize the particle data to zero to avoid any unexpected results
		GPUParticlePartA pa = (GPUParticlePartA)0;
		GPUParticlePartB pb = (GPUParticlePartB)0;
		
		// Generate some random numbers from reading the random texture
		float2 uv = float2( id.x / 1024.0, g_ElapsedTime );
		float3 randomValues0 = g_RandomBuffer.SampleLevel( g_samWrapLinear, uv, 0 ).xyz;

		float2 uv2 = float2( (id.x + 1) / 1024.0, g_ElapsedTime );
		float3 randomValues1 = g_RandomBuffer.SampleLevel( g_samWrapLinear, uv2, 0 ).xyz;

		float velocityMagnitude = length( g_vEmitterVelocity.xyz );

		pb.m_Position = g_vEmitterPosition.xyz + ( randomValues0.xyz * g_PositionVariance.xyz );

		pa.m_EmitterProperties = WriteEmitterProperties( g_EmitterIndex, g_TextureIndex, g_EmitterStreaks ? true : false );
		pa.m_Rotation = 0;
		pa.m_IsSleeping = 0;
		pa.m_CollisionCount = 0;

		pb.m_Mass = g_Mass;
		pb.m_Velocity = g_vEmitterVelocity.xyz + ( randomValues1.xyz * velocityMagnitude * g_VelocityVariance );
		pb.m_Lifespan = g_ParticleLifeSpan;
		pb.m_Age = pb.m_Lifespan;
		pb.m_StartSize = g_StartSize;
		pb.m_EndSize = g_EndSize;

		// The index into the global particle list obtained from the dead list. 
		// Calling consume will decrement the counter in this buffer.
		uint index = g_DeadListToAllocFrom.Consume();

		// Write the new particle state into the global particle buffer
		g_ParticleBufferA[ index ] = pa;
		g_ParticleBufferB[ index ] = pb;
	}
}