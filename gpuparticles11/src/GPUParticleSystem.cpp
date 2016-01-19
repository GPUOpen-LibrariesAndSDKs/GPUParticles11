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
#include "ParticleSystem.h"
#include "ParticleHelpers.h"
#include "Shaders/ShaderConstants.h"
#include "SortLib.h"


#pragma warning( disable : 4100 ) // disable unreference formal parameter warnings for /W4 builds


// Helper function to align values
int align( int value, int alignment ) { return ( value + (alignment - 1) ) & ~(alignment - 1); }


// GPUParticle structure is split into two sections for better cache efficiency - could even be SOA but would require creating more vertex buffers.
struct GPUParticlePartA
{
	DirectX::XMVECTOR	m_params[ 3 ];
};

struct GPUParticlePartB
{
	DirectX::XMVECTOR	m_params[ 3 ];
};


// The per-emitter constant buffer
struct EmitterConstantBuffer
{
	DirectX::XMVECTOR	m_EmitterPosition;
	DirectX::XMVECTOR	m_EmitterVelocity;
	DirectX::XMVECTOR	m_PositionVariance;

	int					m_MaxParticlesThisFrame;
	float				m_ParticleLifeSpan;
	float				m_StartSize;
	float				m_EndSize;
	
	
	float				m_VelocityVariance;
	float				m_Mass;
	int					m_Index;
	int					m_Streaks;

	int					m_TextureIndex;
	int					pads[ 3 ];
};


// The tiling constant buffer. Contains all the tiling dimensions for the various stages of the culling and rendering processes.
struct TilingConstantBuffer
{
	unsigned int numTilesX;
	unsigned int numTilesY;
	unsigned int numCoarseCullingTilesX;
	unsigned int numCoarseCullingTilesY;

	unsigned int numCullingTilesPerCoarseTileX;
	unsigned int numCullingTilesPerCoarseTileY;
	unsigned int pads[ 2 ];
};


// The maximum number of supported GPU particles
static const int g_maxParticles = 400*1024;

// The maximum number of coarse tiles
static const int g_maxCoarseCullingTilesX = 16;
static const int g_maxCoarseCullingTilesY = 8;
static const int g_maxCoarseCullingTiles = g_maxCoarseCullingTilesX * g_maxCoarseCullingTilesY;


// GPU Particle System class. Responsible for updating and rendering the particles
class GPUParticleSystem : public IParticleSystem
{
public:

	GPUParticleSystem( AMD::ShaderCache& shadercache );
	
private:

	enum QualityMode
	{
		NoLighting,
		CheapLighting,
		FullLighting,
		NumQualityModes
	};

	enum ZCullingMode
	{
		CullMaxZ,
		NoZCulling,
		NumZCullingModes
	};

	enum CullingMode
	{
		FrustumCull,
		ScreenspaceCull,
		NumCullingModes
	};

	enum StreakMode
	{
		StreaksOn,
		StreaksOff,
		NumStreakModes
	};

	enum BillboardMode
	{
		UseVS,
		UseGS,
		NumBillboardModes
	};

	virtual ~GPUParticleSystem();

	virtual void OnCreateDevice( ID3D11Device* pDevice, ID3D11DeviceContext* pImmediateContext );
	virtual void OnResizedSwapChain( const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc );
	virtual void OnReleasingSwapChain();
	virtual void OnDestroyDevice();

	virtual void Reset();

	virtual void Render( float frameTime, int flags, Technique technique, CoarseCullingMode coarseCullingMode, const EmitterParams* pEmitters, int nNumEmitters, ID3D11ShaderResourceView* depthSRV );

	virtual const Stats& GetStats() const { return m_Stats; }

	void Emit( int numEmitters, const EmitterParams* emitters );
	void Simulate( int flags, ID3D11ShaderResourceView* depthSRV );
	void Sort();

#if _DEBUG
	int	ReadCounter( ID3D11UnorderedAccessView* uav );
#endif

	void CullParticlesIntoTiles( CoarseCullingMode coarseCullingMode, int flags, ID3D11ShaderResourceView* depthSRV );

	void FillRenderBuffer( int flags, ID3D11ShaderResourceView* depthSRV, Technique technique );
	void RenderQuad();
	void InitDeadList();
	void FillRandomTexture();
	void CoarseCulling( CoarseCullingMode coarseCullingMode );
		
	ID3D11Device*				m_pDevice;
	ID3D11DeviceContext*		m_pImmediateContext;

	ID3D11Buffer*				m_pParticleBufferA;
	ID3D11ShaderResourceView*	m_pParticleBufferA_SRV;
	ID3D11UnorderedAccessView*	m_pParticleBufferA_UAV;

	ID3D11Buffer*				m_pParticleBufferB;
	ID3D11UnorderedAccessView*	m_pParticleBufferB_UAV;

	ID3D11Buffer*				m_pViewSpaceParticlePositions;
	ID3D11ShaderResourceView*	m_pViewSpaceParticlePositionsSRV;
	ID3D11UnorderedAccessView*	m_pViewSpaceParticlePositionsUAV;

	ID3D11Buffer*				m_pMaxRadiusBuffer;
	ID3D11ShaderResourceView*	m_pMaxRadiusBufferSRV;
	ID3D11UnorderedAccessView*	m_pMaxRadiusBufferUAV;

	ID3D11Buffer*				m_pStridedCoarseCullingBuffer;
	ID3D11ShaderResourceView*	m_pStridedCoarseCullingBufferSRV;
	ID3D11UnorderedAccessView*	m_pStridedCoarseCullingBufferUAV;

	ID3D11Buffer*				m_pStridedCoarseCullingBufferCounters;
	ID3D11ShaderResourceView*	m_pStridedCoarseCullingBufferCountersSRV;
	ID3D11UnorderedAccessView*	m_pStridedCoarseCullingBufferCountersUAV;

	ID3D11Buffer*				m_pDeadListBuffer;
	ID3D11UnorderedAccessView*	m_pDeadListUAV;
	
#if _DEBUG
	ID3D11Buffer*				m_pDebugCounterBuffer;
#endif

	ID3D11Buffer*				m_pDeadListConstantBuffer;
	ID3D11Buffer*				m_pActiveListConstantBuffer;
	
	ID3D11Buffer*				m_pIndexBuffer;

	ID3D11VertexShader*			m_pVS[ NumStreakModes ][ NumBillboardModes ];
	ID3D11GeometryShader*		m_pGS[ NumStreakModes ];
	ID3D11PixelShader*			m_pRasterizedPS[ NumQualityModes ][ NumStreakModes ];
	
	ID3D11VertexShader*			m_pQuadVS;
	ID3D11PixelShader*			m_pQuadPS;
	
	ID3D11ComputeShader*		m_pCSSimulate[ NumBillboardModes ];
	ID3D11ComputeShader*		m_pCSInitDeadList;
	ID3D11ComputeShader*		m_pCSEmit;
	ID3D11ComputeShader*		m_pCSResetParticles;

	ID3D11Buffer*				m_pEmitterConstantBuffer;
	ID3D11Buffer*				m_pTilingConstantBuffer;
	TilingConstantBuffer		m_tilingConstants;
		
	ID3D11Buffer*				m_pAliveIndexBuffer;
	ID3D11ShaderResourceView*	m_pAliveIndexBufferSRV;
	ID3D11UnorderedAccessView*	m_pAliveIndexBufferUAV;

	int							m_NumDeadParticlesOnInit;
	int							m_NumDeadParticlesAfterEmit;
	int							m_NumDeadParticlesAfterSimulation;
	int							m_NumActiveParticlesAfterSimulation;

	bool						m_ResetSystem;

	ID3D11ComputeShader*		m_pTiledRenderingCS[ NumQualityModes ][ NumStreakModes ];
	ID3D11ComputeShader*		m_pTileComplexityCS;

	ID3D11ComputeShader*		m_pCoarseCullingCS[ NumCoarseCullingModes ];

	ID3D11ComputeShader*		m_pCullingCS[ NumZCullingModes ][ NumCullingModes ][ 2 ];
	
	ID3D11Buffer*				m_pRenderingBuffer;
	ID3D11ShaderResourceView*	m_pRenderingBufferSRV;
	ID3D11UnorderedAccessView*	m_pRenderingBufferUAV;

	ID3D11Texture2D*			m_pRandomTexture;
	ID3D11ShaderResourceView*	m_pRandomTextureSRV;

	ID3D11Buffer*				m_pIndirectDrawArgsBuffer;
	ID3D11UnorderedAccessView*	m_pIndirectDrawArgsBufferUAV;

	unsigned int				m_uWidth;
	unsigned int				m_uHeight;

	Stats						m_Stats;

	ID3D11BlendState*			m_pCompositeBlendState;
	
	SortLib						m_SortLib;

	ID3D11Buffer*				m_pTiledIndexBuffer;
	ID3D11ShaderResourceView*	m_pTiledIndexBufferSRV;
	ID3D11UnorderedAccessView*	m_pTiledIndexBufferUAV;
};


// The coarse culling configurations that we support
static const int g_NumCoarseTiles[ GPUParticleSystem::NumCoarseCullingModes ][ 2 ] = 
{
	{  0, 0 },
	{  4, 2 },
	{  8, 8 },
	{ 16, 8 },
};



IParticleSystem* IParticleSystem::CreateGPUSystem( AMD::ShaderCache& shadercache )
{
	return new GPUParticleSystem( shadercache );
}


GPUParticleSystem::GPUParticleSystem( AMD::ShaderCache& shadercache ) :
	m_pDevice( nullptr ),
	m_pImmediateContext( nullptr ),
	m_pParticleBufferA( nullptr ),
	m_pParticleBufferA_SRV( nullptr ),
	m_pParticleBufferA_UAV( nullptr ),
	m_pParticleBufferB( nullptr ),
	m_pParticleBufferB_UAV( nullptr ),
	m_pViewSpaceParticlePositions( nullptr ),
	m_pViewSpaceParticlePositionsSRV( nullptr ),
	m_pViewSpaceParticlePositionsUAV( nullptr ),
	m_pMaxRadiusBuffer( nullptr ),
	m_pMaxRadiusBufferSRV( nullptr ),
	m_pMaxRadiusBufferUAV( nullptr ),
	m_pStridedCoarseCullingBuffer( nullptr ),
	m_pStridedCoarseCullingBufferSRV( nullptr ),
	m_pStridedCoarseCullingBufferUAV( nullptr ),
	m_pStridedCoarseCullingBufferCounters( nullptr ),
	m_pStridedCoarseCullingBufferCountersSRV( nullptr ),
	m_pStridedCoarseCullingBufferCountersUAV( nullptr ),
	m_pDeadListBuffer( nullptr ),
	m_pDeadListUAV( nullptr ),
#if _DEBUG	
	m_pDebugCounterBuffer( nullptr ),
#endif
	m_pDeadListConstantBuffer( nullptr ),
	m_pActiveListConstantBuffer( nullptr ),
	m_pIndexBuffer( nullptr ),
	m_pQuadVS( nullptr ),
	m_pQuadPS( nullptr ),
	m_pCSInitDeadList( nullptr ),
	m_pCSEmit( nullptr ),
	m_pCSResetParticles( nullptr ),
	m_pEmitterConstantBuffer( nullptr ),
	m_pTilingConstantBuffer( nullptr ),
	m_pAliveIndexBuffer( nullptr ),
	m_pAliveIndexBufferSRV( nullptr ),
	m_pAliveIndexBufferUAV( nullptr ),
	m_NumDeadParticlesOnInit( 0 ),
	m_NumDeadParticlesAfterEmit( 0 ),
	m_NumDeadParticlesAfterSimulation( 0 ),
	m_NumActiveParticlesAfterSimulation( 0 ),
	m_ResetSystem( true ),
	m_pTileComplexityCS( nullptr ),
	m_pRenderingBuffer( nullptr ),
	m_pRenderingBufferSRV( nullptr ),
	m_pRenderingBufferUAV( nullptr ),
	m_pRandomTexture( nullptr ),
	m_pRandomTextureSRV( nullptr ),
	m_pIndirectDrawArgsBuffer( nullptr ),
	m_pIndirectDrawArgsBufferUAV( nullptr ),
	m_uWidth( 0 ),
	m_uHeight( 0 ),
	m_pCompositeBlendState( nullptr ),
	m_pTiledIndexBuffer( nullptr ),
	m_pTiledIndexBufferSRV( nullptr ),
	m_pTiledIndexBufferUAV( nullptr )
{
	ZeroMemory( m_pVS, sizeof( m_pVS ) );
	ZeroMemory( m_pGS, sizeof( m_pGS ) );
	ZeroMemory( m_pRasterizedPS, sizeof( m_pRasterizedPS ) );
	ZeroMemory( m_pTiledRenderingCS, sizeof( m_pTiledRenderingCS ) );
	ZeroMemory( m_pCullingCS, sizeof( m_pCullingCS ) );
	ZeroMemory( m_pCoarseCullingCS, sizeof( m_pCoarseCullingCS ) );
	ZeroMemory( m_pCSSimulate, sizeof( m_pCSSimulate ) );
	
	// Create all the shader permutations 
	AMD::ShaderCache::Macro defines[ 32 ];
	ZeroMemory( defines, sizeof( defines ) );
	
	shadercache.AddShader( (ID3D11DeviceChild**)&m_pCSInitDeadList, AMD::ShaderCache::SHADER_TYPE_COMPUTE, L"cs_5_0", L"CS_InitDeadList", L"InitDeadList.hlsl", 0, nullptr, nullptr, nullptr, 0 );
	shadercache.AddShader( (ID3D11DeviceChild**)&m_pCSEmit, AMD::ShaderCache::SHADER_TYPE_COMPUTE, L"cs_5_0", L"CS_Emit", L"ParticleEmit.hlsl", 0, nullptr, nullptr, nullptr, 0 );

	for ( int i = 0; i < NumStreakModes; i++ )
	{
		for ( int j = 0; j < NumBillboardModes; j++ )
		{
			int numDefines = 0;
			if ( i == StreaksOn )
			{
				wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"STREAKS" );
				numDefines++;
			}

			if ( j == UseGS )
			{
				wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"USE_GEOMETRY_SHADER" );
				numDefines++;
			}

			shadercache.AddShader( (ID3D11DeviceChild**)&m_pVS[ i ][ j ], AMD::ShaderCache::SHADER_TYPE_VERTEX, L"vs_5_0", L"VS_StructuredBuffer", L"ParticleRender.hlsl", numDefines, defines, nullptr, nullptr, 0 );
		}
	}

	for ( int i = 0; i < NumBillboardModes; i++ )
	{
		int numDefines = 0;
		if ( i == UseGS )
		{
			wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"USE_GEOMETRY_SHADER" );
			numDefines++;
		}
		
		shadercache.AddShader( (ID3D11DeviceChild**)&m_pCSSimulate[ i ], AMD::ShaderCache::SHADER_TYPE_COMPUTE, L"cs_5_0", L"CS_Simulate", L"ParticleSimulation.hlsl", numDefines, defines, nullptr, nullptr, 0 );
	}

	shadercache.AddShader( (ID3D11DeviceChild**)&m_pCSResetParticles, AMD::ShaderCache::SHADER_TYPE_COMPUTE, L"cs_5_0", L"CS_Reset", L"ParticleSimulation.hlsl", 0, nullptr, nullptr, nullptr, 0 );
	
	for ( int i = 0; i < NumStreakModes; i++ )
	{
		int numDefines = 0;
		if ( i == StreaksOn )
		{
			wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"STREAKS" );
			numDefines++;
		}

		wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"USE_GEOMETRY_SHADER" );
		numDefines++;
		shadercache.AddShader( (ID3D11DeviceChild**)&m_pGS[ i ], AMD::ShaderCache::SHADER_TYPE_GEOMETRY, L"gs_5_0", L"GS", L"ParticleRender.hlsl", numDefines, defines, nullptr, nullptr, 0 );
		numDefines--;
	
		shadercache.AddShader( (ID3D11DeviceChild**)&m_pRasterizedPS[ FullLighting ][ i ], AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"PS_Billboard", L"ParticleRender.hlsl", numDefines, defines, nullptr, nullptr, 0 );
		
		wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"CHEAP" );
		numDefines++;
		shadercache.AddShader( (ID3D11DeviceChild**)&m_pRasterizedPS[ CheapLighting ][ i ], AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"PS_Billboard", L"ParticleRender.hlsl", numDefines, defines, nullptr, nullptr, 0 );

		wcscpy_s( defines[ numDefines - 1 ].m_wsName, ARRAYSIZE( defines[ numDefines - 1 ].m_wsName ), L"NOLIGHTING" );
		shadercache.AddShader( (ID3D11DeviceChild**)&m_pRasterizedPS[ NoLighting ][ i ], AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"PS_Billboard", L"ParticleRender.hlsl", numDefines, defines, nullptr, nullptr, 0 );
	}

	for ( int j = 0; j < NumQualityModes; j++ )
	{
		for ( int l = 0; l < NumStreakModes; l++ )
		{
			int numDefines = 0;
			if ( j == CheapLighting )
			{
				wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"CHEAP" );
				numDefines++;
			}
			else if ( j == NoLighting )
			{
				wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"NOLIGHTING" );
				numDefines++;
			}

			if ( l == StreaksOn )
			{
				wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"STREAKS" );
				numDefines++;
			}
					
			shadercache.AddShader( (ID3D11DeviceChild**)&m_pTiledRenderingCS[ j ][ l ], AMD::ShaderCache::SHADER_TYPE_COMPUTE, L"cs_5_0", L"FrontToBack", L"TiledRendering.hlsl", numDefines, defines, nullptr, nullptr, 0 );
		}
	}

	for ( int k = 0; k < NumZCullingModes; k++ )
	{
		for ( int l = 0; l < NumCullingModes; l++ )
		{
			for ( int i = 0; i < 2; i++ )
			{
				int numDefines = 0;
				if ( k == CullMaxZ )
				{
					wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"CULLMAXZ" );
					numDefines++;
				}

				if ( l == FrustumCull )
				{
					wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"USE_VIEW_FRUSTUM_PLANES" );
					numDefines++;
				}

				if ( i == 1 )
				{
					wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"COARSE_CULLING_ENABLED" );
					numDefines++;

					int tilesX = g_NumCoarseTiles[ i ][ 0 ];
					int tilesY = g_NumCoarseTiles[ i ][ 1 ];

					wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"NUM_COARSE_CULLING_TILES_X" );
					defines[ numDefines ].m_iValue = tilesX;
					numDefines++;

					wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"NUM_COARSE_CULLING_TILES_Y" );
					defines[ numDefines ].m_iValue = tilesY;
					numDefines++;

					wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"NUM_COARSE_TILES" );
					defines[ numDefines ].m_iValue = tilesX * tilesY;
					numDefines++;
				}
					
				shadercache.AddShader( (ID3D11DeviceChild**)&m_pCullingCS[ k ][ l ][ i ], AMD::ShaderCache::SHADER_TYPE_COMPUTE, L"cs_5_0", L"Culling", L"CullingCS.hlsl", numDefines, defines, nullptr, nullptr, 0 );
					
				
			}
		}
	}

	// Visualization shader
	shadercache.AddShader( (ID3D11DeviceChild**)&m_pTileComplexityCS, AMD::ShaderCache::SHADER_TYPE_COMPUTE, L"cs_5_0", L"Overdraw", L"TiledRendering.hlsl", 0, nullptr, nullptr, nullptr, 0 );
	
	// Coarse culling shaders
	for ( int i = 1; i < NumCoarseCullingModes; i++ )
	{
		int numDefines = 0;
	
		int tilesX = g_NumCoarseTiles[ i ][ 0 ];
		int tilesY = g_NumCoarseTiles[ i ][ 1 ];

		wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"NUM_COARSE_CULLING_TILES_X" );
		defines[ numDefines ].m_iValue = tilesX;
		numDefines++;

		wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"NUM_COARSE_CULLING_TILES_Y" );
		defines[ numDefines ].m_iValue = tilesY;
		numDefines++;

		wcscpy_s( defines[ numDefines ].m_wsName, ARRAYSIZE( defines[ numDefines ].m_wsName ), L"NUM_COARSE_TILES" );
		defines[ numDefines ].m_iValue = tilesX * tilesY;
		numDefines++;

		shadercache.AddShader( (ID3D11DeviceChild**)&m_pCoarseCullingCS[ i ], AMD::ShaderCache::SHADER_TYPE_COMPUTE, L"cs_5_0", L"CoarseCulling", L"CoarseCullingCS.hlsl", numDefines, defines, nullptr, nullptr, 0 );
	}
	
	// Blit shader to write the UAV back onto our current render target
	shadercache.AddShader( (ID3D11DeviceChild**)&m_pQuadVS, AMD::ShaderCache::SHADER_TYPE_VERTEX, L"vs_5_0", L"QuadVS", L"ParticleRenderQuad.hlsl", 0, nullptr, nullptr, nullptr, 0 );
	shadercache.AddShader( (ID3D11DeviceChild**)&m_pQuadPS, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"QuadPS", L"ParticleRenderQuad.hlsl", 0, nullptr, nullptr, nullptr, 0 );
}


GPUParticleSystem::~GPUParticleSystem()
{
	OnReleasingSwapChain();
	OnDestroyDevice();
}


// Use the sort lib to perform a bitonic sort over the particle indices based on their distance from camera
void GPUParticleSystem::Sort()
{
	AMDProfileEvent( AMD_PROFILE_RED, L"Sort" );
	
	m_SortLib.run( g_maxParticles, m_pAliveIndexBufferUAV, m_pActiveListConstantBuffer );
}


// Init the dead list so that all the particles in the system are marked as dead, ready to be spawned.
void GPUParticleSystem::InitDeadList()
{
	m_pImmediateContext->CSSetShader( m_pCSInitDeadList, nullptr, 0 );

	UINT initialCount[] = { 0 };
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, 1, &m_pDeadListUAV, initialCount );

	// Disaptch a set of 1d thread groups to fill out the dead list, one thread per particle
	m_pImmediateContext->Dispatch( align( g_maxParticles, 256 ) / 256, 1, 1 );
	
#if _DEBUG
	m_NumDeadParticlesOnInit = ReadCounter( m_pDeadListUAV );
#endif
}


void GPUParticleSystem::Reset()
{
	m_ResetSystem = true;
}


void GPUParticleSystem::Render( float frameTime, int flags, Technique technique, CoarseCullingMode coarseCullingMode, const EmitterParams* pEmitters, int nNumEmitters, ID3D11ShaderResourceView* depthSRV )
{
	// Save out the previous render target and depth stencil
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	m_pImmediateContext->OMGetRenderTargets( 1, &rtv, &dsv );

	// Unbind current targets while we run the compute stages of the system
	m_pImmediateContext->OMSetRenderTargets( 0, nullptr, nullptr );
	
	// Set the coarse culling level
	m_tilingConstants.numCoarseCullingTilesX = g_NumCoarseTiles[ coarseCullingMode ][ 0 ];
	m_tilingConstants.numCoarseCullingTilesY = g_NumCoarseTiles[ coarseCullingMode ][ 1 ];

	if ( m_tilingConstants.numCoarseCullingTilesX > 0 && m_tilingConstants.numCoarseCullingTilesY )
	{
		m_tilingConstants.numCullingTilesPerCoarseTileX = align( m_tilingConstants.numTilesX, m_tilingConstants.numCoarseCullingTilesX ) / m_tilingConstants.numCoarseCullingTilesX;
		m_tilingConstants.numCullingTilesPerCoarseTileY = align( m_tilingConstants.numTilesY, m_tilingConstants.numCoarseCullingTilesY ) / m_tilingConstants.numCoarseCullingTilesY;
	}

	// Update the tiling constants buffer
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	m_pImmediateContext->Map( m_pTilingConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource );
	memcpy( MappedResource.pData, &m_tilingConstants, sizeof( m_tilingConstants ) );
	m_pImmediateContext->Unmap( m_pTilingConstantBuffer, 0 );

	// If we are resetting the particle system, then initialize the dead list
	if ( m_ResetSystem )
	{
		InitDeadList();
		
		ID3D11UnorderedAccessView* uavs[] = { m_pParticleBufferA_UAV, m_pParticleBufferB_UAV };
		UINT initialCounts[] = { (UINT)-1, (UINT)-1 };
	
		m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, initialCounts );

		m_pImmediateContext->CSSetShader( m_pCSResetParticles, nullptr, 0 );
		m_pImmediateContext->Dispatch( align( g_maxParticles, 256 ) / 256, 1, 1 );
		
		m_ResetSystem = false;
	}
	
	// Emit particles into the system
	Emit( nNumEmitters, pEmitters );

	// Run the simulation for this frame
	Simulate( flags, depthSRV );
	
	// Copy the atomic counter in the alive list UAV into a constant buffer for access by subsequent passes
	m_pImmediateContext->CopyStructureCount( m_pActiveListConstantBuffer, 0, m_pAliveIndexBufferUAV );
		
	// Only read number of alive and dead particle back to the CPU in debug as we don't want to stall the GPU in release code
#if _DEBUG
	m_NumDeadParticlesAfterSimulation = ReadCounter( m_pDeadListUAV );
	m_NumActiveParticlesAfterSimulation = ReadCounter( m_pAliveIndexBufferUAV );
#endif
	
	// Conventional rasterization path
	if ( technique == Technique_Rasterize )
	{
		// Sort if requested. Not doing so results in the particles rendering out of order and not blending correctly
		if ( flags & PF_Sort )
		{
			Sort();
		}

		AMDProfileEvent( AMD_PROFILE_BLUE, L"Render" );

		QualityMode quality = flags & PF_CheapLighting ? CheapLighting : FullLighting;
		if ( flags & PF_NoLighting )
			quality = NoLighting;
		StreakMode streaks = flags & PF_Streaks ? StreaksOn : StreaksOff;
		BillboardMode billboardMode = flags & PF_UseGeometryShader ? UseGS : UseVS;

		// Set up shader stages
		m_pImmediateContext->VSSetShader( m_pVS[ streaks ][ billboardMode ], nullptr, 0 );
		m_pImmediateContext->GSSetShader( billboardMode == UseGS ? m_pGS[ streaks ] : nullptr, nullptr, 0 );
		m_pImmediateContext->PSSetShader( m_pRasterizedPS[ quality ][ streaks ], nullptr, 0 );
	
		ID3D11ShaderResourceView* vs_srv[] = { m_pParticleBufferA_SRV, m_pViewSpaceParticlePositionsSRV, m_pAliveIndexBufferSRV };
		ID3D11ShaderResourceView* ps_srv[] = { depthSRV };
		
		// Set a null vertex buffer
		ID3D11Buffer* vb = nullptr;
		UINT stride = 0;
		UINT offset = 0;
		m_pImmediateContext->IASetVertexBuffers( 0, 1, &vb, &stride, &offset );
		
		m_pImmediateContext->VSSetConstantBuffers( 3, 1, &m_pActiveListConstantBuffer );

		if ( billboardMode == UseGS )
		{
			// Geometry shader path does not need an index buffer. Each vert in the VS is essentially a point primitive 
			// that is expanded to a triangle strip in the GS and reset for each pair of triangles.
			m_pImmediateContext->IASetIndexBuffer( nullptr, DXGI_FORMAT_UNKNOWN, 0 );
			m_pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );
		}
		else
		{
			// Non-GS path is faster but requires an index buffer
			m_pImmediateContext->IASetIndexBuffer( m_pIndexBuffer, DXGI_FORMAT_R32_UINT, 0 );
			m_pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		}
		
		m_pImmediateContext->VSSetShaderResources( 0, ARRAYSIZE( vs_srv ), vs_srv );
		m_pImmediateContext->PSSetShaderResources( 1, ARRAYSIZE( ps_srv ), ps_srv );
		
		// Set the render target up since it was unbound earlier
		m_pImmediateContext->OMSetRenderTargets( 1, &rtv, dsv );
		SAFE_RELEASE( rtv );
		SAFE_RELEASE( dsv );

		// Render the primitives using the DrawInstancedIndirect API. 
		// The indirect args are filled in prior to this call in a compute shader.
		if ( billboardMode == UseGS )
		{
			m_pImmediateContext->DrawInstancedIndirect( m_pIndirectDrawArgsBuffer, 0 );
		}
		else
		{
			m_pImmediateContext->DrawIndexedInstancedIndirect( m_pIndirectDrawArgsBuffer, 0 );
		}
		
		ZeroMemory( vs_srv, sizeof( vs_srv ) );
		m_pImmediateContext->VSSetShaderResources( 0, ARRAYSIZE( vs_srv ), vs_srv );
		ZeroMemory( ps_srv, sizeof( ps_srv ) );
		m_pImmediateContext->PSSetShaderResources( 1, ARRAYSIZE( ps_srv ), ps_srv );
	}
	else
	{	
		// The tiled rendering path

		// Perform coarse culling if requested
		if ( coarseCullingMode != CoarseCullingOff )
		{
			CoarseCulling( coarseCullingMode );
		}

		// Perform fine-grained culling
		CullParticlesIntoTiles( coarseCullingMode, flags, depthSRV );

		{
			// Do the tiled rendering into a UAV
			AMDProfileEvent( AMD_PROFILE_BLUE, L"Render" );
			FillRenderBuffer( flags, depthSRV, technique );

			// Set the render target we want to render to as it was unbound earlier
			m_pImmediateContext->OMSetRenderTargets( 1, &rtv, dsv );
			SAFE_RELEASE( rtv );
			SAFE_RELEASE( dsv );

			// Render a quad that blits the tiled UAV onto the current render target
			RenderQuad();
		}
	}

	// Update the frame's stats. These aren't valid in release as we don't copy the GPU counters back onto the CPU
	m_Stats.m_MaxParticles = g_maxParticles;
	m_Stats.m_NumActiveParticles = m_NumActiveParticlesAfterSimulation;
	m_Stats.m_NumDead = m_NumDeadParticlesAfterSimulation;
}


void GPUParticleSystem::OnCreateDevice( ID3D11Device* pDevice, ID3D11DeviceContext* pImmediateContext )
{
	m_pDevice = pDevice; 
	m_pImmediateContext = pImmediateContext;

	// Create the global particle pool. Each particle is split into two parts for better cache coherency. The first half contains the data more 
	// relevant to rendering while the second half is more related to simulation
	D3D11_BUFFER_DESC desc;
	desc.ByteWidth = sizeof( GPUParticlePartA ) * g_maxParticles;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = sizeof( GPUParticlePartA );

	m_pDevice->CreateBuffer( &desc, nullptr, &m_pParticleBufferA );

	desc.ByteWidth = sizeof( GPUParticlePartB ) * g_maxParticles;
	desc.StructureByteStride = sizeof( GPUParticlePartB );

	m_pDevice->CreateBuffer( &desc, nullptr, &m_pParticleBufferB );

	D3D11_SHADER_RESOURCE_VIEW_DESC srv;
	srv.Format = DXGI_FORMAT_UNKNOWN;
	srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srv.Buffer.ElementOffset = 0;
	srv.Buffer.ElementWidth = g_maxParticles;
	
	m_pDevice->CreateShaderResourceView( m_pParticleBufferA, &srv, &m_pParticleBufferA_SRV );
	
	D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
	uav.Format = DXGI_FORMAT_UNKNOWN;
	uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uav.Buffer.FirstElement = 0;
	uav.Buffer.NumElements = g_maxParticles;
	uav.Buffer.Flags = 0;
	m_pDevice->CreateUnorderedAccessView( m_pParticleBufferA, &uav, &m_pParticleBufferA_UAV );
	m_pDevice->CreateUnorderedAccessView( m_pParticleBufferB, &uav, &m_pParticleBufferB_UAV );

	// The view space positions of particles are cached during simulation so allocate a buffer for them
	desc.ByteWidth = 16 * g_maxParticles;
	desc.StructureByteStride = 16;
	m_pDevice->CreateBuffer( &desc, 0, &m_pViewSpaceParticlePositions );
	m_pDevice->CreateShaderResourceView( m_pViewSpaceParticlePositions, &srv, &m_pViewSpaceParticlePositionsSRV );
	m_pDevice->CreateUnorderedAccessView( m_pViewSpaceParticlePositions, &uav, &m_pViewSpaceParticlePositionsUAV );

	// The maximum radii of each particle is cached during simulation to avoid recomputing multiple times later. This is only required
	// for streaked particles as they are not round so we cache the max radius of X and Y
	desc.ByteWidth = 4 * g_maxParticles;
	desc.StructureByteStride = 4;
	m_pDevice->CreateBuffer( &desc, 0, &m_pMaxRadiusBuffer );
	m_pDevice->CreateShaderResourceView( m_pMaxRadiusBuffer, &srv, &m_pMaxRadiusBufferSRV );
	m_pDevice->CreateUnorderedAccessView( m_pMaxRadiusBuffer, &uav, &m_pMaxRadiusBufferUAV );
	
	// The dead particle index list. Created as an append buffer
	desc.ByteWidth = sizeof( UINT ) * g_maxParticles;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = sizeof( UINT );

	m_pDevice->CreateBuffer( &desc, nullptr, &m_pDeadListBuffer );

	uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
	m_pDevice->CreateUnorderedAccessView( m_pDeadListBuffer, &uav, &m_pDeadListUAV );
	
	// Create the coarse culling buffer. This is an index buffer that allocates the maximum number of particles for each coarse bin
	desc.StructureByteStride = 0;
	desc.MiscFlags = 0;
	desc.ByteWidth = sizeof( UINT ) * g_maxParticles * g_maxCoarseCullingTiles;
	m_pDevice->CreateBuffer( &desc, nullptr, &m_pStridedCoarseCullingBuffer );

	uav.Format = DXGI_FORMAT_R32_UINT;
	uav.Buffer.Flags = 0;
	uav.Buffer.NumElements = g_maxParticles * g_maxCoarseCullingTiles;
	m_pDevice->CreateUnorderedAccessView( m_pStridedCoarseCullingBuffer, &uav, &m_pStridedCoarseCullingBufferUAV );

	srv.Format = DXGI_FORMAT_R32_UINT;
	srv.Buffer.NumElements = g_maxParticles * g_maxCoarseCullingTiles;
	m_pDevice->CreateShaderResourceView( m_pStridedCoarseCullingBuffer, &srv, &m_pStridedCoarseCullingBufferSRV );

	// In addition to the index buffer for the coarse culling, we also need to track how many particles are in each bin, 
	// therefore we allocate one element per bin which is atomically incremented as each particle is added
	desc.ByteWidth = sizeof( UINT ) * g_maxCoarseCullingTiles;
	m_pDevice->CreateBuffer( &desc, nullptr, &m_pStridedCoarseCullingBufferCounters );

	uav.Buffer.NumElements = g_maxCoarseCullingTiles;
	m_pDevice->CreateUnorderedAccessView( m_pStridedCoarseCullingBufferCounters, &uav, &m_pStridedCoarseCullingBufferCountersUAV );

	srv.Buffer.NumElements = g_maxCoarseCullingTiles;
	m_pDevice->CreateShaderResourceView( m_pStridedCoarseCullingBufferCounters, &srv, &m_pStridedCoarseCullingBufferCountersSRV );


	// Create a staging buffer that is used to read GPU atomic counter into that can then be mapped for reading 
	// back to the CPU for debugging purposes
#if _DEBUG
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.ByteWidth = sizeof( UINT );
	m_pDevice->CreateBuffer( &desc, nullptr, &m_pDebugCounterBuffer );
#endif

	// Create constant buffers to copy the dead and alive list counters into
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.ByteWidth = 4 * sizeof( UINT );
	m_pDevice->CreateBuffer( &desc, nullptr, &m_pDeadListConstantBuffer );
	m_pDevice->CreateBuffer( &desc, nullptr, &m_pActiveListConstantBuffer );

	// Create the emitter constant buffer
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.ByteWidth = sizeof( EmitterConstantBuffer );
	m_pDevice->CreateBuffer( &desc, nullptr, &m_pEmitterConstantBuffer );

	// Create the tiling constant buffer
	desc.ByteWidth = sizeof( m_tilingConstants );
	m_pDevice->CreateBuffer( &desc, nullptr, &m_pTilingConstantBuffer );
	
	struct IndexBufferElement
	{
		float		distance;	// distance squared from the particle to the camera
		float		index;		// global index of the particle
	};

	// Create the index buffer of alive particles that is to be sorted (at least in the rasterization path).
	// For the tiled rendering path this could be just a UINT index buffer as particles are not globally sorted
	desc.ByteWidth = sizeof( IndexBufferElement ) * g_maxParticles;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = sizeof( IndexBufferElement );

	m_pDevice->CreateBuffer( &desc, nullptr, &m_pAliveIndexBuffer );

	srv.Format = DXGI_FORMAT_UNKNOWN;
	srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srv.Buffer.ElementOffset = 0;
	srv.Buffer.ElementWidth = g_maxParticles;
	
	m_pDevice->CreateShaderResourceView( m_pAliveIndexBuffer, &srv, &m_pAliveIndexBufferSRV );

	uav.Buffer.NumElements = g_maxParticles;
	uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;
	uav.Format = DXGI_FORMAT_UNKNOWN;
	m_pDevice->CreateUnorderedAccessView( m_pAliveIndexBuffer, &uav, &m_pAliveIndexBufferUAV );

	// Create the buffer to store the indirect args for the DrawInstancedIndirect call
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	desc.ByteWidth = 5 * sizeof( UINT );
	desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
	m_pDevice->CreateBuffer( &desc, nullptr, &m_pIndirectDrawArgsBuffer );
	
	ZeroMemory( &uav, sizeof( uav ) );
	uav.Format = DXGI_FORMAT_R32_UINT;
	uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uav.Buffer.FirstElement = 0;
	uav.Buffer.NumElements = 5;
	uav.Buffer.Flags = 0;
	m_pDevice->CreateUnorderedAccessView( m_pIndirectDrawArgsBuffer, &uav, &m_pIndirectDrawArgsBufferUAV );
	
	// Create the particle billboard index buffer required for the rasterization VS-only path
	ZeroMemory( &desc, sizeof( desc ) );
	desc.ByteWidth = g_maxParticles * 6 * sizeof( UINT );
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA data;

	UINT* indices = new UINT[ g_maxParticles * 6 ];
	data.pSysMem = indices;
	data.SysMemPitch = 0;
	data.SysMemSlicePitch = 0;

	UINT base = 0;
	for ( int i = 0; i < g_maxParticles; i++ )
	{
		indices[ 0 ] = base + 0;
		indices[ 1 ] = base + 1;
		indices[ 2 ] = base + 2;

		indices[ 3 ] = base + 2;
		indices[ 4 ] = base + 1;
		indices[ 5 ] = base + 3;

		base += 4;
		indices += 6;
	}

	m_pDevice->CreateBuffer( &desc, &data, &m_pIndexBuffer );

	delete[] data.pSysMem;

	// Create a blend state for compositing the particles onto the render target
	D3D11_BLEND_DESC blendDesc;
	ZeroMemory(&blendDesc, sizeof(D3D11_BLEND_DESC));
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.IndependentBlendEnable = false;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = true;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	m_pDevice->CreateBlendState( &blendDesc, &m_pCompositeBlendState );

	// Create the SortLib resources
	m_SortLib.init( m_pDevice, m_pImmediateContext );

	// Initialize the random numbers texture
	FillRandomTexture();
}


void GPUParticleSystem::OnResizedSwapChain( const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc )
{
	HRESULT hr;
	m_uWidth = pBackBufferSurfaceDesc->Width;
	m_uHeight = pBackBufferSurfaceDesc->Height;

	// Ensure the numbers of tiles are sufficient to cover the screen
	m_tilingConstants.numTilesX = align( m_uWidth, TILE_RES_X ) / TILE_RES_X;
	m_tilingConstants.numTilesY = align( m_uHeight, TILE_RES_Y ) / TILE_RES_Y;

	unsigned int uNumRenderingTiles = m_tilingConstants.numTilesX * m_tilingConstants.numTilesY;
 
	// Allocate the tiled rendering buffer as RGBA16F
	D3D11_BUFFER_DESC BufferDesc;
	ZeroMemory( &BufferDesc, sizeof(BufferDesc) );
	int numPixels = uNumRenderingTiles * TILE_RES_X * TILE_RES_Y;
	BufferDesc.ByteWidth = 8 * numPixels;
	BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	V( m_pDevice->CreateBuffer( &BufferDesc, nullptr, &m_pRenderingBuffer ) );
	DXUT_SetDebugName( m_pRenderingBuffer, "RenderingBuffer" );

	DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc;
	ZeroMemory( &SRVDesc, sizeof( SRVDesc ) );
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.ElementOffset = 0;
	SRVDesc.Format = format;
	SRVDesc.Buffer.ElementWidth = numPixels;
	V( m_pDevice->CreateShaderResourceView( m_pRenderingBuffer, &SRVDesc, &m_pRenderingBufferSRV ) );

	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc;
	ZeroMemory( &UAVDesc, sizeof( UAVDesc ) );
	UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.FirstElement = 0;
	UAVDesc.Format = format;
	UAVDesc.Buffer.NumElements = numPixels;
	V( m_pDevice->CreateUnorderedAccessView( m_pRenderingBuffer, &UAVDesc, &m_pRenderingBufferUAV ) );

	unsigned int uNumCullingTiles = m_tilingConstants.numTilesX * m_tilingConstants.numTilesY;

	// Allocate the tiled culling index buffer (for fine-grained culling)
	ZeroMemory( &BufferDesc, sizeof(BufferDesc) );
	int numElements = uNumCullingTiles * PARTICLES_TILE_BUFFER_SIZE;
	BufferDesc.ByteWidth = 4 * numElements;
	BufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	V( m_pDevice->CreateBuffer( &BufferDesc, nullptr, &m_pTiledIndexBuffer ) );
	
	ZeroMemory( &UAVDesc, sizeof( UAVDesc ) );
	UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.FirstElement = 0;
	UAVDesc.Format = DXGI_FORMAT_R32_UINT;
	UAVDesc.Buffer.NumElements = numElements;
	V( m_pDevice->CreateUnorderedAccessView( m_pTiledIndexBuffer, &UAVDesc, &m_pTiledIndexBufferUAV ) );

	ZeroMemory( &SRVDesc, sizeof( SRVDesc ) );
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.ElementOffset = 0;
	SRVDesc.Format = DXGI_FORMAT_R32_UINT;
	SRVDesc.Buffer.ElementWidth = numElements;
	V( m_pDevice->CreateShaderResourceView( m_pTiledIndexBuffer, &SRVDesc, &m_pTiledIndexBufferSRV ) );
}


void GPUParticleSystem::OnReleasingSwapChain()
{
	SAFE_RELEASE( m_pRenderingBufferUAV );
	SAFE_RELEASE( m_pRenderingBufferSRV );
	SAFE_RELEASE( m_pRenderingBuffer );

	SAFE_RELEASE( m_pTiledIndexBufferUAV );
	SAFE_RELEASE( m_pTiledIndexBufferSRV );
	SAFE_RELEASE( m_pTiledIndexBuffer );
}


void GPUParticleSystem::OnDestroyDevice()
{
	m_pImmediateContext = nullptr;
	m_pDevice = nullptr;

	SAFE_RELEASE( m_pIndexBuffer );

	SAFE_RELEASE( m_pIndirectDrawArgsBufferUAV );
	SAFE_RELEASE( m_pIndirectDrawArgsBuffer );

	SAFE_RELEASE( m_pRandomTextureSRV );
	SAFE_RELEASE( m_pRandomTexture );

	SAFE_RELEASE( m_pActiveListConstantBuffer );
	SAFE_RELEASE( m_pDeadListConstantBuffer );

#if _DEBUG
	SAFE_RELEASE( m_pDebugCounterBuffer );
#endif	

	SAFE_RELEASE( m_pAliveIndexBufferUAV );
	SAFE_RELEASE( m_pAliveIndexBufferSRV );
	SAFE_RELEASE( m_pAliveIndexBuffer );

	SAFE_RELEASE( m_pDeadListUAV );
	SAFE_RELEASE( m_pDeadListBuffer );

	SAFE_RELEASE( m_pStridedCoarseCullingBufferUAV );
	SAFE_RELEASE( m_pStridedCoarseCullingBufferSRV );
	SAFE_RELEASE( m_pStridedCoarseCullingBuffer );

	SAFE_RELEASE( m_pStridedCoarseCullingBufferCountersUAV );
	SAFE_RELEASE( m_pStridedCoarseCullingBufferCountersSRV );
	SAFE_RELEASE( m_pStridedCoarseCullingBufferCounters );

	SAFE_RELEASE( m_pMaxRadiusBufferUAV );
	SAFE_RELEASE( m_pMaxRadiusBufferSRV );
	SAFE_RELEASE( m_pMaxRadiusBuffer );

	SAFE_RELEASE( m_pViewSpaceParticlePositionsUAV );
	SAFE_RELEASE( m_pViewSpaceParticlePositionsSRV );
	SAFE_RELEASE( m_pViewSpaceParticlePositions );

	SAFE_RELEASE( m_pParticleBufferB_UAV );
	SAFE_RELEASE( m_pParticleBufferB );

	SAFE_RELEASE( m_pParticleBufferA_UAV );
	SAFE_RELEASE( m_pParticleBufferA_SRV );
	SAFE_RELEASE( m_pParticleBufferA );
	
	SAFE_RELEASE( m_pQuadPS );
	SAFE_RELEASE( m_pQuadVS );

	for ( int j = 0; j < NumQualityModes; j++ )
	{
		for ( int k = 0; k < NumStreakModes; k++ )
		{
			SAFE_RELEASE( m_pRasterizedPS[ j ][ k ] );
		}
	}
	for ( int k = 0; k < ARRAYSIZE( m_pGS ); k++ )
	{
		SAFE_RELEASE( m_pGS[ k ] );
	}

	for ( int i = 0; i < NumStreakModes; i++ )
	{
		for ( int j = 0; j < NumBillboardModes; j++ )
		{
			SAFE_RELEASE( m_pVS[ i ][ j ] );
		}
	}

	for ( int i = 0; i < NumBillboardModes; i++ )
	{
		SAFE_RELEASE( m_pCSSimulate[ i ] );
	}

	SAFE_RELEASE( m_pCSResetParticles );
	SAFE_RELEASE( m_pCSInitDeadList );
	SAFE_RELEASE( m_pCSEmit );

	for ( int i = 0; i < NumCoarseCullingModes; i++ )
	{
		SAFE_RELEASE( m_pCoarseCullingCS[ i ] );
	}

	SAFE_RELEASE( m_pTileComplexityCS );
	
	for ( int j = 0; j < NumQualityModes; j++ )
	{
		for ( int l = 0; l < NumStreakModes; l++ )
		{
			SAFE_RELEASE( m_pTiledRenderingCS[ j ][ l ] );
		}
	}

	for ( int k = 0; k < NumZCullingModes; k++ )
	{
		for ( int l = 0; l < NumCullingModes; l++ )
		{
			for ( int i = 0; i < 2; i++ )
			{
				SAFE_RELEASE( m_pCullingCS[ k ][ l ][ i ] );
			}
		}
	}
	
	SAFE_RELEASE( m_pEmitterConstantBuffer );
	SAFE_RELEASE( m_pTilingConstantBuffer );

	SAFE_RELEASE( m_pCompositeBlendState );
	
	m_SortLib.release();

	m_ResetSystem = true;
}


// Per-frame emission of particles into the GPU simulation
void GPUParticleSystem::Emit( int numEmitters, const EmitterParams* emitters )
{
	AMDProfileEvent( AMD_PROFILE_GREEN, L"Emission" );
	
	// Set resources but don't reset any atomic counters
	ID3D11UnorderedAccessView* uavs[] = { m_pParticleBufferA_UAV, m_pParticleBufferB_UAV, m_pDeadListUAV };
	UINT initialCounts[] = { (UINT)-1, (UINT)-1, (UINT)-1 };
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, initialCounts );

	ID3D11Buffer* buffers[] = { m_pEmitterConstantBuffer, m_pDeadListConstantBuffer };
	m_pImmediateContext->CSSetConstantBuffers( 1, ARRAYSIZE( buffers ), buffers );

	ID3D11ShaderResourceView* srvs[] = { m_pRandomTextureSRV };
	m_pImmediateContext->CSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );
	
	m_pImmediateContext->CSSetShader( m_pCSEmit, nullptr, 0 );

	// Run CS for each emitter
	for ( int i = 0; i < numEmitters; i++ )
	{
		const EmitterParams& emitter = emitters[ i ];
	
		if ( emitter.m_NumToEmit > 0 )
		{	
			// Update the emitter constant buffer
			D3D11_MAPPED_SUBRESOURCE MappedResource;
			m_pImmediateContext->Map( m_pEmitterConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource );
			EmitterConstantBuffer* constants = (EmitterConstantBuffer*)MappedResource.pData;
			constants->m_EmitterPosition = emitter.m_Position;
			constants->m_EmitterVelocity = emitter.m_Velocity;
			constants->m_MaxParticlesThisFrame = emitter.m_NumToEmit;
			constants->m_ParticleLifeSpan = emitter.m_ParticleLifeSpan;
			constants->m_StartSize = emitter.m_StartSize;
			constants->m_EndSize = emitter.m_EndSize;
			constants->m_PositionVariance = emitter.m_PositionVariance;
			constants->m_VelocityVariance = emitter.m_VelocityVariance;
			constants->m_Mass = emitter.m_Mass;
			constants->m_Index = i;
			constants->m_Streaks = emitter.m_Streaks ? 1 : 0;
			constants->m_TextureIndex = emitter.m_TextureIndex;
			m_pImmediateContext->Unmap( m_pEmitterConstantBuffer, 0 );
		
			// Copy the current number of dead particles into a CB so we know how many new particles are available to be spawned
			m_pImmediateContext->CopyStructureCount( m_pDeadListConstantBuffer, 0, m_pDeadListUAV );
		
			// Dispatch enough thread groups to spawn the requested particles
			int numThreadGroups = align( emitter.m_NumToEmit, 1024 ) / 1024;
			m_pImmediateContext->Dispatch( numThreadGroups, 1, 1 );
		}
	}

#if _DEBUG
	m_NumDeadParticlesAfterEmit = ReadCounter( m_pDeadListUAV );
#endif
}


// Per-frame simulation step
void GPUParticleSystem::Simulate( int flags, ID3D11ShaderResourceView* depthSRV )
{
	AMDProfileEvent( AMD_PROFILE_GREEN, L"Simulation" );

	// Set the UAVs and reset the alive index buffer's counter
	ID3D11UnorderedAccessView* uavs[] = { m_pParticleBufferA_UAV, m_pParticleBufferB_UAV, m_pDeadListUAV, m_pAliveIndexBufferUAV, m_pViewSpaceParticlePositionsUAV, m_pMaxRadiusBufferUAV, m_pIndirectDrawArgsBufferUAV };
	UINT initialCounts[] = { (UINT)-1, (UINT)-1, (UINT)-1, 0, (UINT)-1, (UINT)-1, (UINT)-1 };
	
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, initialCounts );
	
	// Bind the depth buffer as a texture for doing collision detection and response
	ID3D11ShaderResourceView* srvs[] = { depthSRV };
	m_pImmediateContext->CSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );

	// Pick the correct CS based on the system's options
	BillboardMode billboardMode = flags & PF_UseGeometryShader ? UseGS : UseVS;
	
	// Dispatch enough thread groups to update all the particles
	m_pImmediateContext->CSSetShader( m_pCSSimulate[ billboardMode ], nullptr, 0 );
	m_pImmediateContext->Dispatch( align( g_maxParticles, 256 ) / 256, 1, 1 );

	ZeroMemory( srvs, sizeof( srvs ) );
	m_pImmediateContext->CSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );

	ZeroMemory( uavs, sizeof( uavs ) );
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, nullptr );
}


// Helper function to read atomic UAV counters back onto the CPU. This will cause a stall so only use in debug
#if _DEBUG
int	GPUParticleSystem::ReadCounter( ID3D11UnorderedAccessView* uav )
{
	int count = 0;

	// Copy the UAV counter to a staging resource
	m_pImmediateContext->CopyStructureCount( m_pDebugCounterBuffer, 0, uav );
	
	// Map the staging resource
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	m_pImmediateContext->Map( m_pDebugCounterBuffer, 0, D3D11_MAP_READ, 0, &MappedResource );
	
	// Read the data
	count = *(int*)MappedResource.pData;

	m_pImmediateContext->Unmap( m_pDebugCounterBuffer, 0 );

	return count;
}
#endif


// Cull the particles into coarse bins to dramatically improve performance
void GPUParticleSystem::CoarseCulling( CoarseCullingMode coarseCullingMode )
{
	AMDProfileEvent( AMD_PROFILE_BLUE, L"CoarseCulling" );

	// Set the UAVs - first one is the index buffer that is divided into n bins. Second is the per-bin counters that keep track of the number of particles in each bin
	UINT initialCounts[] = { (UINT)-1, (UINT)-1 };
	ID3D11UnorderedAccessView* uavs[] = { m_pStridedCoarseCullingBufferUAV, m_pStridedCoarseCullingBufferCountersUAV };
	
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, initialCounts );
	
	ID3D11ShaderResourceView* srvs[] = { m_pViewSpaceParticlePositionsSRV, m_pMaxRadiusBufferSRV, m_pAliveIndexBufferSRV,  };
	m_pImmediateContext->CSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );
	
	m_pImmediateContext->CSSetConstantBuffers( 3, 1, &m_pActiveListConstantBuffer );

	m_pImmediateContext->CSSetShader( m_pCoarseCullingCS[ coarseCullingMode ], nullptr, 0 );
	m_pImmediateContext->Dispatch( align( g_maxParticles, COARSE_CULLING_THREADS ) / COARSE_CULLING_THREADS, 1, 1 );		// Could use DispatchIndirect based on number of alive particles
	
	ZeroMemory( uavs, sizeof( uavs ) );
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, nullptr );

	ZeroMemory( srvs, sizeof( srvs ) );
	m_pImmediateContext->CSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );
}


// Perform fine-grained culling. The culling tile size matches the tile size that we will be rendering with
void GPUParticleSystem::CullParticlesIntoTiles( CoarseCullingMode coarseCullingMode, int flags, ID3D11ShaderResourceView* depthSRV )
{
	AMDProfileEvent( AMD_PROFILE_BLUE, L"Culling" );

	// Set the UAV we are going to write to
	UINT initialCounts[] = { (UINT)-1 };
	ID3D11UnorderedAccessView* uavs[] = { m_pTiledIndexBufferUAV };
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, initialCounts );
	
	// Set the CS inputs
	ID3D11ShaderResourceView* srvs[] = { m_pViewSpaceParticlePositionsSRV, m_pMaxRadiusBufferSRV, m_pAliveIndexBufferSRV, depthSRV, m_pStridedCoarseCullingBufferSRV, m_pStridedCoarseCullingBufferCountersSRV };
	m_pImmediateContext->CSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );
	
	m_pImmediateContext->CSSetConstantBuffers( 3, 1, &m_pActiveListConstantBuffer );
	m_pImmediateContext->CSSetConstantBuffers( 5, 1, &m_pTilingConstantBuffer );
		
	// Pick the right shader based on the system options
	ZCullingMode zculling = flags & PF_CullMaxZ ? CullMaxZ : NoZCulling;
	CullingMode culling = flags & PF_ScreenSpaceCulling ? ScreenspaceCull : FrustumCull;

	m_pImmediateContext->CSSetShader( m_pCullingCS[ zculling ][ culling ][ coarseCullingMode == CoarseCullingOff ? 0 : 1 ], nullptr, 0 );

	// Dispatch a thread group per tile
	m_pImmediateContext->Dispatch( m_tilingConstants.numTilesX, m_tilingConstants.numTilesY, 1 );
		
	ZeroMemory( uavs, sizeof( uavs ) );
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, nullptr );

	ZeroMemory( srvs, sizeof( srvs ) );
	m_pImmediateContext->CSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );
}


// Do the tiled rendering using a compute shader
void GPUParticleSystem::FillRenderBuffer( int flags, ID3D11ShaderResourceView* depthSRV, Technique technique )
{
	// Set the UAV that we will write the shaded particle pixels to
	UINT initialCounts[] = { (UINT)-1 };
	ID3D11UnorderedAccessView* uavs[] = { m_pRenderingBufferUAV };
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, initialCounts );
	
	// Set the shader inputs. Note that the coarse culling buffer isn't required for tiled rendering, but we pass it through for the debug visualization 
	ID3D11ShaderResourceView* srvs[] = { m_pParticleBufferA_SRV, m_pViewSpaceParticlePositionsSRV, depthSRV, m_pTiledIndexBufferSRV, m_pStridedCoarseCullingBufferCountersSRV };
	m_pImmediateContext->CSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );
	
	m_pImmediateContext->CSSetConstantBuffers( 3, 1, &m_pActiveListConstantBuffer );
	m_pImmediateContext->CSSetConstantBuffers( 5, 1, &m_pTilingConstantBuffer );
	
	// Select the shader based on the options
	QualityMode quality = flags & PF_CheapLighting ? CheapLighting : FullLighting;
	if ( flags & PF_NoLighting )
		quality = NoLighting;
	StreakMode streaks = flags & PF_Streaks ? StreaksOn : StreaksOff;
	
	ID3D11ComputeShader* shader = nullptr;
	switch ( technique )
	{
		case Technique_Overdraw: shader = m_pTileComplexityCS; break;
		case Technique_Tiled: shader = m_pTiledRenderingCS[ quality ][ streaks ]; break;
	}

	m_pImmediateContext->CSSetShader( shader, nullptr, 0 );
	
	// Dispatch a thread group per tile
	m_pImmediateContext->Dispatch( m_tilingConstants.numTilesX, m_tilingConstants.numTilesY, 1 );
	m_pImmediateContext->CSSetShader( nullptr, nullptr, 0 );

	ZeroMemory( uavs, sizeof( uavs ) );
	m_pImmediateContext->CSSetUnorderedAccessViews( 0, ARRAYSIZE( uavs ), uavs, nullptr );

	ZeroMemory( srvs, sizeof( srvs ) );
	m_pImmediateContext->CSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );
}


// Function to write the UAV back on to the scene render target
void GPUParticleSystem::RenderQuad()
{
	AMDProfileEvent( AMD_PROFILE_BLUE, L"RenderQuad" );
	
	// Set the blend state to do compositing
	m_pImmediateContext->OMSetBlendState( m_pCompositeBlendState, nullptr, 0xffffffff );

	// Set the quad shader
	m_pImmediateContext->VSSetShader( m_pQuadVS, nullptr, 0 );
	m_pImmediateContext->PSSetShader( m_pQuadPS, nullptr, 0 );

	// No vertex buffer or index buffer required. Just use vertexId to generate triangles
	m_pImmediateContext->IASetIndexBuffer( nullptr, DXGI_FORMAT_UNKNOWN, 0 );
	m_pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	// Bind the tiled UAV to the pixel shader
	ID3D11ShaderResourceView* srvs[] = { m_pRenderingBufferSRV };
	m_pImmediateContext->PSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );

	// Draw one large triangle
	m_pImmediateContext->Draw( 3, 0 );
	
	ZeroMemory( srvs, sizeof( srvs ) );
	m_pImmediateContext->PSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );

	// Restore the default blend state
	m_pImmediateContext->OMSetBlendState( nullptr, nullptr, 0xffffffff );
}


// Populate a texture with random numbers (used for the emission of particles)
void GPUParticleSystem::FillRandomTexture()
{
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Width = 1024;
	desc.Height = 1024;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	
	float* values = new float[ desc.Width * desc.Height * 4 ];
	float* ptr = values;
	for ( UINT i = 0; i < desc.Width * desc.Height; i++ )
	{
		ptr[ 0 ] = RandomVariance( 0.0f, 1.0f );
		ptr[ 1 ] = RandomVariance( 0.0f, 1.0f );
		ptr[ 2 ] = RandomVariance( 0.0f, 1.0f );
		ptr[ 3 ] = RandomVariance( 0.0f, 1.0f );
		ptr += 4;
	}

	D3D11_SUBRESOURCE_DATA data;
	data.pSysMem = values;
	data.SysMemPitch = desc.Width * 16;
	data.SysMemSlicePitch = 0; 

	m_pDevice->CreateTexture2D( &desc, &data, &m_pRandomTexture );
		
	delete[] values;

	D3D11_SHADER_RESOURCE_VIEW_DESC srv;
	srv.Format = desc.Format;
	srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	srv.Texture2D.MostDetailedMip = 0;
	
	m_pDevice->CreateShaderResourceView( m_pRandomTexture, &srv, &m_pRandomTextureSRV );
}