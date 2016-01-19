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
#ifndef __PARTICLE_SYSTEM_H__
#define __PARTICLE_SYSTEM_H__


#include "..\\..\\DXUT\\Core\\DXUT.h"
#include "..\\..\\AMD_SDK\\inc\\AMD_SDK.h"


// Implementation-agnostic particle system interface
struct IParticleSystem
{
	// The rendering technique to use
	enum Technique
	{
		Technique_Rasterize,
		Technique_Tiled,
		Technique_Overdraw,
		Technique_Max
	};

	// The coarse culling mode to use, if any
	enum CoarseCullingMode
	{
		CoarseCullingOff,
		CoarseCulling4x2,
		CoarseCulling8x8,
		CoarseCulling16x16,
		NumCoarseCullingModes
	};

	// Per-frame stats from the particle system
	struct Stats
	{
		int		m_MaxParticles;
		int		m_NumActiveParticles;
		int		m_NumDead;
	};

	enum Flags
	{
		PF_Sort = 1 << 0,				// Sort the particles
		PF_CheapLighting = 1 << 1,		// Perform minimal lighting on the particles to ease ALU load
		PF_NoLighting = 1 << 2,			// Do no lighting at all, just display the particle texture on the billboard
		PF_CullMaxZ = 1 << 3,			// Do per-tile MaxZ culling if applicable
		PF_Streaks = 1 << 4,			// Streak the particles based on velocity
		PF_UseGeometryShader = 1 << 5,	// Use the GS to do the billboarding, otherwise uses the VS for better performance
		PF_ScreenSpaceCulling = 1 << 6	// Do the tile culling in screen space to avoid potential false positives with frustum culling
	};

	// Per-emitter parameters
	struct EmitterParams
	{
		DirectX::XMVECTOR	m_Position;				// World position of the emitter
		DirectX::XMVECTOR	m_Velocity;				// Velocity of each particle from the emitter
		DirectX::XMVECTOR	m_PositionVariance;		// Variance in position of each particle
		int					m_NumToEmit;			// Number of particles to emit this frame
		float				m_ParticleLifeSpan;		// How long the particles should live
		float				m_StartSize;			// Size of particles at spawn time
		float				m_EndSize;				// Size of particle when they reach retirement age
		float				m_Mass;					// Mass of particle
		float				m_VelocityVariance;		// Variance in velocity of each particle
		int					m_TextureIndex;			// Index of the texture in the atlas
		bool				m_Streaks;				// Streak the particles in the direction of travel
	};

	// Create a GPU particle system. Add more factory functions to create other types of system eg CPU-updated system
	static IParticleSystem* CreateGPUSystem( AMD::ShaderCache& shadercache );

	virtual ~IParticleSystem() {}

	virtual void OnCreateDevice( ID3D11Device* pDevice, ID3D11DeviceContext* pImmediateContext ) = 0;
	virtual void OnResizedSwapChain( const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc ) = 0;
	virtual void OnReleasingSwapChain() = 0;
	virtual void OnDestroyDevice() = 0;

	// Completely resets the state of all particles. Handy for changing scenes etc
	virtual void Reset() = 0;

	// Render the system given a frame delta.
	virtual void Render( float frameTime, int flags, Technique technique, CoarseCullingMode coarseCullingMode, const EmitterParams* pEmitters, int nNumEmitters, ID3D11ShaderResourceView* depthSRV ) = 0;
	
	// Retrive the statistics about this frame's particles
	virtual const Stats& GetStats() const = 0;
};


#endif