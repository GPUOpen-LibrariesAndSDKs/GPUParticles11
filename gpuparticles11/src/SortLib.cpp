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
#include "SortLib.h"
#include <d3dcompiler.h>
#include <assert.h>


#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if (p) { (p)->Release(); (p) = nullptr; } }
#endif


const int MAX_NUM_TG = 1024;//128; // max 128 * 512 elements = 64k elements
typedef struct SortConstants
{
    int x,y,z,w;
}int4;


SortLib::SortLib() :
	m_device( nullptr ),
	m_context( nullptr ),
	m_pcbDispatchInfo( nullptr ),
	m_pCSSortStep( nullptr ),
	m_pCSSort512( nullptr ),
	m_pCSSortInner512( nullptr ),
	m_pCSInitArgs( nullptr ),
	m_pIndirectSortArgsBuffer( nullptr ),
	m_pIndirectSortArgsBufferUAV( nullptr )
{
}

SortLib::~SortLib()
{
	release();
}

HRESULT	SortLib::init( ID3D11Device* device, ID3D11DeviceContext* context )
{
	m_device = device;
	m_context = context;

	// Create constant buffer
    D3D11_BUFFER_DESC cbDesc;
    ZeroMemory( &cbDesc, sizeof(cbDesc) );
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDesc.ByteWidth = sizeof( int4 );
    device->CreateBuffer( &cbDesc, nullptr, &m_pcbDispatchInfo );

	ID3DBlob* pBlob;
	ID3DBlob* pErrorBlob;
	// create shaders
	
	// Step sort shader
	HRESULT hr = D3DCompileFromFile( L"..\\src\\Shaders\\SortStepCS2.hlsl", nullptr, nullptr, "BitonicSortStep", "cs_5_0", 0, 0, &pBlob, &pErrorBlob );
	if( FAILED(hr) )
    {
        if( pErrorBlob != nullptr )
            OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );
        SAFE_RELEASE( pErrorBlob );
        return hr;
    }
    SAFE_RELEASE( pErrorBlob );
	device->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &m_pCSSortStep );
	
	// Create inner sort shader
	const D3D10_SHADER_MACRO innerDefines[2] = {{"SORT_SIZE", "512"}, {nullptr,0}};
	hr = D3DCompileFromFile( L"..\\src\\Shaders\\SortInnerCS.hlsl", innerDefines, nullptr, "BitonicInnerSort", "cs_5_0", 0, 0, &pBlob, &pErrorBlob );
	if( FAILED(hr) )
    {
        if( pErrorBlob != NULL )
            OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );
        SAFE_RELEASE( pErrorBlob );
        return hr;
    }
    SAFE_RELEASE( pErrorBlob );
	device->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &m_pCSSortInner512 );

	// create 
	const D3D10_SHADER_MACRO cs512Defines[2] = {{"SORT_SIZE", "512"}, {nullptr,0}};
	hr = D3DCompileFromFile( L"..\\src\\Shaders\\SortCS.hlsl", cs512Defines, nullptr, "BitonicSortLDS", "cs_5_0", 0, 0, &pBlob, &pErrorBlob );
	if( FAILED(hr) )
    {
        if( pErrorBlob != nullptr )
            OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );
        SAFE_RELEASE( pErrorBlob );
        return hr;
    }
    SAFE_RELEASE( pErrorBlob );
	device->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &m_pCSSort512 );

	hr = D3DCompileFromFile( L"..\\src\\Shaders\\InitSortArgsCS.hlsl", nullptr, nullptr, "InitDispatchArgs", "cs_5_0", 0, 0, &pBlob, &pErrorBlob );
	if( FAILED(hr) )
    {
        if( pErrorBlob != nullptr )
            OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );
        SAFE_RELEASE( pErrorBlob );
        return hr;
    }
    SAFE_RELEASE( pErrorBlob );
	device->CreateComputeShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &m_pCSInitArgs );


	D3D11_BUFFER_DESC desc;
	ZeroMemory( &desc, sizeof( desc ) );
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	desc.ByteWidth = 4 * sizeof( UINT );
	desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
	device->CreateBuffer( &desc, nullptr, &m_pIndirectSortArgsBuffer );

	D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
	ZeroMemory( &uav, sizeof( uav ) );
	uav.Format = DXGI_FORMAT_R32_UINT;
	uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uav.Buffer.FirstElement = 0;
	uav.Buffer.NumElements = 4;
	uav.Buffer.Flags = 0;
	device->CreateUnorderedAccessView( m_pIndirectSortArgsBuffer, &uav, &m_pIndirectSortArgsBufferUAV );

	return hr;
}

void SortLib::run( unsigned int maxSize, ID3D11UnorderedAccessView* sortBufferUAV, ID3D11Buffer* itemCountBuffer )
{
	// Capture current state
	ID3D11UnorderedAccessView* prevUAV = nullptr;
	m_context->CSGetUnorderedAccessViews( 0, 1, &prevUAV );

	ID3D11Buffer* prevCBs[] = { nullptr, nullptr };
	m_context->CSGetConstantBuffers( 0, ARRAYSIZE( prevCBs ), prevCBs );

	ID3D11Buffer* cbs[] = { itemCountBuffer, m_pcbDispatchInfo };
	m_context->CSSetConstantBuffers( 0, ARRAYSIZE( cbs ), cbs );
	
	// Write the indirect args to a UAV
	m_context->CSSetUnorderedAccessViews( 0, 1, &m_pIndirectSortArgsBufferUAV, nullptr );

	m_context->CSSetShader( m_pCSInitArgs, nullptr, 0 );
	m_context->Dispatch( 1, 1, 1 );
	
	
	m_context->CSSetUnorderedAccessViews( 0, 1, &sortBufferUAV, nullptr );
	
	bool bDone = sortInitial( maxSize );
	
	int presorted = 512;
	while (!bDone) 
	{
		bDone = sortIncremental( presorted, maxSize );
		presorted *= 2;
	}

#ifdef _DEBUG
	// this leaks resources somehow. Haven't looked into it yet.
	//manualValidate(maxSize, pUAV );
#endif

	// Restore previous state
	m_context->CSSetUnorderedAccessViews( 0, 1, &prevUAV, nullptr );
	m_context->CSSetConstantBuffers( 0, ARRAYSIZE( prevCBs ), prevCBs );

	if ( prevUAV )
		prevUAV->Release();
	
	for ( size_t i = 0; i < ARRAYSIZE( prevCBs ); i++ )
		if ( prevCBs[ i ] )
			prevCBs[ i ]->Release();
}

void SortLib::release()
{
	SAFE_RELEASE( m_pcbDispatchInfo );
	SAFE_RELEASE( m_pCSSortStep );
	SAFE_RELEASE( m_pCSSort512 );
	SAFE_RELEASE( m_pCSSortInner512 );
	SAFE_RELEASE( m_pCSInitArgs );

	SAFE_RELEASE( m_pIndirectSortArgsBufferUAV );
	SAFE_RELEASE( m_pIndirectSortArgsBuffer );
}

bool SortLib::sortInitial( unsigned int maxSize )
{
	bool bDone = true;

	// calculate how many threads we'll require:
	//   we'll sort 512 elements per CU (threadgroupsize 256)
	//     maybe need to optimize this or make it changeable during init
	//     TGS=256 is a good intermediate value
	

	unsigned int numThreadGroups = ((maxSize-1)>>9)+1;

	assert( numThreadGroups <=MAX_NUM_TG );

	if( numThreadGroups>1 ) bDone = false;

	// sort all buffers of size 512 (and presort bigger ones)
	m_context->CSSetShader( m_pCSSort512, nullptr, 0 );
	m_context->DispatchIndirect( m_pIndirectSortArgsBuffer, 0 );

	return bDone;
}

bool SortLib::sortIncremental( unsigned int presorted, unsigned int maxSize )
{
	bool bDone = true;
	m_context->CSSetShader( m_pCSSortStep, nullptr, 0 );
	
	// prepare thread group description data
	unsigned int numThreadGroups=0;
	
	if( maxSize > presorted )
	{	
		if( maxSize>presorted*2 )
			bDone = false;

		unsigned int pow2 = presorted; 
		while( pow2<maxSize ) 
			pow2 *= 2;
		numThreadGroups = pow2>>9;
	}	

	unsigned int nMergeSize = presorted*2;
	for( unsigned int nMergeSubSize=nMergeSize>>1; nMergeSubSize>256; nMergeSubSize=nMergeSubSize>>1 ) 
//	for( int nMergeSubSize=nMergeSize>>1; nMergeSubSize>0; nMergeSubSize=nMergeSubSize>>1 ) 
	{
		D3D11_MAPPED_SUBRESOURCE MappedResource;
		
		m_context->Map( m_pcbDispatchInfo, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource );
		SortConstants* sc = (SortConstants*)MappedResource.pData;
		sc->x = nMergeSubSize;
		if( nMergeSubSize == nMergeSize>>1 )
		{
			sc->y = (2*nMergeSubSize-1);
			sc->z = -1;
		}
		else
		{
			sc->y = nMergeSubSize;
			sc->z = 1;
		}
		sc->w = 0;
		m_context->Unmap( m_pcbDispatchInfo, 0 );

		m_context->Dispatch( numThreadGroups, 1, 1 );
	}
	
	m_context->CSSetShader( m_pCSSortInner512, nullptr, 0 );
	m_context->Dispatch( numThreadGroups, 1, 1 );
	
	return bDone;
}

#ifdef _DEBUG

#pragma pack(push,1)
typedef struct
{
	float	sortval;
	float	data;
}BufferData;
#pragma pack(pop)

// copy the sorted data to CPU memory so it can be viewed in the debugger
void SortLib::manualValidate( unsigned int size, ID3D11UnorderedAccessView* pUAV )
{
    ID3D11Resource* srcResource;
    pUAV->GetResource(&srcResource);
    ID3D11Buffer* srcBuffer;
    srcResource->QueryInterface(IID_ID3D11Buffer, (void**)&srcBuffer);

    D3D11_BUFFER_DESC srcDesc;
    srcBuffer->GetDesc(&srcDesc);

    // create a readbackbuffer for manual verification of correctness
    ID3D11Buffer* readBackBuffer;
    D3D11_BUFFER_DESC bDesc = srcDesc;
	bDesc.Usage = D3D11_USAGE_STAGING;
	bDesc.CPUAccessFlags |= D3D11_CPU_ACCESS_READ;
    bDesc.BindFlags = 0;
	m_device->CreateBuffer( &bDesc, NULL, &readBackBuffer );

	// Download the data
	D3D11_MAPPED_SUBRESOURCE MappedResource = {0}; 
    m_context->CopyResource( readBackBuffer, srcResource );
	m_context->Map( readBackBuffer, 0, D3D11_MAP_READ, 0, &MappedResource );

    bool correct = true;
   
        BufferData* b = &((BufferData*)(MappedResource.pData))[0];
        for( unsigned int i = 1; i<size;++i )
        {
            correct &=  b[i-1].sortval<= b[i].sortval;
            if( b[i-1].sortval > b[i].sortval )
            {
                int _abc = 0;
				(void)_abc;
            }
        }
	
	m_context->Unmap( readBackBuffer, 0 );

    srcResource->Release();
    readBackBuffer->Release();
}
#endif
