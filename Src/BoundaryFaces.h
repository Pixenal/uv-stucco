#pragma once
#include <RUVM.h>
#include <Types.h>

void ruvmMergeBoundaryFaces(RuvmContext pContext, RuvmMap pMap, RuvmMesh *pMeshOut,
                            SendOffArgs *pJobArgs, EdgeVerts *pEdgeVerts);
