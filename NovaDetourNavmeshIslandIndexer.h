// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

// This implementation does a flood on the nav-mesh and sets separate indices for each nav-mesh 'island'.
// Islands do not connect to one another.Checking the poly flag of two nav-mesh polygons you can quickly tell
// if you can reach that polygon via another polygon.This implementation works on dynamically generation of nav-mesh and is fast.

// Extended from: https://github.com/recastnavigation/recastnavigation/tree/05b2b8da80037887d3e79af8a59a8f0f8ed02602/RecastDemo/Source
// by Marvin Gouw 7/7/2020

// Prune navmesh to accessible locations from a point.

// 255 allowed poly island different types
#define NOVA_POLYFLAGS_START 0
#define NOVA_POLYFLAGS_STARTLOOP 16 // start reseting the poly-flag identifier here, anything after this flag value is probably a small island
#define NOVA_POLYFLAGS_UNIQUE_MAX 255 // guaranteed to be unique poly-flag identifiers if there are less than this many islands. If more, it repeats and reuses indexes starting from NOVA_POLYFLAGS_STARTLOOP

#define NOVA_POLYFLAGS_UNSET_FLOODED (0xff) // lower 8 bits
#define NOVA_POLYFLAGS_FLOODED (0x1 << 9)	// bit-flag for internal use while iterating, should be bigger than unique_max
#define NOVA_POLYFLAGS_UNASSIGNED (0x1 << 10) // bit-flag for internal use while iterating

// TUNE these to work with your game, max poly may be larger, and we don't actually check so things may crash!
#define MAX_POLY_PER_TILE 128
#define PURGE_MAX_TILES 16384 // sync with recast's max tiles

class NovaDetourNavmeshIslandIndexer
{
protected:
	class NavmeshFlags* CurrentNavFlags;
	dtNavMesh* NavMesh;
	dtPolyRef* PendingList;
	dtPolyRef* OpenList;
	const int PendingMax = 1024;
	const int OpenMax = 1024; // for flood iteration
	bool InitialUpdate = true;
public:
	NovaDetourNavmeshIslandIndexer();
	virtual ~NovaDetourNavmeshIslandIndexer();

	virtual bool Initialize(dtNavMesh* Mesh);
	virtual void Cleanup();
	virtual void OnRegenerateTiles(AActor* Owner, const TArray<uint32>& ChangedTiles);
	virtual void FloodNavmesh(TSet< dtPolyRef>& polyRefsFlooded, AActor* Owner, const uint8 renderPass, dtPolyRef start, const int iteration);
	virtual void DrawDebugPolyRef(const dtPolyRef neiRef, AActor* Owner, FColor color);


protected:
	// Explicitly disabled copy constructor and copy assignment operator.
	NovaDetourNavmeshIslandIndexer(const NovaDetourNavmeshIslandIndexer&);
	NovaDetourNavmeshIslandIndexer& operator=(const NovaDetourNavmeshIslandIndexer&);
};
