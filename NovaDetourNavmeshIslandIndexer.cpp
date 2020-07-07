// Fill out your copyright notice in the Description page of Project Settings.


#include "NovaDetourNavmeshIslandIndexer.h"

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

// Extended from: https://github.com/recastnavigation/recastnavigation/tree/05b2b8da80037887d3e79af8a59a8f0f8ed02602/RecastDemo/Source
// by Marvin Gouw 7/7/2020

#include "DetourNavMesh.h"
#include "DetourCommon.h"
#include "DetourAssert.h"
#include "DetourDebugDraw.h"
#include "Detour/DetourNavMeshQuery.h"
#include "Navmesh/RecastHelpers.h"
#include "DrawDebugHelpers.h"

constexpr bool DebugIslandIndexing = false;
constexpr bool DebugDrawIslands = false;

DECLARE_CYCLE_STAT(TEXT("Navmesh Island Indexing"), STAT_NovaIslandIndexing, STATGROUP_NovaStat);

class NavmeshFlags
{
	struct TileFlags
	{
		~TileFlags() { dtFree(flags); }
		// this is combined regeneration pass | flags, flags are less than 1<<8, regeneration pass is > (1<<8)
		uint16 * flags;
		uint8 lastRenderPass;
	};

	const dtNavMesh* m_nav;
	TileFlags* m_tiles;
	int m_ntiles;

public:
	uint8 nextFlagToSet = NOVA_POLYFLAGS_START;
	NavmeshFlags() :
		m_nav(0), m_tiles(0), m_ntiles(0)
	{
	}

	~NavmeshFlags()
	{
		for (int i = 0; i < m_ntiles; ++i)
			m_tiles[i].~TileFlags();
		dtFree(m_tiles);
	}

	bool init(const dtNavMesh* nav)
	{
		m_ntiles = nav->getMaxTiles();
		if (!m_ntiles)
			return true;
		if (m_ntiles > PURGE_MAX_TILES)
		{
			// warning message here to set this higher maybe:
			m_ntiles = PURGE_MAX_TILES;
		}

		m_tiles = (TileFlags*)dtAlloc(sizeof(TileFlags) * m_ntiles, DT_ALLOC_PERM);
		if (!m_tiles)
		{
			return false;
		}
		memset(m_tiles, 0, sizeof(TileFlags) * m_ntiles);

		// Alloc flags for each tile.
		for (int i = 0; i < m_ntiles; ++i)
		{
			const dtMeshTile* tile = nav->getTile(i);
			TileFlags* tf = &m_tiles[i];
			tf->flags = (uint16*)dtAlloc(MAX_POLY_PER_TILE * sizeof(uint16), DT_ALLOC_PERM);
			if (!tf->flags)
				return false;
			memset(tf->flags, 0, MAX_POLY_PER_TILE);
		}

		m_nav = nav;

		return false;
	}


	inline uint16 getFlags(dtPolyRef ref)
	{
		// Assume the ref is valid, no bounds checks.
		unsigned int salt, it, ip;
		m_nav->decodePolyId(ref, salt, it, ip);
		return m_tiles[it].flags[ip];
	}

	inline void setFlags(dtPolyRef ref, uint16 flags)
	{
		// Assume the ref is valid, no bounds checks.
		unsigned int salt, it, ip;
		m_nav->decodePolyId(ref, salt, it, ip);
		m_tiles[it].flags[ip] = flags;
	}

	inline uint8 getLastRenderPassForTile(const int tileIndex)
	{
		return m_tiles[tileIndex].lastRenderPass;
	}

	inline void setLastRenderPassForTile(const int tileIndex, const uint8 renderPass)
	{
		m_tiles[tileIndex].lastRenderPass = renderPass;
	}
};

void NovaDetourNavmeshIslandIndexer::FloodNavmesh(TSet< dtPolyRef> &polyRefsFlooded, AActor* Owner, const uint8 renderPass, dtPolyRef start, const int iteration)
{
	uint16 flag = 0;

	// reset open list
	int openIdx = 0;
	OpenList[openIdx] = start;
	openIdx++;
	FColor color = FColor::Turquoise;

	// start is always unknown since we're regenerating this so immediately put on pending list
	PendingList[0] = start;
	int pendingIdx = 1;
	CurrentNavFlags->setFlags(start, NOVA_POLYFLAGS_UNASSIGNED); // visited but unassigned

	while (openIdx > 0)
	{
		const dtPolyRef ref = OpenList[openIdx-1];
		--openIdx;

		// Get current poly and tile.
		// The API input has been checked already, skip checking internal data.
		const dtMeshTile* tile = 0;
		const dtPoly* poly = 0;

		NavMesh->getTileAndPolyByRefUnsafe(ref, &tile, &poly);
		/*if (NavMesh->getTileAndPolyByRef(ref, &tile, &poly) != DT_SUCCESS)
		{
			continue;
		}*/
		//NavMesh->getTileAndPolyByRefUnsafe(ref, &tile, &poly);

		// Visit linked polygons.
		for (unsigned int i = poly->firstLink; i != DT_NULL_LINK; i = tile->links[i].next)
		{
			const dtPolyRef neiRef = tile->links[i].ref;
			// Skip invalid and already visited.
			if (!neiRef)
				continue;

			// Check if this poly was visited
			const uint16 curFlag = CurrentNavFlags->getFlags(neiRef);
			if (curFlag)
			{
				// visited this pass
				if (curFlag & NOVA_POLYFLAGS_UNASSIGNED ||  // visited during this call of "Flood"
					curFlag & NOVA_POLYFLAGS_FLOODED) // this is for use in the outer function RegenerateTiles
				{
					continue;
				}

				// Is this a dirty tile? If not dirty, and flag is not set, set the flag for the rest of the flood to use
				unsigned int salt, it, ip;
				NavMesh->decodePolyId(neiRef, salt, it, ip);
				if (CurrentNavFlags->getLastRenderPassForTile(it) != renderPass) // not dirty?
				{
					if (!flag) // flag still not set
					{
						flag = curFlag & NOVA_POLYFLAGS_UNSET_FLOODED;
						if  constexpr (DebugIslandIndexing)
						{
							UE_LOG(LogNova, Log, TEXT("Old Island Index was found for flood %d"), flag);
							float scalarFlagPercent = ((flag & NOVA_POLYFLAGS_UNSET_FLOODED) / (float)30.f);
							scalarFlagPercent = FMath::Clamp(scalarFlagPercent, 0.f, 1.f);
							color = FColor::MakeRedToGreenColorFromScalar(scalarFlagPercent);
						}
					}
					continue;
				}
				// dirty tile and not visited then
			}

			// We're on a dirty tile and this poly is not yet visited
			// if flag is not yet known, put in pending list to go over later once we do know the flag
			if (flag == 0) 
			{
				PendingList[pendingIdx] = neiRef;
				pendingIdx++;
				CurrentNavFlags->setFlags(neiRef, NOVA_POLYFLAGS_UNASSIGNED); // visited but unassigned
			}
			else
			{
				// We already have a valid flag, just set it at this point
				// also mark with flooded so as we iterate through tile's polygons looking for a new starting point
				// we know this has been processed. RegenerateTiles will remove the flooded bit-flag after it is
				// done setting the actual poly-flag
				CurrentNavFlags->setFlags(neiRef, flag | NOVA_POLYFLAGS_FLOODED);

				if constexpr (DebugIslandIndexing)
				{
					if (!polyRefsFlooded.Contains(neiRef))
					{
						polyRefsFlooded.Add(neiRef);
					}
					else
					{
						unsigned int salt, it, ip;
						NavMesh->decodePolyId(PendingList[i], salt, it, ip);
						UE_LOG(LogNova, Error, TEXT("Navmesh Island Indexing: Poly Ref set to Flood twice in a row for tile: %d and poly: %d"), it, ip);
					}
					DrawDebugPolyRef(neiRef, Owner, color);
				}
			}

			// Visit neighbors
			if (openIdx == OpenMax)
			{
				// should log an error
				break;
			}

			// Add to next open list
			OpenList[openIdx] = neiRef;
			++openIdx;
		}
	}

	// If the flag is still not set, then everything went to pending, and we need to create a new unique island flag group
	if (!flag)
	{
		// increment this to the next flag group to use
		CurrentNavFlags->nextFlagToSet = CurrentNavFlags->nextFlagToSet >= NOVA_POLYFLAGS_UNIQUE_MAX ? NOVA_POLYFLAGS_STARTLOOP : CurrentNavFlags->nextFlagToSet + 1;
		flag = CurrentNavFlags->nextFlagToSet;
		if  constexpr (DebugIslandIndexing)
		{
			float scalarFlagPercent = ((flag & NOVA_POLYFLAGS_UNSET_FLOODED) / (float)30.f);
			scalarFlagPercent = FMath::Clamp(scalarFlagPercent, 0.f, 1.f);
			color = FColor::MakeRedToGreenColorFromScalar(scalarFlagPercent);
			UE_LOG(LogNova, Log, TEXT("New Island Index found during flood %d"), flag);
		}
	}

	// Set all the pending ones with the flag now, flag is guaranteed to be valid at this point
	for (int i = 0; i < pendingIdx; ++i)
	{
		// also mark with flooded so as we iterate through tile's polygons looking for a new starting point
		// we know this has been processed. RegenerateTiles will remove the flooded bit-flag after it is
		// done setting the actual poly-flag
		CurrentNavFlags->setFlags(PendingList[i], flag | NOVA_POLYFLAGS_FLOODED);
		if  constexpr (DebugIslandIndexing)
		{
			if (!polyRefsFlooded.Contains(PendingList[i]))
			{
				polyRefsFlooded.Add(PendingList[i]);
			}
			else
			{
				unsigned int salt, it, ip;
				NavMesh->decodePolyId(PendingList[i], salt, it, ip);
				UE_LOG(LogNova, Error, TEXT("Navmesh Island Indexing: Poly Ref added to Pending List twice for tile: %d and poly: %d"), it, ip);
			}
			DrawDebugPolyRef(PendingList[i], Owner, color);
		}
	}
}

void NovaDetourNavmeshIslandIndexer::DrawDebugPolyRef(const dtPolyRef neiRef, AActor* Owner, FColor color)
{
	if constexpr (DebugDrawIslands)
	{
		const dtMeshTile* drawtile = 0;
		const dtPoly* drawpoly = 0;
		if (NavMesh->getTileAndPolyByRef(neiRef, &drawtile, &drawpoly) == DT_SUCCESS)
		{
			if (drawpoly && drawtile)
			{
				TArray<FVector> verts;
				TArray<int32> indices;
				for (int vert = 0; vert < drawpoly->vertCount; ++vert)
				{
					const unsigned short vertIndex = drawpoly->verts[vert];
					const float* vertArray = &drawtile->verts[vertIndex * 3];
					const FVector unrealPoint = Recast2UnrealPoint(vertArray);
					verts.Add(unrealPoint);
				}

				// add triangle fan for render:
				for (int idx = 1; idx + 1 < verts.Num(); idx++)
				{
					indices.Add(0);
					indices.Add(idx);
					indices.Add(idx + 1);
				}
				DrawDebugMesh(Owner->GetWorld(), verts, indices, color, true, -1.f, 100);
			}
		}
	}
}

NovaDetourNavmeshIslandIndexer::NovaDetourNavmeshIslandIndexer() :
	CurrentNavFlags(0)
{
}

NovaDetourNavmeshIslandIndexer::~NovaDetourNavmeshIslandIndexer()
{
	Cleanup();
}


bool NovaDetourNavmeshIslandIndexer::Initialize(dtNavMesh* Mesh)
{
	NavMesh = Mesh;
	InitialUpdate = true;
	PendingList = (dtPolyRef*)dtAlloc(sizeof(dtPolyRef) * PendingMax, DT_ALLOC_PERM);
	if (!PendingList)
		return false;

	OpenList = (dtPolyRef*)dtAlloc(sizeof(dtPolyRef) * OpenMax, DT_ALLOC_PERM);
	if (!OpenList)
		return false;

	CurrentNavFlags = new NavmeshFlags();
	if (!CurrentNavFlags)
		return false;
	if (!CurrentNavFlags->init(Mesh))
		return false;

	return true;
}

void NovaDetourNavmeshIslandIndexer::Cleanup()
{
	delete CurrentNavFlags;
	CurrentNavFlags = 0;

	dtFree(PendingList);
	PendingList = 0;

	dtFree(OpenList);
	OpenList = 0;
}

// get the tiles regenerated and go through their polygons
void NovaDetourNavmeshIslandIndexer::OnRegenerateTiles(AActor* Owner, const TArray<uint32>& ChangedTiles)
{
	SCOPE_CYCLE_COUNTER(STAT_NovaIslandIndexing);
	// default state is dirty, so first renderpass needs to be 0
	static uint8 renderPass = 0;

	// If we are in initial update, we want to update ALL the tiles, so we don't care about what changed
	if (!InitialUpdate)
	{
		// Mark these tiles dirty, basically set renderPass to the current render pass. Anything using a stale renderpass is NOT dirty
		for (auto tileIndex : ChangedTiles)
		{
			CurrentNavFlags->setLastRenderPassForTile(tileIndex, renderPass);
		}
	}

	int floodPasses = 0;
	TSet< dtPolyRef> polyRefsFlooded;
	// Now loop through the tiles and flood fill all the polygons on the tile
	const int numTiles = InitialUpdate ? NavMesh->getMaxTiles() : ChangedTiles.Num();
	for (int i = 0; i < numTiles; ++i)
	{
		const int tileIndex = InitialUpdate ? i : ChangedTiles[i];
		const dtMeshTile* tile = ((const dtNavMesh*)NavMesh)->getTile(tileIndex);
		if (!tile->header) continue;

		const dtPolyRef base = NavMesh->getPolyRefBase(tile);
		// Loop through ALL polygons on the tile and flood fill each one, they may have been visited
		for (int j = 0; j < tile->header->polyCount; ++j)
		{
			const dtPolyRef ref = base | (unsigned int)j;
			uint16 flag = CurrentNavFlags->getFlags(ref);
			if (!(flag & NOVA_POLYFLAGS_FLOODED)) // not yet flooded, start a flood fill on this tile
			{
				// Flood fill guarantees the flag is set, and will also set flooded to true
				FloodNavmesh(polyRefsFlooded, Owner, renderPass, ref, floodPasses);
				floodPasses++;
				flag = CurrentNavFlags->getFlags(ref);
			}

			// DEBUG assert that the flag now has the flooded bit set

			// This next step overwrites all flags in the actual poly's polyflag
			// DEBUG to ensure we're not using the lower bits of poly flags anywhere, because this overrides it.
			// Can modify this function to not override the lower bits or to shift it up as well past whatever bits are being used
			// NavMesh->getPolyFlags(ref, &f);
			const uint16 set = flag & NOVA_POLYFLAGS_UNSET_FLOODED;
			CurrentNavFlags->setFlags(ref, set);
			NavMesh->setPolyFlags(ref, set);
			if constexpr (DebugIslandIndexing)
			{
				if (polyRefsFlooded.Remove(ref) == 0)
				{
					UE_LOG(LogNova, Error, TEXT("Island Indexing Error: Poly Ref flooded not found in previous flood."));
				}
			}
		}
	}

	// Some debug
	if constexpr (DebugIslandIndexing)
	{
		if (polyRefsFlooded.Num() > 0)
		{
			UE_LOG(LogNova, Error, TEXT("Flooded poly refs were not all processed during RegenerateTiles for Island Indexing"));
			for (auto ref : polyRefsFlooded)
			{
				unsigned int salt, it, ip;
				NavMesh->decodePolyId(ref, salt, it, ip);
				if (ChangedTiles.Contains(it) == false)
				{
					UE_LOG(LogNova, Error, TEXT("Flooded poly refs touched more tiles than was processed by RegenerateTiles. Extra tiles were not correctly updated. Tile %d"), it);
				}
			}
		}
	}
	// flood passes
	renderPass++;

	if (InitialUpdate)
		InitialUpdate = false;
}

/*
void NavMeshPruneTool::handleRender()
{
	duDebugDraw& dd = m_sample->getDebugDraw();

	if (m_hitPosSet)
	{
		const float s = m_sample->getAgentRadius();
		const unsigned int col = duRGBA(255, 255, 255, 255);
		dd.begin(DU_DRAW_LINES);
		dd.vertex(m_hitPos[0] - s, m_hitPos[1], m_hitPos[2], col);
		dd.vertex(m_hitPos[0] + s, m_hitPos[1], m_hitPos[2], col);
		dd.vertex(m_hitPos[0], m_hitPos[1] - s, m_hitPos[2], col);
		dd.vertex(m_hitPos[0], m_hitPos[1] + s, m_hitPos[2], col);
		dd.vertex(m_hitPos[0], m_hitPos[1], m_hitPos[2] - s, col);
		dd.vertex(m_hitPos[0], m_hitPos[1], m_hitPos[2] + s, col);
		dd.end();
	}

	const dtNavMesh* nav = m_sample->getNavMesh();
	if (m_flags && nav)
	{
		for (int i = 0; i < nav->getMaxTiles(); ++i)
		{
			const dtMeshTile* tile = nav->getTile(i);
			if (!tile->header) continue;
			const dtPolyRef base = nav->getPolyRefBase(tile);
			for (int j = 0; j < tile->header->polyCount; ++j)
			{
				const dtPolyRef ref = base | (unsigned int)j;
				if (m_flags->getFlags(ref))
				{
					duDebugDrawNavMeshPoly(&dd, *nav, ref, duRGBA(255, 255, 255, 128));
				}
			}
		}
	}

}*/
