//--------------------------------------------------------------------------------------
// Terrain.h
// PIX Workshop GDC2007
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#pragma once

struct TERRAIN_VERTEX
{
	DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 norm;
    DirectX::XMFLOAT2 uv;
};


struct BOUNDING_BOX
{
    DirectX::XMFLOAT3 min;
    DirectX::XMFLOAT3 max;
};

struct TERRAIN_TILE
{
    ID3D11Buffer* pVB10;
    UINT NumVertices;
    TERRAIN_VERTEX* pRawVertices;
    BOUNDING_BOX BBox;
};

class CTerrain
{
private:
   // LPDIRECT3DDEVICE9 m_pDev;
    ID3D11Device* m_pDev10;
	ID3D11DeviceContext* m_pContext;
    UINT m_SqrtNumTiles;
    UINT m_NumTiles;
    UINT m_NumSidesPerTile;
    TERRAIN_TILE* m_pTiles;
    float m_fWorldScale;
    float m_fHeightScale;
    UINT m_HeightMapX;
    UINT m_HeightMapY;
    float* m_pHeightBits;

    UINT m_NumIndices;
    ID3D11Buffer* m_pTerrainIB10;
    UINT* m_pTerrainRawIndices;

public:
                            CTerrain();
                            ~CTerrain();

    void                    OnDestroyDevice();
	HRESULT                 OnCreateDevice( ID3D11Device* pDev, ID3D11DeviceContext* pContext );

    HRESULT                 LoadTerrain( WCHAR* strHeightMap, UINT SqrtNumTiles, UINT NumSidesPerTile,
                                         float fWorldScale, float fHeightScale );
    float                   GetHeightForTile( UINT iTile, DirectX::XMFLOAT3 pPos );
	float                   GetHeightOnMap( DirectX::XMVECTOR pos );
    DirectX::XMFLOAT3             GetNormalOnMap( DirectX::XMVECTOR pPos );
    void                    RenderTile( TERRAIN_TILE* pTile );
  
    float                   GetWorldScale()
    {
        return m_fWorldScale;
    }
 
    ID3D11Buffer* GetTerrainIB10()
    {
        return m_pTerrainIB10;
    }
    UINT                    GetNumTiles()
    {
        return m_NumTiles;
    }
    TERRAIN_TILE* GetTile( UINT iTile )
    {
        return &m_pTiles[iTile];
    }

protected:
    DirectX::XMFLOAT2             GetUVForPosition( DirectX::XMVECTOR pPos );
    HRESULT                 LoadBMPImage( WCHAR* strHeightMap );
    HRESULT                 GenerateTile( TERRAIN_TILE* pTile, BOUNDING_BOX* pBBox );
    HRESULT                 CreateTileResources( TERRAIN_TILE* pTile );
};

float RPercent();

//--------------------------------------------------------------------------------------
template <class T> void QuickDepthSort( T* indices, float* depths, int lo, int hi )
{
    //  lo is the lower index, hi is the upper index
    //  of the region of array a that is to be sorted
    int i = lo, j = hi;
    float h;
    T index;
    float x = depths[( lo + hi ) / 2];

    //  partition
    do
    {
        while( depths[i] < x ) i++;
        while( depths[j] > x ) j--;
        if( i <= j )
        {
            h = depths[i]; depths[i] = depths[j]; depths[j] = h;
            index = indices[i]; indices[i] = indices[j]; indices[j] = index;
            i++; j--;
        }
    } while( i <= j );

    //  recursion
    if( lo < j ) QuickDepthSort( indices, depths, lo, j );
    if( i < hi ) QuickDepthSort( indices, depths, i, hi );
}
