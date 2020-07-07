# DetourIslandIndexer
Extends detour navmesh to index separate islands
 
Extended from jakobbotsch's implementation for disabling polygons of the navmesh underneath objects.

This implementation does a flood on the navmesh and sets separate indices for each navmesh 'island'. Islands do not connect to one another. Checking the poly flag of two nav mesh polygons you can quickly tell if you can reach that polygon via another polygon. This implementation works on dynamically generation of navmesh and is fast. 
