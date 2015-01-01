/*
 *  inspector.cpp
 *
 * Implement inspector routines
 */

#include <string>

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "inspector.h"
#include "utils.h"
#include "partitioner.h"
#include "coloring.h"
#include "tiling.h"

using namespace std;

inspector_t* insp_init (int avgTileSize, insp_strategy strategy)
{
  inspector_t* insp = new inspector_t;

  insp->strategy = strategy;
  insp->avgTileSize = avgTileSize;
  insp->loops = new loop_list;

  insp->seed = -1;
  insp->iter2tile = NULL;
  insp->iter2color = NULL;
  insp->tiles = NULL;

  return insp;
}

insp_info insp_add_parloop (inspector_t* insp, std::string name, set_t* set,
                            desc_list* descriptors)
{
  ASSERT(insp != NULL, "Invalid NULL pointer to inspector");

  loop_t* loop = new loop_t;
  loop->name = name;
  loop->set = set;
  loop->index = insp->loops->size();
  loop->descriptors = descriptors;
  loop->coloring = NULL;
  loop->tiling = NULL;

  insp->loops->push_back(loop);

  return INSP_OK;
}

insp_info insp_run (inspector_t* insp, int seed)
{
  ASSERT(insp != NULL, "Invalid NULL pointer to inspector");

  insp->seed = seed;

  // aliases
  insp_strategy strategy = insp->strategy;
  int avgTileSize = insp->avgTileSize;
  loop_list* loops = insp->loops;
  int nLoops = loops->size();
  loop_t* baseLoop = loops->at(seed);
  std::string baseLoopSetName = baseLoop->set->setName;
  int baseLoopSetSize = baseLoop->set->size;

  ASSERT((seed >= 0) && (seed < nLoops), "Invalid tiling start point");
  ASSERT(! baseLoop->set->isSubset, "Seed loop cannot be a subset");

  // partition the iteration set of the base loop and create empty tiles
  map_t* iter2tile = partition (baseLoop, avgTileSize);
  int nTiles = iter2tile->outSet->size;
  tile_list* tiles = new tile_list (nTiles);
  for (int i = 0; i < nTiles; i++) {
    tiles->at(i) = tile_init (nLoops);
  }
  tile_assign_loop (tiles, seed, iter2tile->inSet->size, iter2tile->indMap);

  // color the base loop's sets
  map_t* iter2color;
  switch (strategy) {
    case SEQUENTIAL: case MPI:
      iter2color = color_sequential (iter2tile, tiles);
      break;
    case OMP: case OMP_MPI:
      iter2color = color_kdistance (loops, seed, iter2tile, tiles);
      break;
  }

  // track information essential for tiling, execution, and debugging
  insp->iter2tile = iter2tile;
  insp->iter2color = iter2color;
  insp->tiles = tiles;

  // create copies of initial tiling and coloring, cause they can be manipulated
  // during one phase of tiling (e.g. forward), so they need to be reset to their
  // original values before the other tiling phase (e.g. backward)
  int* tmpIter2tileMap = new int[baseLoopSetSize];
  int* tmpIter2colorMap = new int[baseLoopSetSize];
  memcpy (tmpIter2tileMap, iter2tile->indMap, sizeof(int)*baseLoopSetSize);
  memcpy (tmpIter2colorMap, iter2color->indMap, sizeof(int)*baseLoopSetSize);

  // tile the loop chain. First forward, then backward. The algorithm is as follows:
  // 1- start from the base loop, then go forward (backward)
  // 2- make a projection of the dependencies for tiling the subsequent loop
  // 3- tile the subsequent loop, using the projection
  // 4- go back to point 2, and repeat till there are loop along the direction

  // prepare for forward tiling
  loop_t* prevTiledLoop = baseLoop;
  projection_t* baseLoopProj = new projection_t (&iter2tc_cmp);
  projection_t* prevLoopProj = new projection_t (&iter2tc_cmp);
  iter2tc_t* seedTilingInfo = iter2tc_init (baseLoopSetName, baseLoopSetSize,
                                            tmpIter2tileMap, tmpIter2colorMap);
  iter2tc_t* prevTilingInfo = iter2tc_cpy (seedTilingInfo);

  // forward tiling
  for (int i = seed + 1; i < nLoops; i++) {
    loop_t* curLoop = loops->at(i);
    iter2tc_t* curTilingInfo;

    // compute projection from i-1 for tiling loop i
    project_forward (prevTiledLoop, prevTilingInfo, prevLoopProj, baseLoopProj);

    // tile loop i as going forward
    curTilingInfo = tile_forward (curLoop, prevLoopProj);
    tile_assign_loop (tiles, i, curTilingInfo->itSetSize, curTilingInfo->iter2tile);

    // prepare for next iteration
    prevTiledLoop = curLoop;
    prevTilingInfo = curTilingInfo;
  }

  // prepare for backward tiling
  iter2tc_free (prevTilingInfo);
  projection_free (prevLoopProj);
  prevLoopProj = new projection_t (&iter2tc_cmp);
  prevTiledLoop = baseLoop;
  prevTilingInfo = seedTilingInfo;

  // backward tiling
  for (int i = seed - 1; i >= 0; i--) {
    // compute projection from i+1 for tiling loop i

    // tile loop i as going backward

  }

  // free memory
  iter2tc_free (prevTilingInfo);
  projection_free (prevLoopProj);
  projection_free (baseLoopProj);

  return INSP_OK;
}

static void print_tiled_loop (tile_list* tiles, int loopIndex, int verbosityTiles);

void insp_print (inspector_t* insp, insp_verbose level)
{
  ASSERT(insp != NULL, "Invalid NULL pointer to inspector");

  // aliases
  loop_list* loops = insp->loops;
  map_t* iter2tile = insp->iter2tile;
  map_t* iter2color = insp->iter2color;
  tile_list* tiles = insp->tiles;
  int seed = insp->seed;
  int avgTileSize = insp->avgTileSize;
  int nTiles = tiles->size();
  int nLoops = loops->size();
  int itSetSize = loops->at(seed)->set->size;

  // set verbosity level
  int verbosityItSet, verbosityTiles;
  switch (level) {
    case LOW:
      verbosityItSet = MIN(LOW, itSetSize);
      verbosityTiles = avgTileSize / 2;
      break;
    case MEDIUM:
      verbosityItSet = MIN(MEDIUM, itSetSize);
      verbosityTiles = avgTileSize;
      break;
    case HIGH:
      verbosityItSet = itSetSize;
      verbosityTiles = INT_MAX;
  }

  cout << endl << ":: Inspector info ::" << endl << endl;
  if (loops) {
    cout << "Number of loops: " << nLoops << ", base loop: " << seed << endl;
  }
  else {
    cout << "No loops specified" << endl;
  }
  cout << "Number of tiles: " << nTiles << endl;
  cout << "Average tile size: " << avgTileSize << endl;
  if (iter2tile && iter2color) {
    cout << endl << "Printing partioning of the base loop's iteration set:" << endl;
    cout << "  Iteration  |  Tile |  Color" << endl;
    for (int i = 0; i < verbosityItSet; i++) {
      cout << "         " << i
           << "   |   " << iter2tile->indMap[i]
           << "   |   " << iter2color->indMap[i] << endl;
    }
    if (verbosityItSet < itSetSize) {
      cout << "         ..." << endl;
      cout << "         " << itSetSize - 1
           << "   |   " << iter2tile->indMap[itSetSize - 1]
           << "   |   " << iter2color->indMap[itSetSize - 1] << endl;
    }
  }
  else {
    cout << "No partitioning of the base loop performed" << endl;
  }

  if (tiles) {
    cout << endl << "Printing tiles' base loop iterations" << endl;
    print_tiled_loop (tiles, seed, verbosityTiles);
    if (seed + 1 < nLoops) {
      cout << endl << "Printing result of forward tiling..." << endl;
      for (int i = seed + 1; i < nLoops; i++) {
        cout << "  Loop " << i << " - " << loops->at(i)->name << endl;
        print_tiled_loop (tiles, i, verbosityTiles);
      }
    }
    else {
      cout << endl << "No forward tiling (seed loop is loop chain's top)" << endl;
    }
    cout << endl << "Printing result of backward tiling..." << endl;
    if (0 <= seed - 1) {
      for (int i = seed - 1; i >= 0; i--) {
        cout << "  Loop " << i << " - " << loops->at(i)->name << endl;
        print_tiled_loop (tiles, i, verbosityTiles);
      }
    }
    else {
      cout << endl << "No backward tiling (seed loop is loop chain's bottom)" << endl;
    }
  }
}

void insp_free (inspector_t* insp)
{
  // Note that tiles are not freed because they are already freed in the
  // executor's free function

  // aliases
  loop_list* loops = insp->loops;

  // delete tiled loops, access descriptors, maps, and sets
  // freed data structures are tracked so that freeing twice the same pointer
  // is avoided
  desc_list deletedDescs;
  map_list deletedMaps;
  set_list deletedSets;
  loop_list::const_iterator lIt, lEnd;
  for (lIt = loops->begin(), lEnd = loops->end(); lIt != lEnd; lIt++) {
    desc_list* descriptors = (*lIt)->descriptors;
    desc_list::const_iterator dIt, dEnd;
    for (dIt = descriptors->begin(), dEnd = descriptors->end(); dIt != dEnd; dIt++) {
      descriptor_t* desc = *dIt;
      if (deletedDescs.find(desc) == deletedDescs.end()) {
        // found an access descriptor to be freed
        // now check if its map and sets still need be freed
        map_t* map = desc->map;
        desc->map = NULL;
        if (map != DIRECT) {
          if (deletedMaps.find(map) == deletedMaps.end()) {
            // map still to be freed, so its sets might have to be freed as well
            set_t* inSet = map->inSet;
            set_t* outSet = map->outSet;
            map->inSet = NULL;
            map->outSet = NULL;
            if (deletedSets.find(inSet) == deletedSets.end()) {
              // set still to be freed
              set_free (inSet);
              deletedSets.insert (inSet);
            }
            if (deletedSets.find(outSet) == deletedSets.end()) {
              // set still to be freed
              set_free (outSet);
              deletedSets.insert (outSet);
            }
            map_free (map);
            deletedMaps.insert (map);
          }
        }
        desc_free (desc);
        deletedDescs.insert (desc);
      }
    }
    // delete loops
#ifndef VTKON
    delete[] (*lIt)->tiling;
    delete[] (*lIt)->coloring;
#endif
    delete *lIt;
  }

  delete insp->loops;

  map_free (insp->iter2tile, true);
  map_free (insp->iter2color, true);
  delete insp;
}

/***** Static / utility functions *****/

static void print_tiled_loop (tile_list* tiles, int loopIndex, int verbosityTiles)
{
  // aliases
  int nTiles = tiles->size();

  cout << "       Tile  |  Color  |    Iterations " << endl;
  for (int i = 0; i < nTiles; i++) {
    int tileSize = tiles->at(i)->iterations[loopIndex]->size();
    int range = MIN(tileSize, verbosityTiles);
    cout << "         " << i << "   |    " << tiles->at(i)->color << "    |   {";
    if (tileSize == 0) {
      cout << "No iterations}" << endl;
      continue;
    }
    cout << tiles->at(i)->iterations[loopIndex]->at(0);
    for (int j = 1; j < range; j++) {
      cout << ", " << tiles->at(i)->iterations[loopIndex]->at(j);
    }
    if (tileSize > verbosityTiles) {
      int lastIterID = tiles->at(i)->iterations[loopIndex]->at(tileSize - 1);
      cout << "..., " << lastIterID;
    }
    cout << "}" << endl;
  }
}
