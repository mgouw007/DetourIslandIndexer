# DetourIslandIndexer
Extends detour navmesh to index separate islands
 
Extended from jakobbotsch's implementation for disabling polygons of the navmesh underneath objects.

This implementation does a flood on the navmesh and sets separate indices for each navmesh 'island'. Islands do not connect to one another.

By Checking the poly flags of two nav mesh polygons, you can quickly tell if you can reach that polygon via another polygon.

This implementation works on dynamic generation of navmesh and is fast. Hook up regenerateTiles on callbacks from Navmesh regeneration. Make sure you set the memory limits to work well with your game's use of the nav mesh.

Tested to work with Unreal Engine's Detour navmesh
