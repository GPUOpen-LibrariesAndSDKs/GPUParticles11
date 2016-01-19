//--------------------------------------------------------------------------------------
// Terrain.cpp
// PIX Workshop GDC2007
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "..\\..\\DXUT\\Core\\DXUT.h"
#include "Terrain.h"


#pragma warning( disable : 4100 ) // disable unreference formal parameter warnings for /W4 builds

//--------------------------------------------------------------------------------------
CTerrain::CTerrain() : m_pDev10( NULL ),
                       m_SqrtNumTiles( 0 ),
                       m_NumTiles( 0 ),
                       m_NumSidesPerTile( 0 ),
                       m_pTiles( NULL ),
                       m_fWorldScale( 0.0f ),
                       m_fHeightScale( 0.0f ),
                       m_HeightMapX( 0 ),
                       m_HeightMapY( 0 ),
                       m_pHeightBits( NULL ),
                       m_NumIndices( 0 ),
                       m_pTerrainIB10( NULL ),
                       m_pTerrainRawIndices( NULL )
{
}


//--------------------------------------------------------------------------------------
CTerrain::~CTerrain()
{
    for( UINT i = 0; i < m_NumTiles; i++ )
    {
        SAFE_DELETE_ARRAY( m_pTiles[i].pRawVertices );
    }

    SAFE_DELETE_ARRAY( m_pTiles );
    SAFE_DELETE_ARRAY( m_pHeightBits );
    SAFE_DELETE_ARRAY( m_pTerrainRawIndices );
}


//--------------------------------------------------------------------------------------
void CTerrain::OnDestroyDevice()
{
    for( UINT i = 0; i < m_NumTiles; i++ )
    {
        SAFE_RELEASE( m_pTiles[i].pVB10 );
    }
    SAFE_RELEASE( m_pTerrainIB10 );
}



//--------------------------------------------------------------------------------------
HRESULT CTerrain::OnCreateDevice( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pContext )
{
    HRESULT hr = S_OK;
    m_pDev10 = pd3dDevice;
	m_pContext = pContext;
   
    if( 0 == m_NumTiles )
        return S_FALSE;

    // Create the terrain tile vertex buffers
    for( UINT i = 0; i < m_NumTiles; i++ )
    {
        V_RETURN( CreateTileResources( &m_pTiles[i] ) );
    }

    // Create the index buffer
    D3D11_BUFFER_DESC BufferDesc;
    BufferDesc.ByteWidth = m_NumIndices * sizeof( UINT );
    BufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    BufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    BufferDesc.CPUAccessFlags = 0;
    BufferDesc.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    InitData.pSysMem = m_pTerrainRawIndices;

    V_RETURN( pd3dDevice->CreateBuffer( &BufferDesc, &InitData, &m_pTerrainIB10 ) );

    return hr;
}

//--------------------------------------------------------------------------------------
HRESULT CTerrain::LoadTerrain( WCHAR* strHeightMap, UINT SqrtNumTiles, UINT NumSidesPerTile, float fWorldScale,
                               float fHeightScale )
{
    HRESULT hr = S_OK;

    // Store variables
    m_SqrtNumTiles = SqrtNumTiles;
    m_fWorldScale = fWorldScale;
    m_fHeightScale = fHeightScale;
    m_NumSidesPerTile = NumSidesPerTile;
    m_NumTiles = SqrtNumTiles * SqrtNumTiles;

    // Load the image
    V_RETURN( LoadBMPImage( strHeightMap ) );

    // Create tiles
    m_pTiles = new TERRAIN_TILE[ m_NumTiles ];
    if( !m_pTiles )
        return E_OUTOFMEMORY;

    UINT iTile = 0;
    float zStart = -m_fWorldScale / 2.0f;
    float zDelta = m_fWorldScale / ( float )m_SqrtNumTiles;
    float xDelta = m_fWorldScale / ( float )m_SqrtNumTiles;
    for( UINT z = 0; z < m_SqrtNumTiles; z++ )
    {
        float xStart = -m_fWorldScale / 2.0f;
        for( UINT x = 0; x < m_SqrtNumTiles; x++ )
        {
            BOUNDING_BOX BBox;
			BBox.min = DirectX::XMFLOAT3( xStart, 0, zStart );
            BBox.max = DirectX::XMFLOAT3( xStart + xDelta, 0, zStart + zDelta );

            V_RETURN( GenerateTile( &m_pTiles[iTile], &BBox ) );

            iTile ++;
            xStart += xDelta;
        }
        zStart += zDelta;
    }

    // Create the indices for the tile strips
    m_NumIndices = ( m_NumSidesPerTile + 2 ) * 2 * ( m_NumSidesPerTile )- 2;
    m_pTerrainRawIndices = new UINT[ m_NumIndices ];
    if( !m_pTerrainRawIndices )
        return E_OUTOFMEMORY;

    UINT vIndex = 0;
    UINT iIndex = 0;
    for( UINT z = 0; z < m_NumSidesPerTile; z++ )
    {
        for( UINT x = 0; x < m_NumSidesPerTile + 1; x++ )
        {
            m_pTerrainRawIndices[iIndex] = vIndex;
            iIndex++;
            m_pTerrainRawIndices[iIndex] = vIndex + ( UINT )m_NumSidesPerTile + 1;
            iIndex++;
            vIndex++;
        }
        if( z != m_NumSidesPerTile - 1 )
        {
            // add a degenerate tri
            m_pTerrainRawIndices[iIndex] = vIndex + ( UINT )m_NumSidesPerTile;
            iIndex++;
            m_pTerrainRawIndices[iIndex] = vIndex;
            iIndex++;
        }
    }

    return hr;
}


//--------------------------------------------------------------------------------------
float CTerrain::GetHeightForTile( UINT iTile, DirectX::XMFLOAT3 pPos )
{
    // TODO: impl
    return 0.0f;
}


//--------------------------------------------------------------------------------------
#define HEIGHT_INDEX( a, b ) ( (b)*m_HeightMapX + (a) )
#define LINEAR_INTERPOLATE(a,b,x) (a*(1.0f-x) + b*x)
float CTerrain::GetHeightOnMap( DirectX::XMVECTOR pos )
{
    // move x and z into [0..1] range
    DirectX::XMFLOAT2 uv = GetUVForPosition( pos );
    float x = uv.x;
    float z = uv.y;

    // scale into heightmap space
    x *= m_HeightMapX;
    z *= m_HeightMapY;
    x += 0.5f;
    z += 0.5f;
    if( x >= m_HeightMapX - 1 )
        x = ( float )m_HeightMapX - 2;
    if( z >= m_HeightMapY - 1 )
        z = ( float )m_HeightMapY - 2;
    z = DirectX::XMMax( 0.0f, z );
    x = DirectX::XMMax( 0.0f, x );

    // bilinearly interpolate
	unsigned long integer_X = unsigned long( x );
    float fractional_X = x - integer_X;

	unsigned long integer_Z = unsigned long( z );
    float fractional_Z = z - integer_Z;

    float v1 = m_pHeightBits[ HEIGHT_INDEX( integer_X,    integer_Z ) ];
    float v2 = m_pHeightBits[ HEIGHT_INDEX( integer_X + 1,integer_Z ) ];
    float v3 = m_pHeightBits[ HEIGHT_INDEX( integer_X,    integer_Z + 1 ) ];
    float v4 = m_pHeightBits[ HEIGHT_INDEX( integer_X + 1,integer_Z + 1 ) ];

    float i1 = LINEAR_INTERPOLATE( v1 , v2 , fractional_X );
    float i2 = LINEAR_INTERPOLATE( v3 , v4 , fractional_X );

    float result = LINEAR_INTERPOLATE( i1 , i2 , fractional_Z );

    return result;
}


//--------------------------------------------------------------------------------------
DirectX::XMFLOAT3 CTerrain::GetNormalOnMap( DirectX::XMVECTOR pos )
{
    // Calculate the normal
    float xDelta = ( m_fWorldScale / ( float )m_SqrtNumTiles ) / ( float )m_NumSidesPerTile;
    float zDelta = ( m_fWorldScale / ( float )m_SqrtNumTiles ) / ( float )m_NumSidesPerTile;

	DirectX::XMVECTOR vLeft = DirectX::XMVectorSubtract( pos, DirectX::XMVectorSet( xDelta, 0, 0, 0 ) );
    DirectX::XMVECTOR vRight = DirectX::XMVectorAdd( pos, DirectX::XMVectorSet( xDelta, 0, 0, 0 ) );
	DirectX::XMVECTOR vUp = DirectX::XMVectorAdd( pos, DirectX::XMVectorSet( 0, 0, zDelta, 0 ) );
    DirectX::XMVECTOR vDown = DirectX::XMVectorSubtract( pos, DirectX::XMVectorSet( 0, 0, zDelta, 0 ) );

    vLeft = DirectX::XMVectorSetY( vLeft, GetHeightOnMap( vLeft ) );
    vRight =  DirectX::XMVectorSetY( vRight, GetHeightOnMap( vRight ) );
    vUp = DirectX::XMVectorSetY( vUp, GetHeightOnMap( vUp ) );
    vDown = DirectX::XMVectorSetY( vDown, GetHeightOnMap( vDown ) );

    DirectX::XMVECTOR e0 = DirectX::XMVectorSubtract( vRight, vLeft );
    DirectX::XMVECTOR e1 = DirectX::XMVectorSubtract( vUp, vDown );
    
    DirectX::XMVECTOR ortho = DirectX::XMVector3Cross( e1, e0 );
    
	DirectX::XMFLOAT3 out;
	DirectX::XMStoreFloat3( &out, DirectX::XMVector3Normalize( ortho ) );

	return out;
	//return DirectX::XMFLOAT3( 0.0f, 1.0f, 0.0f );
}


//--------------------------------------------------------------------------------------
void CTerrain::RenderTile( TERRAIN_TILE* pTile )
{
        UINT stride = sizeof( TERRAIN_VERTEX );
        UINT offset = 0;
        m_pContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
        m_pContext->IASetVertexBuffers( 0, 1, &pTile->pVB10, &stride, &offset );
        m_pContext->DrawIndexed( m_NumIndices, 0, 0 );
   
}


//--------------------------------------------------------------------------------------
DirectX::XMFLOAT2 CTerrain::GetUVForPosition( DirectX::XMVECTOR pos )
{
    DirectX::XMFLOAT2 uv;
    uv.x = ( DirectX::XMVectorGetX( pos ) / m_fWorldScale ) + 0.5f;
    uv.y = ( DirectX::XMVectorGetZ( pos ) / m_fWorldScale ) + 0.5f;
    return uv;
}


//--------------------------------------------------------------------------------------
HRESULT CTerrain::LoadBMPImage( WCHAR* strHeightMap )
{
    FILE* fp = NULL;
    _wfopen_s( &fp, strHeightMap, L"rb" );
    if( !fp )
        return E_INVALIDARG;

    // read the bfh
    BITMAPFILEHEADER bfh;
    fread( &bfh, sizeof( BITMAPFILEHEADER ), 1, fp );
    unsigned long toBitmapData = bfh.bfOffBits;
    unsigned long bitmapSize = bfh.bfSize;

    // read the header
    BITMAPINFOHEADER bih;
    fread( &bih, sizeof( BITMAPINFOHEADER ), 1, fp );
    if( bih.biCompression != BI_RGB )
        goto Error;

    // alloc memory
    unsigned long U = m_HeightMapX = bih.biWidth;
    unsigned long V = m_HeightMapY = abs( bih.biHeight );
    m_pHeightBits = new float[ U * V ];
    if( !m_pHeightBits )
        return E_OUTOFMEMORY;

    // find the step size
    unsigned long iStep = 4;
    if( 24 == bih.biBitCount )
        iStep = 3;

    // final check for size
    unsigned long UNew = ( ( U * iStep * 8 + 31 ) & ~31 ) / ( 8 * iStep );
    if( bitmapSize < UNew * V * iStep * sizeof( BYTE ) )
        goto Error;

    // seek
    fseek( fp, toBitmapData, SEEK_SET );

    // read in the bits
    BYTE* pBits = new BYTE[ bitmapSize ];
    if( !pBits )
        return E_OUTOFMEMORY;
    fread( pBits, bitmapSize, 1, fp );
	
    // close the file
    fclose( fp );

    // Load the Height Information
    unsigned long iHeight = 0;
    unsigned long iBitmap = 0;
    for( unsigned long y = 0; y < V; y++ )
    {
        iBitmap = y * UNew * iStep;
        for( unsigned long x = 0; x < U * iStep; x += iStep )
        {
            m_pHeightBits[iHeight] = 0;
            for( unsigned long c = 0; c < iStep; c++ )
            {
                m_pHeightBits[iHeight] += pBits[ iBitmap + c ];
            }
            m_pHeightBits[iHeight] /= ( FLOAT )( iStep * 255.0 );
            m_pHeightBits[iHeight] *= m_fHeightScale;

            iHeight ++;
            iBitmap += iStep;
        }
    }

    SAFE_DELETE_ARRAY( pBits );
	
    return S_OK;

Error:
    fclose( fp );
    return E_FAIL;
}


//--------------------------------------------------------------------------------------
HRESULT CTerrain::GenerateTile( TERRAIN_TILE* pTile, BOUNDING_BOX* pBBox )
{
    HRESULT hr = S_OK;

    // Alloc memory for the vertices
    pTile->NumVertices = ( m_NumSidesPerTile + 1 ) * ( m_NumSidesPerTile + 1 );
    pTile->pRawVertices = new TERRAIN_VERTEX[ pTile->NumVertices ];
    if( !pTile->pRawVertices )
        return E_OUTOFMEMORY;

    pTile->BBox = *pBBox;

    UINT iVertex = 0;
    float zStart = pBBox->min.z;
    float xDelta = ( pBBox->max.x - pBBox->min.x ) / ( float )m_NumSidesPerTile;
    float zDelta = ( pBBox->max.z - pBBox->min.z ) / ( float )m_NumSidesPerTile;

    // Loop through terrain vertices and get height from the heightmap
    for( UINT z = 0; z < m_NumSidesPerTile + 1; z++ )
    {
        float xStart = pBBox->min.x;
        for( UINT x = 0; x < m_NumSidesPerTile + 1; x++ )
        {
			DirectX::XMVECTOR pos = DirectX::XMVectorSet( xStart,0,zStart, 0 );
            
			pos = DirectX::XMVectorSetY( pos, GetHeightOnMap( pos ) );

			DirectX::XMStoreFloat3( &pTile->pRawVertices[iVertex].pos, pos );
            pTile->pRawVertices[iVertex].uv = GetUVForPosition( pos );
            pTile->pRawVertices[iVertex].uv.y = 1.0f - pTile->pRawVertices[iVertex].uv.y;
            pTile->pRawVertices[iVertex].norm = GetNormalOnMap( pos );

            iVertex ++;
            xStart += xDelta;
        }
        zStart += zDelta;
    }

    return hr;
}


//--------------------------------------------------------------------------------------
HRESULT CTerrain::CreateTileResources( TERRAIN_TILE* pTile )
{
    HRESULT hr = S_OK;

    if( m_pDev10 )
    {
        D3D11_BUFFER_DESC BufferDesc;
        BufferDesc.ByteWidth = m_pTiles->NumVertices * sizeof( TERRAIN_VERTEX );
        BufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        BufferDesc.CPUAccessFlags = 0;
        BufferDesc.MiscFlags = 0;

        D3D11_SUBRESOURCE_DATA InitData;
        InitData.pSysMem = pTile->pRawVertices;
        V_RETURN( m_pDev10->CreateBuffer( &BufferDesc, &InitData, &pTile->pVB10 ) );
    }

    return hr;
}


//--------------------------------------------------------------------------------------
float RPercent()
{
    float ret = ( float )( ( rand() % 20000 ) - 10000 );
    return ret / 10000.0f;
}