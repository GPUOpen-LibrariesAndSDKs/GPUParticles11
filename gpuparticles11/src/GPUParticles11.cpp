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
#include "..\\..\\DXUT\\Core\\DXUT.h"
#include "..\\..\\DXUT\\Core\\DXUTmisc.h"
#include "..\\..\\DXUT\\Core\\DDSTextureLoader.h"
#include "..\\..\\DXUT\\Optional\\DXUTgui.h"
#include "..\\..\\DXUT\\Optional\\DXUTCamera.h"
#include "..\\..\\DXUT\\Optional\\DXUTSettingsDlg.h"
#include "..\\..\\DXUT\\Optional\\SDKmisc.h"
#include "..\\..\\DXUT\\Optional\\SDKmesh.h"

#include "..\\..\\AMD_SDK\\inc\\AMD_SDK.h"
#include "..\\..\\AMD_SDK\\inc\\ShaderCacheSampleHelper.h"

#include "resource.h"
#include "ParticleSystem.h"
#include "ParticleHelpers.h"
#include "Terrain.h"
#include "Shaders/ShaderConstants.h"

#pragma warning( disable : 4100 ) // disable unreference formal parameter warnings for /W4 builds

// Parameters that only change ONCE per frame
struct PER_FRAME_CONSTANT_BUFFER
{
	DirectX::XMVECTOR	m_StartColor[ NUM_EMITTERS ];
	DirectX::XMVECTOR	m_EndColor[ NUM_EMITTERS ];
	DirectX::XMVECTOR	m_EmitterLightingCenter[ NUM_EMITTERS ];

	DirectX::XMMATRIX	m_ViewProjection;
	DirectX::XMMATRIX	m_ViewProjInv;
	DirectX::XMMATRIX	m_View;
	DirectX::XMMATRIX	m_ViewInv;
	DirectX::XMMATRIX	m_Projection;
	DirectX::XMMATRIX	m_ProjectionInv;
	
	DirectX::XMVECTOR	m_EyePosition;
	DirectX::XMVECTOR	m_SunDirection;
	DirectX::XMVECTOR	m_SunColor;
	DirectX::XMVECTOR	m_AmbientColor;

	DirectX::XMVECTOR	m_SunDirectionVS;
	DirectX::XMVECTOR	pads2[ 3 ];

	float				m_FrameTime;
	int					m_ScreenWidth;
	int					m_ScreenHeight;
	int					m_FrameIndex;
	
	float				m_AlphaThreshold;
	float				m_CollisionThickness;
	float				m_ElapsedTime;
	int					m_CollisionsEnabled;

	int					m_ShowSleepingParticles;
	int					m_EnableSleepState;
	int					pads[ 2 ];
	
};

//--------------------------------------------------------------------------------------
// Global variables
//--------------------------------------------------------------------------------------
CFirstPersonCamera						g_Camera;					// A model viewing camera
CDXUTDialogResourceManager				g_DialogResourceManager;	// manager for shared resources of dialogs
CD3DSettingsDlg							g_SettingsDlg;				// Device settings dialog
CDXUTTextHelper*						g_pTxtHelper = nullptr;

// Terrain resources
ID3D11VertexShader*						g_pTerrainVS = nullptr;
ID3D11PixelShader*						g_pTerrainPS = nullptr;
ID3D11InputLayout*						g_pTerrainLayout = nullptr;
CTerrain								g_Terrain;

// Tank resources
ID3D11VertexShader*						g_pTankVS = nullptr;
ID3D11PixelShader*						g_pTankPS = nullptr;
ID3D11InputLayout*						g_pTankVertexLayout = nullptr;
CDXUTSDKMesh							g_TankMesh;

ID3D11PixelShader*						g_pSkyPS = nullptr;
CDXUTSDKMesh							g_SkyMesh;

// Renderer states 
ID3D11BlendState*						g_pAlphaState = nullptr;
ID3D11BlendState*						g_pOpaqueState = nullptr;
ID3D11RasterizerState*					g_pRasterState = nullptr;
ID3D11DepthStencilState*				g_pDepthWriteState = nullptr;
ID3D11DepthStencilState*				g_pDepthTestState = nullptr;

// Sampler states
ID3D11SamplerState*						g_pSamWrapLinear = nullptr;		// Only need two samplers: wrap and clamp
ID3D11SamplerState*						g_pSamClampLinear = nullptr;

// Depth stencil surface for our main scene render target
ID3D11Texture2D*						g_pDepthStencilTexture = nullptr;
ID3D11DepthStencilView*					g_pDepthStencilView = nullptr;
ID3D11ShaderResourceView*				g_pDepthStencilSRV = nullptr;

// Render target for the main scene (we don't render directly to the backbuffer)
ID3D11Texture2D*						g_RenderTargetTexture = nullptr;
ID3D11RenderTargetView*					g_RenderTargetRTV = nullptr;
ID3D11ShaderResourceView*				g_RenderTargetSRV = nullptr;

// Quad shader to blit the redner target above to the backbuffer
ID3D11VertexShader*						g_pQuadVS = nullptr;
ID3D11PixelShader*						g_pQuadPS = nullptr;

// Constant buffers
ID3D11Buffer*							g_pPerFrameConstantBuffer = nullptr;
PER_FRAME_CONSTANT_BUFFER				g_GlobalConstantBuffer;

// The particle system itself
IParticleSystem*						g_pGPUParticleSystem = nullptr;

// The texture atlas for the particles
ID3D11ShaderResourceView*				g_pTextureAtlas = nullptr;

// Misc
int										g_ScreenWidth = 0;
int										g_ScreenHeight = 0;



enum SceneType
{
	Volcano,
	Tank,
	NumScenes
};

const wchar_t* gSceneNames[ NumScenes ] = 
{ 
	L"Volcano", 
	L"Tank"
};

SceneType								g_Scene = Volcano;
CDXUTComboBox*							g_SceneCombo = nullptr;

struct EmissionRate
{
	float		m_ParticlesPerSecond;	// Number of particles to emit per second
	float		m_Accumulation;			// Running total of how many particles to emit over elapsed time
};

IParticleSystem::EmitterParams			g_VolcanoSparksEmitter;
IParticleSystem::EmitterParams			g_VolcanoSmokeEmitter;

IParticleSystem::EmitterParams			g_TankSmokeEmitters[ 2 ];
EmissionRate							g_EmissionRates[ NUM_EMITTERS ];

IParticleSystem::EmitterParams			g_CollisionTestEmitter;
bool									g_SpawnCollisionTestParticles = false;

struct CameraPosition
{
	DirectX::XMVECTOR		m_Position;
	DirectX::XMVECTOR		m_LookAt;
	const wchar_t*			m_Name;
};

// Pre-define some camera positions for benchmarking
const CameraPosition gCameraPositionsVolcano[] = 
{
	{
		DirectX::XMVectorSet( -48.81f, 13.00f, -134.0f, 1.0f ),
		DirectX::XMVectorSet( -48.46f, 13.70f, -133.0f, 1.0f ),
		L"default"
	},
	{
		DirectX::XMVectorSet( -17.21f,  99.73f, 28.60f, 1.0f ),
		DirectX::XMVectorSet( -16.70f, 100.59f, 28.51f, 1.0f ),
		L"in smoke"
	},
	{
		DirectX::XMVectorSet( -215.65f, -2.65f, -290.58f, 1.0f ),
		DirectX::XMVectorSet( -215.09f, -2.22f, -289.87f, 1.0f ),
		L"distant"
	}
};

const CameraPosition gCameraPositionsTank[] = 
{
	{
		DirectX::XMVectorSet( 4.0f, 0.2f, -11.0f, 1.0f ),
		DirectX::XMVectorSet( 1.0f, 1.0f, 0.0f, 1.0f ),
		L"default"
	},
	{
		DirectX::XMVectorSet( -1.0f, 0.57f, -1.0f, 1.0f ),
		DirectX::XMVectorSet( -0.27f, 0.68f, 0.44f, 1.0f ),
		L"close to tank"
	}
};


const CameraPosition*			g_CurrentCameraPositions = nullptr;
int								g_CurrentCameraNumPositions = 0;


const wchar_t* gTechniqueNames[ IParticleSystem::Technique_Max ] = 
{ 
	L"Rasterization", 
	L"Tiled",
	L"Tile Complexity" 
};


enum RenderTargetFormat
{
	RT_RGBA16F,
	RT_RGBA8,
	RT_MAX
};


const wchar_t* gRTNames[] = 
{ 
	L"RGBA16F", 
	L"RGBA8888", 
};

//UI & app state
CDXUTComboBox*				g_TechniqueCombo = nullptr;
IParticleSystem::Technique	g_Technique = IParticleSystem::Technique_Tiled;

CDXUTComboBox*				g_RenderTargetFormatCombo = nullptr;
RenderTargetFormat			g_RenderTargetFormat = RT_RGBA16F;

CDXUTComboBox*				g_CameraCombo = nullptr;

AMD::Slider*				g_AlphaThresholdSlider = nullptr;
int							g_AlphaThreshold = 97;

CDXUTCheckBox*				g_DepthBufferCollisionsCheckBox = nullptr;
CDXUTCheckBox*				g_CullMaxZCheckBox = nullptr;
CDXUTCheckBox*				g_CullInScreenSpaceCheckBox = nullptr;
CDXUTCheckBox*				g_SortCheckBox = nullptr;
CDXUTCheckBox*				g_SupportStreaksCheckBox = nullptr;
CDXUTCheckBox*				g_UseGeometryShaderCheckBox = nullptr;
CDXUTCheckBox*				g_PauseCheckBox = nullptr;

AMD::Slider*				g_CollisionThicknessSlider = nullptr;
int							g_CollisionThickness = 40;

CDXUTCheckBox*				g_EnableSleepStateCheckBox = nullptr;
CDXUTCheckBox*				g_ShowSleepingParticlesCheckBox = nullptr;

enum LightingMode
{
	NoLighting,
	CheapLighting,
	FullLighting,
	NumLightingModes
};

CDXUTComboBox*							g_LightingModeCombo = nullptr;
LightingMode							g_LightingMode = FullLighting;

CDXUTComboBox*							g_CoarseCullingCombo = nullptr;
IParticleSystem::CoarseCullingMode		g_CoarseCullingMode = IParticleSystem::CoarseCulling8x8;



//--------------------------------------------------------------------------------------
// AMD helper classes defined here
//--------------------------------------------------------------------------------------

static AMD::ShaderCache     g_ShaderCache( AMD::ShaderCache::SHADER_AUTO_RECOMPILE_ENABLED, AMD::ShaderCache::ERROR_DISPLAY_ON_SCREEN, AMD::ShaderCache::GENERATE_ISA_DISABLED );
static AMD::HUD             g_HUD;
static AMD::Sprite			g_Blitter;
bool                        g_bRenderHUD = true;

//--------------------------------------------------------------------------------------
// UI control IDs
//--------------------------------------------------------------------------------------
enum 
{
	IDC_TOGGLEFULLSCREEN = 1,
	IDC_CHANGEDEVICE,
	IDC_PAUSE,

	IDC_SCENE_LABEL,
	IDC_SCENE,

	IDC_SORT,
	IDC_USE_GEOMETRY_SHADER,

	IDC_TECHNIQUE_LABEL,
	IDC_TECHNIQUE,
	IDC_RENDERTARGET,
	IDC_CAMERA_LABEL,
	IDC_CAMERA,

	IDC_ALPHA_THRESHOLD,
	
	IDC_LIGHTING_MODE_LABEL,
	IDC_LIGHTING_MODE,
	IDC_CULL_MAXZ,
	IDC_CULL_SCREENSPACE,
	IDC_SUPPORT_STREAKS,

	IDC_COARSE_CULLING_LABEL,
	IDC_COARSE_CULLING,

	IDC_COLLISIONS_ENABLED,
	IDC_COLLISION_THICKNESS,
	IDC_COLLISION_TEST,
	IDC_SLEEP_STATE,
	IDC_SHOW_SLEEPING_PARTICLES,

	IDC_NUM_CONTROL_IDS
};

const int AMD::g_MaxApplicationControlID = IDC_NUM_CONTROL_IDS;



//--------------------------------------------------------------------------------------
// Forward declarations 
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing, void* pUserContext );
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext );
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext );
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext );
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext );
bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo, DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext );
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext );
void CALLBACK OnD3D11DestroyDevice( void* pUserContext );
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime, float fElapsedTime, void* pUserContext );

void InitApp();
void InitGUIControls();
void RenderText();
HRESULT AddShadersToCache();
void ChangeScene();
void PopulateEmitters( int& numEmitters, IParticleSystem::EmitterParams* emitters, int maxEmitters, float frameTime );
void DoCollisionTest();

// Clean up previously allocated render target resources
void DestroyRenderTargets()
{
	SAFE_RELEASE( g_RenderTargetSRV );
	SAFE_RELEASE( g_RenderTargetRTV );
	SAFE_RELEASE( g_RenderTargetTexture );
}


// Create render targets on boot up and every time the window is resized or the format is changed
void CreateRenderTargets()
{
	// Clean up previously allocated targets
	DestroyRenderTargets();

	ID3D11Device* device = DXUTGetD3D11Device();

	// Set up new destination texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Width = g_ScreenWidth;
	desc.Height = g_ScreenHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = g_RenderTargetFormat == RT_RGBA16F ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = S_OK;
	V( device->CreateTexture2D( &desc, 0, &g_RenderTargetTexture ) );
	
	// If there is no multisampling, then we can just get a render target view to the destination texture and render directly to that
	D3D11_RENDER_TARGET_VIEW_DESC viewDesc;
	viewDesc.Format = desc.Format;
	viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	viewDesc.Texture2D.MipSlice = 0;
	V( device->CreateRenderTargetView( g_RenderTargetTexture, &viewDesc, &g_RenderTargetRTV ) );

	// Create the depth stencil surface that matches the render target view of the color scene
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = desc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	V( device->CreateShaderResourceView( g_RenderTargetTexture, &srvDesc, &g_RenderTargetSRV ) );
}



//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE, HINSTANCE, LPWSTR, int )
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) || defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    // DXUT will create and use the best device
    // that is available on the system depending on which D3D callbacks are set below

    // Set DXUT callbacks
    DXUTSetCallbackMsgProc( MsgProc );
    DXUTSetCallbackKeyboard( OnKeyboard );
    DXUTSetCallbackFrameMove( OnFrameMove );
    DXUTSetCallbackDeviceChanging( ModifyDeviceSettings );

    DXUTSetCallbackD3D11DeviceAcceptable( IsD3D11DeviceAcceptable );
    DXUTSetCallbackD3D11DeviceCreated( OnD3D11CreateDevice );
    DXUTSetCallbackD3D11SwapChainResized( OnD3D11ResizedSwapChain );
    DXUTSetCallbackD3D11SwapChainReleasing( OnD3D11ReleasingSwapChain );
    DXUTSetCallbackD3D11DeviceDestroyed( OnD3D11DestroyDevice );
    DXUTSetCallbackD3D11FrameRender( OnD3D11FrameRender );

	InitApp();
    DXUTInit( true, true, nullptr ); // Parse the command line, show msgboxes on error, no extra command line params
    DXUTSetCursorSettings( true, true );
    DXUTCreateWindow( L"GPU Particles v1.1" );

    DXUTCreateDevice( D3D_FEATURE_LEVEL_11_0, true, 1920, 1080 );
	
	InitGUIControls();

	ChangeScene();

	DXUTMainLoop(); // Enter into the DXUT render loop

	// Ensure the ShaderCache aborts if in a lengthy generation process
	g_ShaderCache.Abort();

	delete g_pGPUParticleSystem;
	g_pGPUParticleSystem = 0;

    return DXUTGetExitCode();
}


void SetCamera( int index )
{
	const CameraPosition* positions = nullptr;
	switch ( g_Scene )
	{
		default:
		case Volcano:
			positions = gCameraPositionsVolcano;
			break;

		case Tank:
			positions = gCameraPositionsTank;
			break;
	}

	const CameraPosition& camera = positions[ index ];
	g_Camera.SetViewParams( camera.m_Position, camera.m_LookAt );
}

//--------------------------------------------------------------------------------------
// Initialize the app 
//--------------------------------------------------------------------------------------
void InitApp()
{
	// Lighting
	g_GlobalConstantBuffer.m_SunDirection = DirectX::XMVector4Normalize( DirectX::XMVectorSet( 0.5f, 0.5f, 0.0f, 0.0f ) );
	
	g_GlobalConstantBuffer.m_SunColor = DirectX::XMVectorSet( 0.8f, 0.8f, 0.7f, 0.0f );
	g_GlobalConstantBuffer.m_AmbientColor = DirectX::XMVectorSet( 0.2f, 0.2f, 0.3f, 0.0f );

	g_GlobalConstantBuffer.m_FrameIndex = 0;
	g_GlobalConstantBuffer.m_ElapsedTime = 0.0f;

	D3DCOLOR DlgColor = 0x88888888; // Semi-transparent background for the dialog
    
    g_SettingsDlg.Init( &g_DialogResourceManager );
    g_HUD.m_GUI.Init( &g_DialogResourceManager );
    g_HUD.m_GUI.SetBackgroundColors( DlgColor );
    g_HUD.m_GUI.SetCallback( OnGUIEvent );

	// Disable the backbuffer AA
	g_SettingsDlg.GetDialogControl()->GetControl( DXUTSETTINGSDLG_D3D11_MULTISAMPLE_COUNT )->SetEnabled( false );
	g_SettingsDlg.GetDialogControl()->GetControl( DXUTSETTINGSDLG_D3D11_MULTISAMPLE_QUALITY )->SetEnabled( false );


	// Setup the camera's view parameters
	g_Camera.SetRotateButtons( true, false, false );
	
	wchar_t str[ MAX_PATH ];
	DXUTFindDXSDKMediaFileCch( str, MAX_PATH, L"MSH1024.bmp" );

	float fMapWidth = 300.0f;
	float fMapHeight = 80.0f;

	g_Terrain.LoadTerrain( str, 1, 800, fMapWidth, fMapHeight );
}


void InitGUIControls()
{
	const int groupDelta = 20; // smaller delta than the default one to fit everything on the screen :)
    int iY = AMD::HUD::iElementDelta;

    g_HUD.m_GUI.AddButton( IDC_TOGGLEFULLSCREEN, L"Toggle full screen", AMD::HUD::iElementOffset, iY, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight );
    g_HUD.m_GUI.AddButton( IDC_CHANGEDEVICE, L"Change device (F2)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, VK_F2 );

	g_ShaderCache.SetShowShaderISAFlag( false );
	AMD::InitApp( g_ShaderCache, g_HUD, iY, false );

	iY += groupDelta;

	g_HUD.m_GUI.AddStatic( IDC_SCENE_LABEL, L"Scene (1, 2)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight );
	g_HUD.m_GUI.AddComboBox( IDC_SCENE, AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight, 0, false, &g_SceneCombo );
	if( g_SceneCombo )
	{
		g_SceneCombo->SetDropHeight( 100 );
		for ( int i = 0; i < ARRAYSIZE( gSceneNames ); i++ )
		{
			g_SceneCombo->AddItem( gSceneNames[ i ], nullptr );
		}
		g_SceneCombo->SetSelectedByIndex( g_Scene );
	}

	g_HUD.m_GUI.AddStatic( IDC_CAMERA_LABEL, L"Camera (C)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight );
	g_HUD.m_GUI.AddComboBox( IDC_CAMERA, AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight, 0, false, &g_CameraCombo );
	
	iY += groupDelta;

	g_HUD.m_GUI.AddCheckBox( IDC_PAUSE, L"Pause Simulation (P)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, false, 'P', false, &g_PauseCheckBox );
	g_HUD.m_GUI.AddCheckBox( IDC_SORT, L"Sort Particles", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, true, 0, false, &g_SortCheckBox );
	g_HUD.m_GUI.AddCheckBox( IDC_USE_GEOMETRY_SHADER, L"Use GS (G)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, false, 'G', false, &g_UseGeometryShaderCheckBox );

	g_HUD.m_GUI.AddStatic( IDC_TECHNIQUE_LABEL, L"Technique (+/-)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight );
	g_HUD.m_GUI.AddComboBox( IDC_TECHNIQUE, AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight, 0, false, &g_TechniqueCombo );
	if( g_TechniqueCombo )
	{
		g_TechniqueCombo->SetDropHeight( 100 );
		for ( int i = 0; i < IParticleSystem::Technique_Max; i++ )
		{
			g_TechniqueCombo->AddItem( gTechniqueNames[ i ], nullptr );
		}
		g_TechniqueCombo->SetSelectedByIndex( g_Technique );
	}

	g_HUD.m_GUI.AddComboBox( IDC_RENDERTARGET, AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight, 0, false, &g_RenderTargetFormatCombo );
	if( g_RenderTargetFormatCombo )
	{
		g_RenderTargetFormatCombo->SetDropHeight( 100 );
		for ( int i = 0; i < RT_MAX; i++ )
		{
			g_RenderTargetFormatCombo->AddItem( gRTNames[ i ], nullptr );
		}
		g_RenderTargetFormatCombo->SetSelectedByIndex( g_RenderTargetFormat );
	}

	iY += groupDelta;

	g_AlphaThresholdSlider = new AMD::Slider( g_HUD.m_GUI, IDC_ALPHA_THRESHOLD, iY, L"Alpha Threshold", 70, 100, g_AlphaThreshold );

	iY += groupDelta;

	g_HUD.m_GUI.AddStatic( IDC_LIGHTING_MODE_LABEL, L"(L)ighting Mode", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight );
	g_HUD.m_GUI.AddComboBox( IDC_LIGHTING_MODE, AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight, 0, false, &g_LightingModeCombo );
	if( g_LightingModeCombo )
	{
		const wchar_t* names[] = 
		{
			L"No Lighting",
			L"Cheap Lighting",
			L"Full Lighting"
		};

		g_LightingModeCombo->SetDropHeight( 100 );
		for ( int i = 0; i < NumLightingModes; i++ )
		{
			g_LightingModeCombo->AddItem( names[ i ], nullptr );
		}
		g_LightingModeCombo->SetSelectedByIndex( g_LightingMode );
	}
		
	g_HUD.m_GUI.AddCheckBox( IDC_SUPPORT_STREAKS, L"Streaks (K)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, true, 'K', false, &g_SupportStreaksCheckBox );
	g_HUD.m_GUI.AddCheckBox( IDC_CULL_MAXZ, L"Cull Max(Z)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, true, 'Z', false, &g_CullMaxZCheckBox );
	g_HUD.m_GUI.AddCheckBox( IDC_CULL_SCREENSPACE, L"Cull in Screen-space", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, true, 0, false, &g_CullInScreenSpaceCheckBox );
		
	g_HUD.m_GUI.AddStatic( IDC_COARSE_CULLING_LABEL, L"Coarse Culling (R)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight );
	g_HUD.m_GUI.AddComboBox( IDC_COARSE_CULLING, AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth + 20, AMD::HUD::iElementHeight, 0, false, &g_CoarseCullingCombo );
	if( g_CoarseCullingCombo )
	{
		const wchar_t* names[] = 
		{
			L"No Coarse Culling",
			L"4x2 Culling",
			L"8x8 Culling",
			L"16x8 Culling",
		};

		g_CoarseCullingCombo->SetDropHeight( 70 );
		
		for ( int i = 0; i < IParticleSystem::NumCoarseCullingModes; i++ )
		{
			g_CoarseCullingCombo->AddItem( names[ i ], nullptr );
		}
		g_CoarseCullingCombo->SetSelectedByIndex( g_CoarseCullingMode );
	}

	iY += groupDelta;

	g_HUD.m_GUI.AddCheckBox( IDC_COLLISIONS_ENABLED, L"Collisions", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, true, 0, false, &g_DepthBufferCollisionsCheckBox );
	g_CollisionThicknessSlider = new AMD::Slider( g_HUD.m_GUI, IDC_COLLISION_THICKNESS, iY, L"Collision Thickness", 0, 40, g_CollisionThickness );

	g_HUD.m_GUI.AddButton( IDC_COLLISION_TEST, L"Collision Test (T)", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, 'T' );

	g_HUD.m_GUI.AddCheckBox( IDC_SLEEP_STATE, L"Enable sleep state", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, true, 0, false, &g_EnableSleepStateCheckBox );
	g_HUD.m_GUI.AddCheckBox( IDC_SHOW_SLEEPING_PARTICLES, L"Show Sleeping Particles", AMD::HUD::iElementOffset, iY += AMD::HUD::iElementDelta, AMD::HUD::iElementWidth, AMD::HUD::iElementHeight, false, 0, false, &g_ShowSleepingParticlesCheckBox );
}


//--------------------------------------------------------------------------------------
// Render the help and statistics text. This function uses the ID3DXFont interface for 
// efficient text rendering.
//--------------------------------------------------------------------------------------
void RenderText()
{
	g_pTxtHelper->Begin();
    g_pTxtHelper->SetInsertionPos( 5, 5 );
    g_pTxtHelper->SetForegroundColor( DirectX::XMVectorSet( 1.0f, 1.0f, 0.0f, 1.0f ) );
    g_pTxtHelper->DrawTextLine( DXUTGetFrameStats( DXUTIsVsyncEnabled() ) );
    g_pTxtHelper->DrawTextLine( DXUTGetDeviceStats() );

#if defined _DEBUG
	// stats only generated in debug as they involve GPU readback
	const IParticleSystem::Stats& stats = g_pGPUParticleSystem->GetStats();
	WCHAR buff[ 1024 ];
	swprintf_s( buff, 1024, L"GPU Particles: %d/%d (%d dead)", stats.m_NumActiveParticles, stats.m_MaxParticles, stats.m_NumDead );
	g_pTxtHelper->DrawTextLine( buff );
#endif


    float fGpuTime = (float)TIMER_GetTime( Gpu, L"Scene" ) * 1000.0f;

    // count digits in the total time
    int iIntegerPart = (int)fGpuTime;
    int iNumDigits = 0;
    while( iIntegerPart > 0 )
    {
        iIntegerPart /= 10;
        iNumDigits++;
    }
    iNumDigits = ( iNumDigits == 0 ) ? 1 : iNumDigits;
    // three digits after decimal, 
    // plus the decimal point itself
    int iNumChars = iNumDigits + 4;

    // dynamic formatting for swprintf_s
    WCHAR szPrecision[16];
    swprintf_s( szPrecision, 16, L"%%%d.3f", iNumChars );

    WCHAR szBuf[256];
    WCHAR szFormat[256];
    swprintf_s( szFormat, 256, L"Total:       %s", szPrecision );
    swprintf_s( szBuf, 256, szFormat, fGpuTime );
    g_pTxtHelper->DrawTextLine( szBuf );

    fGpuTime = (float)TIMER_GetTime( Gpu, L"Scene|Simulation" ) * 1000.0f;
    swprintf_s( szFormat, 256, L"+----Simulation: %s", szPrecision );
    swprintf_s( szBuf, 256, szFormat, fGpuTime );
    g_pTxtHelper->DrawTextLine( szBuf );

	if ( g_SortCheckBox->GetChecked() && g_Technique == IParticleSystem::Technique_Rasterize )
	{
		fGpuTime = (float)TIMER_GetTime( Gpu, L"Scene|Sort" ) * 1000.0f;
		swprintf_s( szFormat, 256, L"+----------Sort: %s", szPrecision );
		swprintf_s( szBuf, 256, szFormat, fGpuTime );
		g_pTxtHelper->DrawTextLine( szBuf );
	}

	if ( g_Technique != IParticleSystem::Technique_Rasterize )
	{
		if ( g_CoarseCullingMode != IParticleSystem::CoarseCullingOff )
		{
			fGpuTime = (float)TIMER_GetTime( Gpu, L"Scene|CoarseCulling" ) * 1000.0f;
			swprintf_s( szFormat, 256, L"+-CoarseCulling: %s", szPrecision );
			swprintf_s( szBuf, 256, szFormat, fGpuTime );
			g_pTxtHelper->DrawTextLine( szBuf );
		}

		fGpuTime = (float)TIMER_GetTime( Gpu, L"Scene|Culling" ) * 1000.0f;
		swprintf_s( szFormat, 256, L"+-------Culling: %s", szPrecision );
		swprintf_s( szBuf, 256, szFormat, fGpuTime );
		g_pTxtHelper->DrawTextLine( szBuf );
	}

	fGpuTime = (float)TIMER_GetTime( Gpu, L"Scene|Render" ) * 1000.0f;
    swprintf_s( szFormat, 256, L"+--------Render: %s", szPrecision );
    swprintf_s( szBuf, 256, szFormat, fGpuTime );
    g_pTxtHelper->DrawTextLine( szBuf );

    g_pTxtHelper->SetInsertionPos( 5, DXUTGetDXGIBackBufferSurfaceDesc()->Height - AMD::HUD::iElementDelta );
	g_pTxtHelper->DrawTextLine( L"Toggle GUI    : F1" );

    g_pTxtHelper->End();
}


//--------------------------------------------------------------------------------------
// Reject any D3D11 devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *, UINT , const CD3D11EnumDeviceInfo *,
                                       DXGI_FORMAT , bool , void*  )
{
    return true;
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that aren't dependant on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                     void* pUserContext )
{
    HRESULT hr;
	
    ID3D11DeviceContext* pd3dImmediateContext = DXUTGetD3D11DeviceContext();
    V_RETURN( g_DialogResourceManager.OnD3D11CreateDevice( pd3dDevice, pd3dImmediateContext ) );
    V_RETURN( g_SettingsDlg.OnD3D11CreateDevice( pd3dDevice ) );
    g_pTxtHelper = new CDXUTTextHelper( pd3dDevice, pd3dImmediateContext, &g_DialogResourceManager, 15 );

    // Create state objects
    D3D11_SAMPLER_DESC samDesc;
    ZeroMemory( &samDesc, sizeof(samDesc) );
    samDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samDesc.AddressU = samDesc.AddressV = samDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samDesc.MaxAnisotropy = 1;
    samDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samDesc.MaxLOD = D3D11_FLOAT32_MAX;
	V_RETURN( pd3dDevice->CreateSamplerState( &samDesc, &g_pSamWrapLinear ) );
    DXUT_SetDebugName( g_pSamWrapLinear, "wrap" );

	//samDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samDesc.AddressU = samDesc.AddressV = samDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	V_RETURN( pd3dDevice->CreateSamplerState( &samDesc, &g_pSamClampLinear ) );
    DXUT_SetDebugName( g_pSamClampLinear, "clamp" );

    // Create constant buffers
    D3D11_BUFFER_DESC cbDesc;
    ZeroMemory( &cbDesc, sizeof(cbDesc) );
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    cbDesc.ByteWidth = sizeof( g_GlobalConstantBuffer );
    V_RETURN( pd3dDevice->CreateBuffer( &cbDesc, nullptr, &g_pPerFrameConstantBuffer ) );
    DXUT_SetDebugName( g_pPerFrameConstantBuffer, "g_pPerFrameConstantBuffer" );

    // Create blend states 
    D3D11_BLEND_DESC BlendStateDesc;
    BlendStateDesc.AlphaToCoverageEnable = FALSE;
    BlendStateDesc.IndependentBlendEnable = FALSE;
    BlendStateDesc.RenderTarget[0].BlendEnable = TRUE;
    BlendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    BlendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; 
    BlendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; 
    BlendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    BlendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA; 
    BlendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; 
    BlendStateDesc.RenderTarget[0].RenderTargetWriteMask =  D3D11_COLOR_WRITE_ENABLE_ALL;
    V_RETURN( pd3dDevice->CreateBlendState( &BlendStateDesc, &g_pAlphaState ) );
    BlendStateDesc.RenderTarget[0].BlendEnable = FALSE;
    V_RETURN( pd3dDevice->CreateBlendState( &BlendStateDesc, &g_pOpaqueState ) );


	D3D11_RASTERIZER_DESC RasterDesc;
	RasterDesc.FillMode = D3D11_FILL_SOLID;
	
	RasterDesc.CullMode = D3D11_CULL_BACK;
    RasterDesc.FrontCounterClockwise = FALSE;
    RasterDesc.DepthBias = 0;
    RasterDesc.DepthBiasClamp = 0.0f;
    RasterDesc.SlopeScaledDepthBias = 0.0f;
    RasterDesc.DepthClipEnable = TRUE;
    RasterDesc.ScissorEnable = FALSE;
    RasterDesc.MultisampleEnable = FALSE;
    RasterDesc.AntialiasedLineEnable = FALSE;
	V( pd3dDevice->CreateRasterizerState( &RasterDesc, &g_pRasterState ) );


	D3D11_DEPTH_STENCIL_DESC DepthStencilDesc;
	DepthStencilDesc.DepthEnable = TRUE; 
	DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	DepthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS; 
	DepthStencilDesc.StencilEnable = FALSE; 
	DepthStencilDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK; 
	DepthStencilDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK; 
	V( pd3dDevice->CreateDepthStencilState( &DepthStencilDesc, &g_pDepthWriteState ) );

	DepthStencilDesc.DepthEnable = TRUE; 
	DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	V( pd3dDevice->CreateDepthStencilState( &DepthStencilDesc, &g_pDepthTestState ) );

    // Create other render resources here

    // Create AMD_SDK resources here
    g_HUD.OnCreateDevice( pd3dDevice );
    TIMER_Init( pd3dDevice )

	// Generate shaders ( this is an async operation - call AMD::ShaderCache::ShadersReady() to find out if they are complete ) 
    static bool bFirstPass = true;
    if( bFirstPass )
    {
		// Add the applications shaders to the cache
		AddShadersToCache();

		g_pGPUParticleSystem = IParticleSystem::CreateGPUSystem( g_ShaderCache );
        g_ShaderCache.GenerateShaders( AMD::ShaderCache::CREATE_TYPE_COMPILE_CHANGES );    // Only compile shaders that have changed (development mode)
        bFirstPass = false;
    }
	
	V( DirectX::CreateDDSTextureFromFile( pd3dDevice, L"..\\Media\\atlas.dds", nullptr, &g_pTextureAtlas ) );
		
	g_pGPUParticleSystem->OnCreateDevice( pd3dDevice, pd3dImmediateContext );

	V( g_Blitter.OnCreateDevice( pd3dDevice ) );
	
	V( g_Terrain.OnCreateDevice( pd3dDevice, pd3dImmediateContext ) );

	V( g_TankMesh.Create( pd3dDevice, L"Tank\\tankscene.sdkmesh" ) );
	V( g_SkyMesh.Create( pd3dDevice, L"Tank\\desertsky.sdkmesh" ) );

	return S_OK;
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that depend on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                         const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext )
{
    HRESULT hr;
	g_ScreenWidth = pBackBufferSurfaceDesc->Width;
	g_ScreenHeight = pBackBufferSurfaceDesc->Height;

    V_RETURN( g_DialogResourceManager.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );
    V_RETURN( g_SettingsDlg.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );

	CreateRenderTargets();

    // Setup the camera's projection parameters
    float fAspectRatio = pBackBufferSurfaceDesc->Width / ( FLOAT )pBackBufferSurfaceDesc->Height;
	g_Camera.SetProjParams( DirectX::XM_PI / 4, fAspectRatio, 0.1f, 1000.0f );

	g_GlobalConstantBuffer.m_ScreenWidth = g_ScreenWidth;
	g_GlobalConstantBuffer.m_ScreenHeight = g_ScreenHeight;

    // Set the location and size of the AMD standard HUD
    g_HUD.m_GUI.SetLocation( pBackBufferSurfaceDesc->Width - AMD::HUD::iDialogWidth, 0 );
    g_HUD.m_GUI.SetSize( AMD::HUD::iDialogWidth, pBackBufferSurfaceDesc->Height );
    g_HUD.OnResizedSwapChain( pBackBufferSurfaceDesc );

	
	V_RETURN( AMD::CreateDepthStencilSurface( &g_pDepthStencilTexture, &g_pDepthStencilSRV, &g_pDepthStencilView, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height, pBackBufferSurfaceDesc->SampleDesc.Count ) );
	
	g_Blitter.OnResizedSwapChain( pBackBufferSurfaceDesc );

	g_pGPUParticleSystem->OnResizedSwapChain( pBackBufferSurfaceDesc );

	

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Render the scene using the D3D11 device
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime,
                                 float fElapsedTime, void* pUserContext )
{
    // Reset the timer at start of frame
    TIMER_Reset()

	bool enableRasterOptions = g_Technique == IParticleSystem::Technique_Rasterize;
	g_SortCheckBox->SetEnabled( enableRasterOptions );
	g_SortCheckBox->SetVisible( enableRasterOptions );
	g_UseGeometryShaderCheckBox->SetEnabled( enableRasterOptions );
	g_UseGeometryShaderCheckBox->SetVisible( enableRasterOptions );

	// Increment the time IF we aren't paused
	float fFrameTime = g_PauseCheckBox->GetChecked() ? 0.0f : fElapsedTime;
	g_GlobalConstantBuffer.m_ElapsedTime += fFrameTime;

	// Wrap the timer so the numbers don't go too high
	const float wrapPeriod = 10.0f;
	if ( g_GlobalConstantBuffer.m_ElapsedTime > wrapPeriod )
	{
		g_GlobalConstantBuffer.m_ElapsedTime -= wrapPeriod;
	}

	 // If the settings dialog is being shown, then render it instead of rendering the app's scene
    if( g_SettingsDlg.IsActive() )
    {
        g_SettingsDlg.OnRender( fElapsedTime );
        return;
    }       
	
	// Increment the frame index and wrap if necessary
	if ( !g_PauseCheckBox->GetChecked() )
	{
		g_GlobalConstantBuffer.m_FrameIndex++;
		g_GlobalConstantBuffer.m_FrameIndex %= 1000;
	}
    // Clear the backbuffer and depth stencil
    float ClearColor[4] = { 0.176f, 0.196f, 0.667f, 1.0f };
 
    pd3dImmediateContext->ClearRenderTargetView( (ID3D11RenderTargetView*)DXUTGetD3D11RenderTargetView(), ClearColor );
	pd3dImmediateContext->ClearRenderTargetView( (ID3D11RenderTargetView*)g_RenderTargetRTV, ClearColor );
	pd3dImmediateContext->ClearDepthStencilView( g_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0, 0 );
  
	// Compute some matrices for our per-frame constant buffer
	DirectX::XMMATRIX mView = g_Camera.GetViewMatrix();
	DirectX::XMMATRIX mProj = g_Camera.GetProjMatrix();
	DirectX::XMMATRIX mViewProjection = mView * mProj;
	
	g_GlobalConstantBuffer.m_ViewProjection = DirectX::XMMatrixTranspose( mViewProjection );
	g_GlobalConstantBuffer.m_View  = DirectX::XMMatrixTranspose( mView );
	g_GlobalConstantBuffer.m_Projection = DirectX::XMMatrixTranspose( mProj );

	DirectX::XMMATRIX viewProjInv = DirectX::XMMatrixInverse( nullptr, mViewProjection );
	g_GlobalConstantBuffer.m_ViewProjInv = DirectX::XMMatrixTranspose( viewProjInv );

	DirectX::XMMATRIX viewInv = DirectX::XMMatrixInverse( nullptr, mView );
	g_GlobalConstantBuffer.m_ViewInv= DirectX::XMMatrixTranspose( viewInv );

	DirectX::XMMATRIX projInv = DirectX::XMMatrixInverse( nullptr, mProj );
	g_GlobalConstantBuffer.m_ProjectionInv = DirectX::XMMatrixTranspose( projInv );

	g_GlobalConstantBuffer.m_SunDirectionVS = DirectX::XMVector4Transform( g_GlobalConstantBuffer.m_SunDirection, mView );

	g_GlobalConstantBuffer.m_EyePosition = g_Camera.GetEyePt();

	g_GlobalConstantBuffer.m_FrameTime = fFrameTime;

	g_GlobalConstantBuffer.m_AlphaThreshold = (float)g_AlphaThreshold / 100.0f;
	g_GlobalConstantBuffer.m_CollisionThickness = (float)g_CollisionThickness * 0.1f;

	g_GlobalConstantBuffer.m_CollisionsEnabled = g_DepthBufferCollisionsCheckBox->GetChecked() ? 1 : 0;
	g_GlobalConstantBuffer.m_EnableSleepState = g_EnableSleepStateCheckBox->GetChecked() ? 1 : 0;
	g_GlobalConstantBuffer.m_ShowSleepingParticles = g_ShowSleepingParticlesCheckBox->GetChecked() ? 1 : 0;


    // Update the per-frame constant buffer
    HRESULT hr;
    
	D3D11_MAPPED_SUBRESOURCE MappedResource;
	V( pd3dImmediateContext->Map( g_pPerFrameConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource ) );
	memcpy( MappedResource.pData, &g_GlobalConstantBuffer, sizeof( g_GlobalConstantBuffer ) );
	pd3dImmediateContext->Unmap( g_pPerFrameConstantBuffer, 0 );
		
    // Switch off alpha blending
    float BlendFactor[1] = { 0.0f };
    pd3dImmediateContext->OMSetBlendState( g_pOpaqueState, BlendFactor, 0xffffffff );

	// Render the scene if the shader cache has finished compiling shaders
    if( g_ShaderCache.ShadersReady() )
    {
        TIMER_Begin( 0, L"Scene" )

		ID3D11ShaderResourceView* srv = 0;
		pd3dImmediateContext->CSSetShaderResources( 0, 1, &srv );

		pd3dImmediateContext->RSSetState( g_pRasterState );
		
		// Set the per-frame constant buffer on all shader stages
		pd3dImmediateContext->VSSetConstantBuffers( 0, 1, &g_pPerFrameConstantBuffer );
		pd3dImmediateContext->PSSetConstantBuffers( 0, 1, &g_pPerFrameConstantBuffer );
		pd3dImmediateContext->GSSetConstantBuffers( 0, 1, &g_pPerFrameConstantBuffer );
		pd3dImmediateContext->CSSetConstantBuffers( 0, 1, &g_pPerFrameConstantBuffer );
	
		// Set the viewport to be the full screen area
		D3D11_VIEWPORT vp;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		vp.Width = (float)g_ScreenWidth;
		vp.Height = (float)g_ScreenHeight;
		pd3dImmediateContext->RSSetViewports( 1, &vp );

		// Make the render target our intermediate buffer (ie not the back buffer)
		pd3dImmediateContext->OMSetRenderTargets( 1, &g_RenderTargetRTV, g_pDepthStencilView );
		
		// Set the global samplers
		pd3dImmediateContext->PSSetSamplers( 0, 1, &g_pSamWrapLinear );
		pd3dImmediateContext->PSSetSamplers( 1, 1, &g_pSamClampLinear );

		pd3dImmediateContext->CSSetSamplers( 0, 1, &g_pSamWrapLinear );
		pd3dImmediateContext->CSSetSamplers( 1, 1, &g_pSamClampLinear );

		// Switch on depth writes for scene
		pd3dImmediateContext->OMSetDepthStencilState( g_pDepthWriteState, 0 );
		
		switch ( g_Scene )
		{
			default:
			case Volcano:
			{
				// Render terrain
				pd3dImmediateContext->IASetInputLayout( g_pTerrainLayout );
				
				pd3dImmediateContext->VSSetShader( g_pTerrainVS, nullptr, 0 );
				pd3dImmediateContext->PSSetShader( g_pTerrainPS, nullptr, 0 );
		
				pd3dImmediateContext->IASetIndexBuffer( g_Terrain.GetTerrainIB10(), DXGI_FORMAT_R32_UINT, 0 );
		
				TERRAIN_TILE* tile = g_Terrain.GetTile( 0 );
				g_Terrain.RenderTile( tile );
				break;
			}

			case Tank:
			{
				pd3dImmediateContext->IASetInputLayout( g_pTankVertexLayout );
				
				pd3dImmediateContext->VSSetShader( g_pTankVS, nullptr, 0 );
				pd3dImmediateContext->PSSetShader( g_pTankPS, nullptr, 0 );
				g_TankMesh.Render( pd3dImmediateContext, 2, 3 );

				pd3dImmediateContext->PSSetShader( g_pSkyPS, nullptr, 0 );
				g_SkyMesh.Render( pd3dImmediateContext, 2, 3 );
				break;
			}
		}

		// Switch depth writes off and alpha blending on
		pd3dImmediateContext->OMSetDepthStencilState( g_pDepthTestState, 0 );
		pd3dImmediateContext->OMSetBlendState( g_pAlphaState, BlendFactor, 0xffffffff );
		
		// Set the particle texture atlas
		pd3dImmediateContext->PSSetShaderResources( 0, 1, &g_pTextureAtlas );
		pd3dImmediateContext->CSSetShaderResources( 6, 1, &g_pTextureAtlas );
			
		// Fill in array of emitters that we will send to the particle system
		IParticleSystem::EmitterParams emitters[ 5 ];
		int numEmitters = 0;
		PopulateEmitters( numEmitters, emitters, ARRAYSIZE( emitters ), fElapsedTime );
		
		// Unbind the depth buffer because we don't need it and we are going to be using it as shader input
		pd3dImmediateContext->OMSetRenderTargets( 1, &g_RenderTargetRTV, nullptr );
		
		// Convert our UI options into the particle system flags
		int flags = 0;
		if ( g_SortCheckBox->GetChecked() )
			flags |= IParticleSystem::PF_Sort;
		if ( g_CullMaxZCheckBox->GetChecked() )
			flags |= IParticleSystem::PF_CullMaxZ;
		if ( g_CullInScreenSpaceCheckBox->GetChecked() )
			flags |= IParticleSystem::PF_ScreenSpaceCulling;
		if ( g_SupportStreaksCheckBox->GetChecked() )
			flags |= IParticleSystem::PF_Streaks;
		
		if ( g_LightingMode == NoLighting )
			flags |= IParticleSystem::PF_NoLighting;
		else if ( g_LightingMode == CheapLighting )
			flags |= IParticleSystem::PF_CheapLighting;
		
		if ( g_UseGeometryShaderCheckBox->GetChecked() )
			flags |= IParticleSystem::PF_UseGeometryShader;
				
		// Render the GPU particles system
		g_pGPUParticleSystem->Render( fFrameTime, flags, g_Technique, g_CoarseCullingMode, emitters, numEmitters, g_pDepthStencilSRV );

		//  Unset the GS in-case we have been using it previously
		pd3dImmediateContext->GSSetShader( nullptr, nullptr, 0 );
		
        TIMER_End()
    }

    DXUT_BeginPerfEvent( DXUT_PERFEVENTCOLOR, L"HUD / Stats" );

	// Set the render target to be our back buffer
	ID3D11RenderTargetView* rtv = DXUTGetD3D11RenderTargetView();
	pd3dImmediateContext->OMSetRenderTargets( 1, &rtv, g_pDepthStencilView );
		
    AMD::ProcessUIChanges();
    
    if( g_ShaderCache.ShadersReady() )
    {
		// Switch blending off
		pd3dImmediateContext->OMSetBlendState( nullptr, nullptr, 0xffffffff );
		
		// Set the quad shader up
		pd3dImmediateContext->VSSetShader( g_pQuadVS, nullptr, 0 );
		pd3dImmediateContext->PSSetShader( g_pQuadPS, nullptr, 0 );

		// Render a simple quad containing the intermediate render target contents
		pd3dImmediateContext->IASetIndexBuffer( nullptr, DXGI_FORMAT_UNKNOWN, 0 );
		pd3dImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

		ID3D11ShaderResourceView* srvs[] = { g_RenderTargetSRV };
		pd3dImmediateContext->PSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );

		pd3dImmediateContext->Draw( 3, 0 );

		ZeroMemory( srvs, sizeof( srvs ) );
		pd3dImmediateContext->PSSetShaderResources( 0, ARRAYSIZE( srvs ), srvs );

        // Render the HUD
        if( g_bRenderHUD )
        {
			g_HUD.OnRender( fElapsedTime ); 
			RenderText();
			AMD::RenderHUDUpdates( g_pTxtHelper );
		}
    }
    else
    {
        // Render shader cache progress if still processing
        g_ShaderCache.RenderProgress( g_pTxtHelper, 15, DirectX::XMVectorSet( 1.0f, 1.0f, 0.0f, 1.0f ) );
    }
    
    DXUT_EndPerfEvent();
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11ResizedSwapChain 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext )
{
	if ( g_pGPUParticleSystem )
		g_pGPUParticleSystem->OnReleasingSwapChain();

    g_DialogResourceManager.OnD3D11ReleasingSwapChain();

	DestroyRenderTargets();

	SAFE_RELEASE( g_pDepthStencilTexture );
    SAFE_RELEASE( g_pDepthStencilView );
    SAFE_RELEASE( g_pDepthStencilSRV );
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11CreateDevice 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11DestroyDevice( void* pUserContext )
{
	g_Terrain.OnDestroyDevice();
	g_TankMesh.Destroy();
	g_SkyMesh.Destroy();

    g_DialogResourceManager.OnD3D11DestroyDevice();
    g_SettingsDlg.OnD3D11DestroyDevice();
    DXUTGetGlobalResourceCache().OnDestroyDevice();
    SAFE_DELETE( g_pTxtHelper );

	SAFE_RELEASE( g_pSamWrapLinear );
	SAFE_RELEASE( g_pSamClampLinear );

	SAFE_RELEASE( g_pTerrainVS );
	SAFE_RELEASE( g_pTerrainPS );
	SAFE_RELEASE( g_pTerrainLayout );

	SAFE_RELEASE( g_pTankVS );
	SAFE_RELEASE( g_pTankPS );
	SAFE_RELEASE( g_pTankVertexLayout );
	SAFE_RELEASE( g_pSkyPS );

	SAFE_RELEASE( g_pQuadPS );
	SAFE_RELEASE( g_pQuadVS );
	
    SAFE_RELEASE( g_pAlphaState );
    SAFE_RELEASE( g_pOpaqueState );
	SAFE_RELEASE( g_pRasterState );
	SAFE_RELEASE( g_pDepthTestState );
	SAFE_RELEASE( g_pDepthWriteState );

	SAFE_RELEASE( g_pTextureAtlas );

	if ( g_pGPUParticleSystem )
		g_pGPUParticleSystem->OnDestroyDevice();
	
    SAFE_RELEASE( g_pPerFrameConstantBuffer );
	
    // Destroy AMD_SDK resources here
	g_Blitter.OnDestroyDevice();
	g_ShaderCache.OnDestroyDevice();
	g_HUD.OnDestroyDevice();
    TIMER_Destroy()
}


//--------------------------------------------------------------------------------------
// Called right before creating a D3D9 or D3D11 device, allowing the app to modify the device settings as needed
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext )
{
    pDeviceSettings->d3d11.AutoCreateDepthStencil = false;

    return true;
}


//--------------------------------------------------------------------------------------
// Handle updates to the scene.  This is called regardless of which D3D API is used
//--------------------------------------------------------------------------------------
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext )
{
	// Update the camera's position based on user input 
    g_Camera.FrameMove( fElapsedTime );

}


//--------------------------------------------------------------------------------------
// Handle messages to the application
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing,
                          void* pUserContext )
{
    // Pass messages to dialog resource manager calls so GUI state is updated correctly
    *pbNoFurtherProcessing = g_DialogResourceManager.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;

    // Pass messages to settings dialog if its active
    if( g_SettingsDlg.IsActive() )
    {
        g_SettingsDlg.MsgProc( hWnd, uMsg, wParam, lParam );
        return 0;
    }

    // Give the dialogs a chance to handle the message first
    *pbNoFurtherProcessing = g_HUD.m_GUI.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;
    
    // Pass all remaining windows messages to camera so it can respond to user input
    g_Camera.HandleMessages( hWnd, uMsg, wParam, lParam );

    return 0;
}


//--------------------------------------------------------------------------------------
// Handle key presses
//--------------------------------------------------------------------------------------
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext )
{
    if( bKeyDown )
    {
        switch( nChar )
        {
			case VK_F1:
				g_bRenderHUD = !g_bRenderHUD;
				break;

			case VK_ADD:
				g_Technique = (IParticleSystem::Technique)( (g_Technique + 1) % IParticleSystem::Technique_Max );
				g_TechniqueCombo->SetSelectedByIndex( g_Technique );
				break;

			case VK_SUBTRACT:
				if ( g_Technique > 0 )
				{
					g_Technique = (IParticleSystem::Technique)( g_Technique - 1 );
					g_TechniqueCombo->SetSelectedByIndex( g_Technique );
				}
				else
				{
					g_Technique = (IParticleSystem::Technique)( IParticleSystem::Technique_Max - 1 );
					g_TechniqueCombo->SetSelectedByIndex( g_Technique );
				}
				break;

			case 'L':
				g_LightingMode = (LightingMode)( (g_LightingMode + 1) % NumLightingModes );
				g_LightingModeCombo->SetSelectedByIndex( g_LightingMode );
				break;

			case 'C':
			{
				int camera = g_CameraCombo->GetSelectedIndex();
				camera++;
				camera %= g_CurrentCameraNumPositions;
				g_CameraCombo->SetSelectedByIndex( camera );
				SetCamera( camera );
				break;
			}

			case 'R':
			{
				if ( bAltDown )
				{
					g_pGPUParticleSystem->Reset();
				}
				else
				{
					int mode = g_CoarseCullingCombo->GetSelectedIndex();
					mode++;
					mode %= IParticleSystem::NumCoarseCullingModes;
					g_CoarseCullingMode = (IParticleSystem::CoarseCullingMode)mode;
					g_CoarseCullingCombo->SetSelectedByIndex( g_CoarseCullingMode );
					OnGUIEvent( 0, IDC_COARSE_CULLING, nullptr, nullptr );
				}
				break;
			}

			case '1':
				if ( g_Scene != Volcano )
				{
					g_SceneCombo->SetSelectedByIndex( Volcano );
					OnGUIEvent( 0, IDC_SCENE, nullptr, nullptr );
				}
				break;

			case '2':
				if ( g_Scene != Tank )
				{
					g_SceneCombo->SetSelectedByIndex( Tank );
					OnGUIEvent( 0, IDC_SCENE, nullptr, nullptr );
				}
				break;
		}
	}
}


//--------------------------------------------------------------------------------------
// Handles the GUI events
//--------------------------------------------------------------------------------------
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext )
{
    switch( nControlID )
    {
        case IDC_TOGGLEFULLSCREEN:
            DXUTToggleFullScreen();
            break;
        case IDC_CHANGEDEVICE:
            g_SettingsDlg.SetActive( !g_SettingsDlg.IsActive() );
            break;

		case IDC_SCENE:
			g_Scene = (SceneType)g_SceneCombo->GetSelectedIndex();
			ChangeScene();
			break;

		case IDC_TECHNIQUE:
			g_Technique = (IParticleSystem::Technique)g_TechniqueCombo->GetSelectedIndex();
			break;

		case IDC_RENDERTARGET:
			g_RenderTargetFormat = (RenderTargetFormat)g_RenderTargetFormatCombo->GetSelectedIndex();
			CreateRenderTargets();
			break;

		case IDC_CAMERA:
			SetCamera( g_CameraCombo->GetSelectedIndex() );
			break;
		
		case IDC_ALPHA_THRESHOLD:
			g_AlphaThresholdSlider->OnGuiEvent();
			break;

		case IDC_COLLISION_THICKNESS:
			g_CollisionThicknessSlider->OnGuiEvent();
			break;

		case IDC_LIGHTING_MODE:
			g_LightingMode = (LightingMode)g_LightingModeCombo->GetSelectedIndex();
			break;

		case IDC_COARSE_CULLING:
			g_CoarseCullingMode = (IParticleSystem::CoarseCullingMode)g_CoarseCullingCombo->GetSelectedIndex();
			break;

		case IDC_COLLISION_TEST:
			DoCollisionTest();
			break;

		default:
			AMD::OnGUIEvent( nEvent, nControlID, pControl, pUserContext );
			break;
    }
}


//--------------------------------------------------------------------------------------
// Adds all shaders to the shader cache
//--------------------------------------------------------------------------------------
HRESULT AddShadersToCache()
{
    HRESULT hr = E_FAIL;
        
	const D3D11_INPUT_ELEMENT_DESC TerrainLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

	const D3D11_INPUT_ELEMENT_DESC SceneLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pTerrainVS, AMD::ShaderCache::SHADER_TYPE_VERTEX, L"vs_5_0", L"TerrainVS", L"Terrain.hlsl", 0, nullptr, &g_pTerrainLayout, TerrainLayout, ARRAYSIZE( TerrainLayout ) );
	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pTerrainPS, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"TerrainPS", L"Terrain.hlsl", 0, nullptr, nullptr, nullptr, 0 );

	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pTankVS, AMD::ShaderCache::SHADER_TYPE_VERTEX, L"vs_5_0", L"VS_RenderScene", L"RenderScene.hlsl", 0, nullptr, &g_pTankVertexLayout, SceneLayout, ARRAYSIZE( SceneLayout ) );
	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pTankPS, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"PS_RenderScene", L"RenderScene.hlsl", 0, nullptr, nullptr, nullptr, 0 );
	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pSkyPS, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"PS_Sky", L"RenderScene.hlsl", 0, nullptr, nullptr, nullptr, 0 );

	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pQuadVS, AMD::ShaderCache::SHADER_TYPE_VERTEX, L"vs_5_0", L"FSQuadVS", L"FullscreenQuad.hlsl", 0, nullptr, 0, 0, 0 );
	g_ShaderCache.AddShader( (ID3D11DeviceChild**)&g_pQuadPS, AMD::ShaderCache::SHADER_TYPE_PIXEL, L"ps_5_0", L"FSQuadPS", L"FullscreenQuad.hlsl", 0, nullptr, nullptr, nullptr, 0 );


	return hr;
}


void ChangeScene()
{
	ZeroMemory( g_EmissionRates, sizeof( g_EmissionRates ) );

	switch ( g_Scene )
	{
		default:
		case Volcano:
		{
			g_CurrentCameraPositions = gCameraPositionsVolcano;
			g_CurrentCameraNumPositions = ARRAYSIZE( gCameraPositionsVolcano );

			g_Camera.SetScalers( 0.01f, 50.0f );

			DirectX::XMVECTOR spawnPosition = DirectX::XMVectorSet( 2.0f, 70.0f, 26.0f, 1.0f );

			// Sparks
			g_EmissionRates[ 0 ].m_ParticlesPerSecond = 1500.0f;
			
			g_GlobalConstantBuffer.m_StartColor[ 0 ] = DirectX::XMVectorSet( 10.0f, 10.0f, 0.0f, 1.0f );
			g_GlobalConstantBuffer.m_EndColor[ 0 ] = DirectX::XMVectorSet( 1.0f, 0.0f, 0.0f, 1.0f );
			g_GlobalConstantBuffer.m_EmitterLightingCenter[ 0 ] = spawnPosition;

			g_VolcanoSparksEmitter.m_Position = spawnPosition;
			g_VolcanoSparksEmitter.m_Velocity = DirectX::XMVectorSet( 0.0f, 30.0f, 0.0f, 0.0f );
			g_VolcanoSparksEmitter.m_NumToEmit = 0;
			g_VolcanoSparksEmitter.m_ParticleLifeSpan = 40.0f;
			g_VolcanoSparksEmitter.m_StartSize = 0.3f;
			g_VolcanoSparksEmitter.m_EndSize = 0.1f;
			g_VolcanoSparksEmitter.m_PositionVariance = DirectX::XMVectorSet( 5.0f, 0.0f, 5.0f, 1.0f );
			g_VolcanoSparksEmitter.m_VelocityVariance = 0.4f;
			g_VolcanoSparksEmitter.m_Mass = 1.0f;
			g_VolcanoSparksEmitter.m_TextureIndex = 1;
			g_VolcanoSparksEmitter.m_Streaks = true;
						
			// Smoke
			g_EmissionRates[ 1 ].m_ParticlesPerSecond = 100.0f;
			
			g_GlobalConstantBuffer.m_StartColor[ 1 ] = DirectX::XMVectorSet( 0.5f, 0.5f, 0.5f, 1.0f );
			g_GlobalConstantBuffer.m_EndColor[ 1 ] = DirectX::XMVectorSet( 0.6f, 0.6f, 0.65f, 1.0f );
			g_GlobalConstantBuffer.m_EmitterLightingCenter[ 1 ] = spawnPosition;

			g_VolcanoSmokeEmitter.m_Position = spawnPosition;
			g_VolcanoSmokeEmitter.m_Velocity = DirectX::XMVectorSet( 0.0f, 5.0f, 0.0f, 0.0f );
			g_VolcanoSmokeEmitter.m_NumToEmit = 0;
			g_VolcanoSmokeEmitter.m_ParticleLifeSpan = 150.0f;
			g_VolcanoSmokeEmitter.m_StartSize = 5.0f;
			g_VolcanoSmokeEmitter.m_EndSize = 22.0f;
			g_VolcanoSmokeEmitter.m_PositionVariance = DirectX::XMVectorSet( 2.0f, 0.0f, 2.0f, 1.0f );
			g_VolcanoSmokeEmitter.m_VelocityVariance = 0.6f;
			g_VolcanoSmokeEmitter.m_Mass = 0.0003f;
			g_VolcanoSmokeEmitter.m_TextureIndex = 0;
			g_VolcanoSmokeEmitter.m_Streaks = false;
			
			g_CollisionThicknessSlider->SetValue( 40 );

			break;
		}

		case Tank:
		{
			g_CurrentCameraPositions = gCameraPositionsTank;
			g_CurrentCameraNumPositions = ARRAYSIZE( gCameraPositionsTank );

			g_Camera.SetScalers( 0.01f, 5.0f );

			const DirectX::XMVECTOR spawnPositions[] = 
			{
				DirectX::XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f ),
				DirectX::XMVectorSet( -23.0f, 4.0f, 20.0f, 1.0f )
			};

			const DirectX::XMVECTOR startColors[] = 
			{
				DirectX::XMVectorSet( 0.5f, 0.5f, 0.5f, 1.0f ),
				DirectX::XMVectorSet( 0.4f, 0.4f, 0.4f, 1.0f )
			};

			const DirectX::XMVECTOR endColors[] = 
			{
				DirectX::XMVectorSet( 0.8f, 0.8f, 0.8f, 1.0f ),
				DirectX::XMVectorSet( 0.8f, 0.8f, 0.8f, 1.0f )
			};

			// Smoke
			g_EmissionRates[ 0 ].m_ParticlesPerSecond = 5.0f;
			g_EmissionRates[ 1 ].m_ParticlesPerSecond = 10.0f;
			
			int numTankEmitters = ARRAYSIZE( g_TankSmokeEmitters );

			for ( int i = 0; i < numTankEmitters; i++ )
			{
				g_GlobalConstantBuffer.m_StartColor[ i ] = startColors[ i ];
				g_GlobalConstantBuffer.m_EndColor[ i ] = endColors[ i ];
				g_GlobalConstantBuffer.m_EmitterLightingCenter[ i ] = spawnPositions[ i ];
				
				g_TankSmokeEmitters[ i ].m_Position = spawnPositions[ i ];
				g_TankSmokeEmitters[ i ].m_Velocity = DirectX::XMVectorSet( 0.2f, 0.1f, 0.1f, 0.0f );
				g_TankSmokeEmitters[ i ].m_NumToEmit = 0;
				g_TankSmokeEmitters[ i ].m_ParticleLifeSpan = 50.0f;
				g_TankSmokeEmitters[ i ].m_StartSize = i == 0 ? 0.2f : 3.0f;
				g_TankSmokeEmitters[ i ].m_EndSize = i == 0 ? 5.2f : 10.0f;
				g_TankSmokeEmitters[ i ].m_PositionVariance = DirectX::XMVectorSet( 4.4f, 0.5f, 4.4f, 1.0f );
				g_TankSmokeEmitters[ i ].m_VelocityVariance = 0.65f;
				g_TankSmokeEmitters[ i ].m_Mass = 0.0003f;
				g_TankSmokeEmitters[ i ].m_TextureIndex = 0;
				g_TankSmokeEmitters[ i ].m_Streaks = false;
			}

			g_CollisionThicknessSlider->SetValue( 2 );
			
			break;
		}
	}

	// Reset the particle system when the scene changes so no particles from the previous scene persist
	g_pGPUParticleSystem->Reset();

	if( g_CameraCombo )
	{
		g_CameraCombo->SetDropHeight( 100 );
		g_CameraCombo->RemoveAllItems();
		
		for ( int i = 0; i < g_CurrentCameraNumPositions; i++ )
		{
			g_CameraCombo->AddItem( g_CurrentCameraPositions[ i ].m_Name, nullptr );
		}
		g_CameraCombo->SetSelectedByIndex( 0 );
	}

	SetCamera( 0 );

	CDXUTButton* button = g_HUD.m_GUI.GetButton( IDC_COLLISION_TEST );
	if ( button )
	{
		button->SetEnabled( g_Scene == Tank );
		button->SetVisible( g_Scene == Tank );
	}
}


void PopulateEmitters( int& numEmitters, IParticleSystem::EmitterParams* emitters, int maxEmitters, float frameTime )
{
	// Set the emitters up based on the scene type
	numEmitters = 0;
	switch ( g_Scene )
	{
		default:
		case Volcano:
		{
			emitters[ numEmitters++ ] = g_VolcanoSparksEmitter;
			emitters[ numEmitters++ ] = g_VolcanoSmokeEmitter;
			break;
		}

		case Tank:
		{
			for ( int i = 0; i < ARRAYSIZE( g_TankSmokeEmitters ); i++ )
			{
				emitters[ numEmitters++ ] = g_TankSmokeEmitters[ i ];
			}
			break;
		}
	}

	// Add the collision test emitter if we are doing a collision stress test
	if ( g_SpawnCollisionTestParticles )
	{
		emitters[ numEmitters++ ] = g_CollisionTestEmitter;
		g_SpawnCollisionTestParticles = false;
	}

	// Update all our active emitters so we know how many whole numbers of particles to emit from each emitter this frame
	for ( int i = 0; i < numEmitters; i++ )
	{
		if ( g_EmissionRates[ i ].m_ParticlesPerSecond > 0.0f )
		{
			g_EmissionRates[ i ].m_Accumulation += g_EmissionRates[ i ].m_ParticlesPerSecond * ( g_PauseCheckBox->GetChecked() ? 0.0f : frameTime );

			if ( g_EmissionRates[ i ].m_Accumulation > 1.0f )
			{
				float integerPart = 0.0f;
				float fraction = modf( g_EmissionRates[ i ].m_Accumulation, &integerPart );
				
				emitters[ i ].m_NumToEmit = (int)integerPart;
				g_EmissionRates[ i ].m_Accumulation = fraction;
			}
		}
	}
}


void DoCollisionTest()
{
	// Set up some spawn parameters to dump a load of particles into our scene for a collision stress test.
	// When hammering 'T' huge numbers of particles will be spawned. As they overlap in screen space, they are 
	// likely to exceed the per tile buffer limits. This will result in visual artefacts. In a real world scenario, 
	// it is up to the user to decide how to handle this either by limiting the number of particles that can be spawned or 
	// setting the buffer sizes appropriately. Be aware that increasing the buffer sizes increases the LDS requirements.
	// Increasing the amount of LDS used per thread group may result in fewer waves being run in parallel.


	g_SpawnCollisionTestParticles = true;

	DirectX::XMVECTOR spawnPosition = DirectX::XMVectorSet( 0.0f, 6.0f, 0.0f, 1.0f );

	g_GlobalConstantBuffer.m_StartColor[ 2 ] = DirectX::XMVectorSet( 1.0f, 0.0f, 0.0f, 1.0f );
	g_GlobalConstantBuffer.m_EndColor[ 2 ] = g_GlobalConstantBuffer.m_StartColor[ 2 ];
	g_GlobalConstantBuffer.m_EmitterLightingCenter[ 2 ] = spawnPosition;
				
	g_CollisionTestEmitter.m_Position = spawnPosition;
	g_CollisionTestEmitter.m_Velocity = DirectX::XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
	g_CollisionTestEmitter.m_NumToEmit = 5000;
	g_CollisionTestEmitter.m_ParticleLifeSpan = 50.0f;
	g_CollisionTestEmitter.m_StartSize = 0.1f;
	g_CollisionTestEmitter.m_EndSize = 0.1f;
	g_CollisionTestEmitter.m_PositionVariance = DirectX::XMVectorSet( 12.0f, 0.0f, 12.0f, 1.0f );
	g_CollisionTestEmitter.m_VelocityVariance = 0.1f;
	g_CollisionTestEmitter.m_Mass = 0.3f;
	g_CollisionTestEmitter.m_TextureIndex = 1;
	g_CollisionTestEmitter.m_Streaks = false;
}

//--------------------------------------------------------------------------------------
// EOF.
//--------------------------------------------------------------------------------------
