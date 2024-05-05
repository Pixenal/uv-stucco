#pragma once
#include <Types.h>
#include <RUVM.h>
#include <EnclosingCells.h>

void ruvmMapToSingleFace(MappingJobVars *pArgs, EnclosingCells *pEcVars,
                         DebugAndPerfVars *pDpVars, V2_F32 fTileMin, int32_t tile, FaceInfo baseFace);
