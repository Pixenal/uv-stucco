/* 
SPDX-FileCopyrightText: 2025 Caleb Dawson
SPDX-License-Identifier: Apache-2.0
*/

#pragma once
#include <stdint.h>

#include <cluster_tree_2d.h>

#include <mesh.h>
#include <usg.h>

typedef struct StucMap {
	UsgArr usgArr;
	Mesh *pMesh;
	PixmshV2Bb *pFaceBBoxes;
	TriCache triCache;
	ClutreTree clustTree;
	StucAttribIndexedArr indexedAttribs;
	V2_F32 zBounds;
	char *pName;
	char *pPath;
} StucMap;
