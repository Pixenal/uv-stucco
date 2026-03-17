/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <stdint.h>

#include <cluster_tree_2d.h>

#include <io.h>
#include <quadtree.h>
#include <mesh.h>
#include <usg.h>

typedef struct AttributeDesc {
	char name[64];
	char type[2];
	I32 sizeInBits;
} AttributeDesc;

typedef struct StucMapInternal {
	UsgArr usgArr;
	Mesh *pMesh;
	BBox *pFaceBBoxes;
	TriCache triCache;
#ifdef STUC_QUADTREE_ENABLE
	QuadTree quadTree;
#endif
	ClutreTree clustTree;
	StucAttribIndexedArr indexedAttribs;
	V2_F32 zBounds;
	char *pName;
	char *pPath;
} MapFile;
