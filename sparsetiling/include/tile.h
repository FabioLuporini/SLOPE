/*
 *  tile.h
 *
 */

#ifndef _TILE_H_
#define _TILE_H_

#include <vector>
#include <set>
#include <unordered_map>
#include <map>

#include "descriptor.h"
#include "parloop.h"

typedef std::vector<int> iterations_list;
typedef std::unordered_map<std::string, iterations_list*> mapname_iterations;
typedef std::pair<std::string, iterations_list*> mi_pair;

enum tile_region {LOCAL, EXEC_HALO, NON_EXEC_HALO};

typedef struct {
  /* number of parloops crossed by the tile */
  int crossedLoops;
  /* list of iterations owned by the tile, for each parloop */
  iterations_list** iterations;
  /* local indirection maps; for each loop crossed, there's one local map for each
   * global (i.e., parloop's) indirection map */
  mapname_iterations** localMaps;
  /* color of the tile */
  int color;
  /* a tile can either be local, exec_halo, or non_exec_halo */
  tile_region region;
} tile_t;

typedef std::vector<tile_t*> tile_list;

/*
 * Initialize a tile
 *
 * @param crossedLoops
 *   number of loops the tile crosses
 * @param region
 *   the iteration space region (local, exec_halo, non_exec_halo) in which the
 *   tile lives
 */
tile_t* tile_init (int crossedLoops, tile_region region = LOCAL);

/*
 * Distribute a loop iteration set to tiles
 *
 * @param tiles
 *   the list of tiles the iterations are added to
 * @param loop
 *   the tiled loop
 * @param iter2tile
 *   indirection array from iterations to tile identifiers
 */
void tile_assign_loop (tile_list* tiles, loop_t* loop, int* iter2tileMap);

/*
 * Retrieve a local map of a tile given a loop index and a map name
 *
 * @param tile
 *   the tile for which the map is retrieved
 * @param loopIndex
 *   the index of a loop crossed by tile
 * @param mapName
 *   name of the map to be retrieved
 * @return
 *   a reference to a local map of name mapName
 */
iterations_list& tile_get_local_map (tile_t* tile, int loopIndex, std::string mapName);

/*
 * Retrieve the iterations list of a tile for a given loop index
 *
 * @param tile
 *   the tile for which a list of iterations is retrieved
 * @param loopIndex
 *   the index of a loop crossed by tile the iterations belong to
 * @return
 *   a reference to an iterations list
 */
iterations_list& tile_get_iterations (tile_t* tile, int loopIndex);

/*
 * Free resources associated with the tile
 */
void tile_free (tile_t* tile);

#endif
