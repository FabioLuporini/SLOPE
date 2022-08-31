/*
 * map.cpp
 *
 */

#include <algorithm>

#include <stdlib.h>

#include "map.h"
#include "utils.h"
#include "common.h"
#include <mpi.h>

#ifdef OP2

map_t* map (std::string name, set_t* inSet, set_t* outSet, int* values, int size, int dim)
{
  map_t* map = new map_t;

  map->name = name;
  map->inSet = inSet;
  map->outSet = outSet;
  map->values = values;
  map->size = size;
  map->offsets = NULL;
  map->dim = dim;
  map->mappedValues = NULL;
  map->mappedSize = 0;

  return map;
}

#else
map_t* map (std::string name, set_t* inSet, set_t* outSet, int* values, int size)
{
  map_t* map = new map_t;

  map->name = name;
  map->inSet = inSet;
  map->outSet = outSet;
  map->values = values;
  map->size = size;
  map->offsets = NULL;

  return map;
}
#endif

map_t* imap (std::string name, set_t* inSet, set_t* outSet, int* values, int* offsets)
{
  map_t* map = new map_t;

  map->name = name;
  map->inSet = inSet;
  map->outSet = outSet;
  map->values = values;
  // int offsetSize = inSet->size;
  // if(x2yMaxVal + 1 > offsetSize){
  //   offsetSize = x2yMaxVal + 1;
  // }
  // map->size = offsets[offsetSize];
  map->size = offsets[inSet->size];
  map->offsets = offsets;
  map->dim = -2;

  return map;
}

map_t* map_cpy (std::string name, map_t* map)
{
  map_t* new_map = new map_t;

  new_map->name = name;
  new_map->inSet = set_cpy (map->inSet);
  new_map->outSet = set_cpy (map->outSet);

  new_map->values = new int[map->size];
  std::copy (map->values, map->values + map->size, new_map->values);
  new_map->size = map->size;

  if (map->offsets) {
    new_map->offsets = new int[map->inSet->size];
    std::copy (map->offsets, map->offsets + map->inSet->size, new_map->offsets);
  }
}

void map_free (map_t* map, bool freeIndMap)
{
  if (! map) {
    return;
  }

  set_free (map->inSet);
  set_free (map->outSet);
  delete[] map->offsets;
  if (freeIndMap) {
    delete[] map->values;
  }
  delete map;
}

void map_ofs (map_t* map, int element, int* offset, int* size)
{
  ASSERT(element < map->inSet->size, "Invalid element passed to map_ofs");
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (map->offsets) {
    // imap case
    *size = map->offsets[element + 1] - map->offsets[element];
    *offset = map->offsets[element];
  }
  else {
    // size == mapArity
#ifdef OP2
    *size = (map->dim < 0) ? map->size / map->inSet->size : map->dim;
#else
    *size = map->size / map->inSet->size;
#endif
    *offset = element*(*size);
    if(rank == 2){
      // printf("map_ofs map=%s size=%d offset=%d inset=%s size=%d outset=%s size=%d\n",map->name.c_str(), *size, *offset, map->inSet->name.c_str(), map->inSet->size, map->outSet->name.c_str(), map->outSet->size);
    }
    
  }
}

int get_max_value(map_t* x2y){
  int maxVal = 0;
  for(int i = 0; i < x2y->size; i++){
    if(maxVal < x2y->values[i]){
      maxVal = x2y->values[i];
    }
  }
  return maxVal;
  // int ySize = x2y->outSet->size;
  // return (maxVal > ySize) ? maxVal : ySize;
}

map_t* map_invert (map_t* x2y, int* maxIncidence)
{
  // aliases
  int xSize = x2y->inSet->size;
  int ySize = x2y->outSet->size;
  int* x2yMap = x2y->values;
  int x2yMapSize = x2y->size;
  int x2yArity = -1;
#ifdef OP2
  if(x2y->dim < 0){
    x2yArity = x2yMapSize / xSize;
  }else{
    x2yArity = x2y->dim;
  }
#else
  x2yArity = x2yMapSize / xSize;
#endif

  int* y2xMap = new int[x2yMapSize];
  int* y2xOffset = new int[ySize + 1]();
  int incidence = 0;

  // compute the offsets in y2x
  // note: some entries in /x2yMap/ might be set to -1 to indicate that an element
  // in /x/ is on the boundary, and it is touching some off-processor elements in /y/;
  // so here we have to reset /y2xOffset[0]/ to 0
  for (int i = 0; i < x2yMapSize; i++) {
    y2xOffset[x2yMap[i] + 1]++;
  }
  y2xOffset[0] = 0;
  for (int i = 1; i < ySize + 1; i++) {
    y2xOffset[i] += y2xOffset[i - 1];
    incidence = MAX(incidence, y2xOffset[i] - y2xOffset[i - 1]);
  }

  // compute y2x
  int* inserted = new int[ySize + 1]();
  for (int i = 0; i < x2yMapSize; i += x2yArity) {
    for (int j = 0; j < x2yArity; j++) {
      int entry = x2yMap[i + j];
      if (entry == -1) {
        // as explained before, off-processor elements are ignored. In the end,
        // /y2xMap/ might just be slightly larger than strictly necessary
        continue;
      }
      y2xMap[y2xOffset[entry] + inserted[entry]] = i / x2yArity;
      inserted[entry]++;
    }
  }
  delete[] inserted;

  if (maxIncidence)
    *maxIncidence = incidence;
  return imap ("inverse_" + x2y->name, set_cpy(x2y->outSet), set_cpy(x2y->inSet),
               y2xMap, y2xOffset);
}


// map_t* map_invert (map_t* x2y, int* maxIncidence)
// {
//   // aliases
//   int xSize = x2y->inSet->size;
//   int x2yMaxVal = get_max_value(x2y);
//   int ySize = x2y->outSet->size;
  
//   if(ySize < x2yMaxVal + 1){
//     ySize = x2yMaxVal + 1;
//   }
//   int* x2yMap = x2y->values;
//   int x2yMapSize = x2y->size;
//   int x2yArity = -1;
// #ifdef OP2
//   if(x2y->dim == -1){
//     x2yArity = x2yMapSize / xSize;
//   }else{
//     x2yArity = x2y->dim;
//   }
// #else
//   x2yArity = x2yMapSize / xSize;
// #endif

//   int* y2xMap = new int[x2yMapSize];
//   int* y2xOffset = new int[ySize + 1]();
//   int incidence = 0;

//   int rank;
//   MPI_Comm_rank(MPI_COMM_WORLD, &rank);
//   if(rank == 1){
//     printf("map_invert map=%s inset=%s(size=%d) outset=%s(size=%d, prev=%d) mapsize=%d arity=%d\n", x2y->name.c_str(), x2y->inSet->name.c_str(), xSize, x2y->outSet->name.c_str(), ySize, x2y->outSet->size, x2yMapSize, x2yArity);
//   //     // PRINT_MAP(descMap);
//   //   // PRINT_INTARR(iter2tile->values, 0, seedLoopSetSize);

//   }

//   // compute the offsets in y2x
//   // note: some entries in /x2yMap/ might be set to -1 to indicate that an element
//   // in /x/ is on the boundary, and it is touching some off-processor elements in /y/;
//   // so here we have to reset /y2xOffset[0]/ to 0
//   for (int i = 0; i < x2yMapSize; i++) {
//     y2xOffset[x2yMap[i] + 1]++;
//   }
//   y2xOffset[0] = 0;
//   for (int i = 1; i < ySize + 1; i++) {
//     y2xOffset[i] += y2xOffset[i - 1];
//     incidence = MAX(incidence, y2xOffset[i] - y2xOffset[i - 1]);
//   }

//   // compute y2x
//   int* inserted = new int[ySize + 1]();
//   for (int i = 0; i < x2yMapSize; i += x2yArity) {
//     for (int j = 0; j < x2yArity; j++) {
//       int entry = x2yMap[i + j];
//       if (entry == -1) {
//         // as explained before, off-processor elements are ignored. In the end,
//         // /y2xMap/ might just be slightly larger than strictly necessary
//         continue;
//       }
//       y2xMap[y2xOffset[entry] + inserted[entry]] = i / x2yArity;
//       inserted[entry]++;
//     }
//   }
//   delete[] inserted;

//   if (maxIncidence)
//     *maxIncidence = incidence;
//   map_t* inv =  imap ("inverse_" + x2y->name, set_cpy(x2y->outSet), set_cpy(x2y->inSet),
//                y2xMap, y2xOffset, x2yMaxVal);
//   if(rank == 1){
//     // PRINT_MAP(x2y);
//     // PRINT_MAP(inv);
//     // printf("offsets:");
//     // PRINT_INTARR(inv->offsets, 0, ySize + 1);
//   }
//   return inv;
// }

// core | size | imp exec 0 | imp nonexec 0 | imp exec 1 | imp nonexec 1 |

void convert_map_vals_to_normal(map_t* map){

  set_t* outSet = map->outSet;
  int haloLevel = outSet->curHaloLevel;
  printf("convert_map_vals_to_normal map=%s size=%d\n", map->name.c_str(), map->size);
  int* unmappedVals = new int[map->size];
  for(int i = 0; i < map->size; i++){
    // check for non exec values
    int nonExecOffset = 0;
    for(int j = 0; j < haloLevel - 1; j++){
      nonExecOffset += outSet->nonExecSizes[j];
    }
    int execOffset = outSet->execSizes[haloLevel - 1];
    int totalOffset = outSet->setSize + execOffset + nonExecOffset;
    int mapVal = map->values[i];
    if(mapVal > totalOffset - 1){
      unmappedVals[i] = mapVal - nonExecOffset;
    }else{
    // check for exec values
      int execAvail = 0;
      for(int j = haloLevel - 1; j > 0; j--){
        nonExecOffset = 0;
        for(int k = 0; k < j; k ++){
          nonExecOffset += outSet->nonExecSizes[k];
        }
        execOffset = outSet->execSizes[j - 1];
        totalOffset = outSet->setSize + execOffset + nonExecOffset;
        if(mapVal > totalOffset - 1){
          unmappedVals[i] = mapVal - nonExecOffset;
          execAvail = 1;
          break;
        }
      }
      if(execAvail == 0){
        unmappedVals[i] = mapVal;
      }
    }
  }

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  if(rank == 0){
    // printf("map->values map=%s size=%d out=%s size=%d exec=%d,%d nonexec=%d,%d\n", map->name.c_str(), map->size, outSet->name.c_str(), outSet->setSize, outSet->execSizes[0], outSet->execSizes[1], outSet->nonExecSizes[0], outSet->nonExecSizes[1]);
    // printf("map->values map=%s size=%d\n", map->name.c_str(), map->size);
    // PRINT_INTARR(map->values, 0, map->size);
    // printf("unmappedVals map=%s size=%d\n", map->name.c_str(), map->size);
    // PRINT_INTARR(unmappedVals, 0, map->size);
  }

  map->mappedValues = new int[map->size];
  memcpy(map->mappedValues, map->values, map->size * sizeof(int));
  delete[] map->values;
  map->values = NULL;
  map->values = unmappedVals;
}

