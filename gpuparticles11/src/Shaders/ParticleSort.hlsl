//--------------------------------------------------------------------------------------
// File: ParticleSort.hlsl
//
// The HLSL file for sorting particles
// 
// Author: Gareth Thomas
// 
// Copyright © AMD Corporation. All rights reserved.
//--------------------------------------------------------------------------------------


RWStructuredBuffer<float2>		g_IndexBuffer	: register( u0 );


cbuffer SortConstants : register( b5 )
{
    uint4 g_kj;
}

cbuffer ActiveListCount : register( b6 )
{
	int		g_NumActiveParticles;
	int3	pad;
};


[numthreads(64, 1, 1)]
void BitonicSortStep( uint3 DTid : SV_DispatchThreadID )
{
	uint index_low = DTid.x & (g_kj.y-1);
	uint index_high = 2*(DTid.x-index_low);
	uint index = index_high + index_low;
	uint nSwapElem = index ^ g_kj.y;

	if ( index < g_NumActiveParticles && nSwapElem < g_NumActiveParticles )
	{
		float2 a = g_IndexBuffer[index];
		float2 b = g_IndexBuffer[nSwapElem];

		bool ascending = ( index > g_kj.w ) ?
							( index & g_kj.z ) == 0 :
							( index & g_kj.x ) == 0;
		bool AisGreater = (a.x < b.x);
		if ( ascending == AisGreater )
		{ 
			g_IndexBuffer[index] = b;
			g_IndexBuffer[nSwapElem] = a;
		}
	}
}
