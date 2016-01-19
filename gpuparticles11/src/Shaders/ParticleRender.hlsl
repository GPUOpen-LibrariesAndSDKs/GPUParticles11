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
//	Shader code for rendering particles as simple quads using rasterization
//

#include "Globals.h"


struct VS_OUTPUT
{
	float4	ViewSpaceCentreAndRadius		: TEXCOORD0;
	float4	VelocityXYRotationEmitterNdotL	: TEXCOORD1;
	uint	EmitterProperties				: TEXCOORD2;
	float4	Color							: COLOR0;
};

struct PS_INPUT
{
	float4	ViewSpaceCentreAndRadius	: TEXCOORD0;
	float2	TexCoord					: TEXCOORD1;
	float3	ViewPos						: TEXCOORD2;
	float3	VelocityXYEmitterNdotL		: TEXCOORD3;
	float3	Extrusion					: TEXCOORD4;
	float4	Color						: COLOR0;
	float4 	Position					: SV_POSITION;
};


// The particle buffer data. Note this is only one half of the particle data - the data that is relevant to rendering as opposed to simulation
StructuredBuffer<GPUParticlePartA>	g_ParticleBufferA		: register( t0 );

// A buffer containing the pre-computed view space positions of the particles
StructuredBuffer<float4>			g_ViewSpacePositions	: register( t1 );

// The sorted index list of particles
StructuredBuffer<float2>			g_SortedIndexBuffer		: register( t2 );


// The geometry shader path for rendering particles. 
#if defined (USE_GEOMETRY_SHADER)
VS_OUTPUT VS_StructuredBuffer( uint VertexId : SV_VertexID )
{
	VS_OUTPUT Output = (VS_OUTPUT)0;

	// There is one particle per vertex, so use the vertex id as an index into the sorted list
	uint particleIndex = VertexId;

	// Get the global particle index
	uint index = (uint)g_SortedIndexBuffer[ g_NumActiveParticles - particleIndex - 1 ].y;

	// Retreive the particle data
	GPUParticlePartA pa = g_ParticleBufferA[ index ];
		
	// Pack the particle data into our interpolators
	Output.ViewSpaceCentreAndRadius = g_ViewSpacePositions[ index ];
	Output.VelocityXYRotationEmitterNdotL = float4( pa.m_VelocityXY.x, pa.m_VelocityXY.y, pa.m_Rotation, pa.m_EmitterNdotL );
	Output.EmitterProperties = pa.m_EmitterProperties;
	Output.Color = pa.m_TintAndAlpha;
	
	return Output;
}

[maxvertexcount(4)]
void GS( point VS_OUTPUT input[ 1 ], inout TriangleStream<PS_INPUT> SpriteStream )
{
	PS_INPUT Output = (PS_INPUT)0;

	uint emitterProperties = input[ 0 ].EmitterProperties;

	bool streaks = IsStreakEmitter( emitterProperties );

	// Generate the texture atlas UV offset
	float xOffset = GetTextureOffset( emitterProperties );
	
	const float2 offsets[ 4 ] =
	{
		float2( -1,  1 ),
		float2(  1,  1 ),
		float2( -1, -1 ),
		float2(  1, -1 ),
	};

	// Expand the vertex point into four points
	[unroll] for ( int i = 0; i < 4; i++ )
	{
		float2 offset = offsets[ i ];

		// Generate UVs
		float2 uv = (offset+1)*float2( 0.25, 0.5 );
		uv.x += xOffset;
		
		float radius = input[ 0 ].ViewSpaceCentreAndRadius.w;
		float3 cameraFacingPos;
			
		// Only apply streak logic to sparks
#if defined (STREAKS)
		if ( streaks )
		{
			float2 viewSpaceVelocity = input[ 0 ].VelocityXYRotationEmitterNdotL.xy;
			
			float2 ellipsoidRadius = calcEllipsoidRadius( radius, viewSpaceVelocity );
						
			float2 extrusionVector = normalize( viewSpaceVelocity );
			float2 tangentVector = float2( extrusionVector.y, -extrusionVector.x );
			float2x2 transform = float2x2( tangentVector, extrusionVector );

			Output.Extrusion.xy = extrusionVector;
			Output.Extrusion.z = 1.0;
			
			cameraFacingPos = input[ 0 ].ViewSpaceCentreAndRadius.xyz;
			
			cameraFacingPos.xy += mul( offset * ellipsoidRadius, transform );
		}
		else
#endif
		{
			float s, c;
			sincos( input[ 0 ].VelocityXYRotationEmitterNdotL.z, s, c );
			float2x2 rotation = { float2( c, -s ), float2( s, c ) };
		
			offset = mul( offset, rotation );

			cameraFacingPos = input[ 0 ].ViewSpaceCentreAndRadius.xyz;
			cameraFacingPos.xy += radius * offset;
		}
		
		Output.Position = mul( float4( cameraFacingPos, 1 ), g_mProjection );
		
		Output.TexCoord = uv;
		Output.Color = input[ 0 ].Color;
		Output.ViewSpaceCentreAndRadius = input[ 0 ].ViewSpaceCentreAndRadius;
		Output.VelocityXYEmitterNdotL = input[ 0 ].VelocityXYRotationEmitterNdotL.xyw;
		Output.ViewPos = cameraFacingPos;

		SpriteStream.Append( Output );
	}
	SpriteStream.RestartStrip();
}

#else

// Vertex shader only path
PS_INPUT VS_StructuredBuffer( uint VertexId : SV_VertexID )
{
	PS_INPUT Output = (PS_INPUT)0;

	// Particle index 
	uint particleIndex = VertexId / 4;

	// Per-particle corner index
	uint cornerIndex = VertexId % 4;

	float xOffset = 0;
	
	const float2 offsets[ 4 ] =
	{
		float2( -1,  1 ),
		float2(  1,  1 ),
		float2( -1, -1 ),
		float2(  1, -1 ),
	};

	uint index = (uint)g_SortedIndexBuffer[ g_NumActiveParticles - particleIndex - 1 ].y;
	GPUParticlePartA pa = g_ParticleBufferA[ index ];
		
	float4 ViewSpaceCentreAndRadius = g_ViewSpacePositions[ index ];
	float3 VelocityXYEmitterNdotL = float3( pa.m_VelocityXY.x, pa.m_VelocityXY.y, pa.m_EmitterNdotL );
		
	uint emitterProperties = pa.m_EmitterProperties;

	bool streaks = IsStreakEmitter( emitterProperties );

	float2 offset = offsets[ cornerIndex ];
	float2 uv = (offset+1)*float2( 0.25, 0.5 );
	uv.x += GetTextureOffset( emitterProperties );
		
	float radius = ViewSpaceCentreAndRadius.w;
	float3 cameraFacingPos;
			
#if defined (STREAKS)
	if ( streaks )
	{
		float2 viewSpaceVelocity = VelocityXYEmitterNdotL.xy;
		
		float2 ellipsoidRadius = calcEllipsoidRadius( radius, viewSpaceVelocity );
		
		float2 extrusionVector = normalize( viewSpaceVelocity );
		float2 tangentVector = float2( extrusionVector.y, -extrusionVector.x );
		float2x2 transform = float2x2( tangentVector, extrusionVector );

		Output.Extrusion.xy = extrusionVector;
		Output.Extrusion.z = 1.0;
			
		cameraFacingPos = ViewSpaceCentreAndRadius.xyz;
			
		cameraFacingPos.xy += mul( offset * ellipsoidRadius, transform );
	}
	else
#endif
	{
		float s, c;
		sincos( pa.m_Rotation, s, c );
		float2x2 rotation = { float2( c, -s ), float2( s, c ) };
		
		offset = mul( offset, rotation );

		cameraFacingPos = ViewSpaceCentreAndRadius.xyz;
		cameraFacingPos.xy += radius * offset;
	}
		
	Output.Position = mul( float4( cameraFacingPos, 1 ), g_mProjection );
		
	Output.TexCoord = uv;
	Output.Color = pa.m_TintAndAlpha;
	Output.ViewSpaceCentreAndRadius = ViewSpaceCentreAndRadius;
	Output.VelocityXYEmitterNdotL = VelocityXYEmitterNdotL;
	Output.ViewPos = cameraFacingPos;
	
	return Output;
}

#endif


// The texture atlas for the particles
Texture2D 			g_ParticleTexture			 	: register( t0 );

// The opaque scene depth buffer read as a texture
Texture2D<float>	g_DepthTexture					: register( t1 );


// Ratserization path's pixel shader
float4 PS_Billboard( PS_INPUT In ) : SV_TARGET
{ 
	// Retrieve the particle data
	float3 particleViewSpacePos = In.ViewSpaceCentreAndRadius.xyz;
	float  particleRadius = In.ViewSpaceCentreAndRadius.w;

	// Get the depth at this point in screen space
	float depth = g_DepthTexture.Load( uint3( In.Position.x, In.Position.y, 0 ) ).x;

	// Get viewspace position by generating a point in screen space at the depth of the depth buffer
	float4 viewSpacePos;
	viewSpacePos.x = In.Position.x / (float)g_ScreenWidth;
	viewSpacePos.y = 1 - ( In.Position.y / (float)g_ScreenHeight );
	viewSpacePos.xy = (2*viewSpacePos.xy) - 1;
	viewSpacePos.z = depth;
	viewSpacePos.w = 1;

	// ...then transform it into view space using the inverse projection matrix and a divide by W
	viewSpacePos = mul( viewSpacePos, g_mProjectionInv );
	viewSpacePos.xyz /= viewSpacePos.w;

	// remove this?
	if ( particleViewSpacePos.z > viewSpacePos.z )
	{
		clip( -1 );
	}
	//~
	
	// Calculate the depth fade factor
	float depthFade = saturate( ( viewSpacePos.z - particleViewSpacePos.z ) / particleRadius );

	float4 albedo = 1;
	albedo.a = depthFade;
	
	// Read the texture atlas
	albedo *= g_ParticleTexture.SampleLevel( g_samClampLinear, In.TexCoord, 0 );	// 2d
	
	// Multiply in the particle color
	float4 color = albedo * In.Color;
	
	// Calculate the UV based the screen space position
	float3 n = 0;
	float2 uv;
#if defined (STREAKS)
	if ( In.Extrusion.z > 0.0 )
	{
		float2 ellipsoidRadius = calcEllipsoidRadius( particleRadius, In.VelocityXYEmitterNdotL.xy );
			
		float2 extrusionVector = In.Extrusion.xy;
		float2 tangentVector = float2( extrusionVector.y, -extrusionVector.x );
		float2x2 transform = float2x2( tangentVector, extrusionVector );

		
		float2 vecToCentre = In.ViewPos.xy - particleViewSpacePos.xy;
		vecToCentre = mul( transform, vecToCentre );
		
		uv = vecToCentre / ellipsoidRadius;
	}
	else
#endif
	{
		uv = (In.ViewPos.xy - particleViewSpacePos.xy ) / particleRadius;
	}

	// Scale and bias
	uv = (1+uv)*0.5;

	// Skip the lighting calcs if NOLIGHTING is specified
#if defined (NOLIGHTING)

#else
	// For CHEAP lighting, assume a fixed lighting contribution per particle and use the emitter lighting term
#if defined (CHEAP)
	float ndotl = 0.7;
#else

	float pi = 3.1415926535897932384626433832795;

	n.x = -cos( pi * uv.x );
	n.y = -cos( pi * uv.y );
	n.z = sin( pi * length( uv ) );
	n = normalize( n );
	
	float ndotl = saturate( dot( g_SunDirectionVS.xyz, n ) );
#endif

	// Fetch the emitter's lighting term
	float emitterNdotL = In.VelocityXYEmitterNdotL.z;

	// Mix the particle lighting term with the emitter lighting
	ndotl = lerp( ndotl, emitterNdotL, 0.5 );

	// Ambient lighting plus directional lighting
	float3 lighting = g_AmbientColor + ndotl * g_SunColor;

	// Multiply lighting term in
	color.rgb *= lighting;
#endif

	return color;
}