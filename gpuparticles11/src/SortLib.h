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
#pragma once

class SortLib
{
public:
	SortLib();
	virtual ~SortLib();

	HRESULT init( ID3D11Device* device, ID3D11DeviceContext* context );
	void run( unsigned int maxSize, ID3D11UnorderedAccessView* sortBufferUAV, ID3D11Buffer* itemCountBuffer );
	void release();

private:
	bool sortInitial		( unsigned int maxSize );
	bool sortIncremental	( unsigned int presorted, unsigned int maxSize );

#ifdef _DEBUG
    void manualValidate     ( unsigned int maxSize, ID3D11UnorderedAccessView* pUAV );
#endif

private:

	ID3D11Device*					m_device;
	ID3D11DeviceContext*			m_context;
	ID3D11Buffer*					m_pcbDispatchInfo;		// constant buffer containing dispatch specific information
	
	ID3D11ComputeShader*			m_pCSSortStep;			// CS port of the VS/PS bitonic sort
	ID3D11ComputeShader*			m_pCSSort512;			// CS implementation to sort a number of 512 element sized arrays using a single dispatch
	ID3D11ComputeShader*			m_pCSSortInner512;		// CS implementation of the "down" pass from 512 to 1
	ID3D11ComputeShader*			m_pCSInitArgs;			// CS to write indirect args for Dispatch calls

	ID3D11Buffer*					m_pIndirectSortArgsBuffer;
	ID3D11UnorderedAccessView*		m_pIndirectSortArgsBufferUAV;
};
