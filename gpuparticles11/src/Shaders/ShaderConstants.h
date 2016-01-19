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

// This file is shared between the HLSL and C++ code for convenience

// Fine-grained culling and rendering tile size
#define TILE_RES_X						32
#define TILE_RES_Y						32

// Maximum number of emitters supported
#define NUM_EMITTERS					4

// Maximum number of particles that can be stored per tile. Storing more will increase the amount of LDS and thus potentially reduce the number of waves in flight
#define NUM_PARTICLES_PER_TILE			1023

// The per-tile buffer size is the maximum number of particles that can be stored, plus another UINT to store the number of particles in that tile
#define PARTICLES_TILE_BUFFER_SIZE		(NUM_PARTICLES_PER_TILE+1)

// The number of threads in the coarse culling thread group
#define COARSE_CULLING_THREADS			256	// 512 and 1024 are fractionally slower