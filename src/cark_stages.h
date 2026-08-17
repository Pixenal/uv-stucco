#pragma once

#define STUC_STAGE_ISLAND_SPLIT_STRUCTS 4
#define STUC_STAGE_INFO_ISLAND_SPLIT (CarkStructInfoArr){\
	.size = STUC_STAGE_ISLAND_SPLIT_STRUCTS,\
	.pArr = (CarkStructInfo[STUC_STAGE_ISLAND_SPLIT_STRUCTS]){\
		{\
			.name = "face",\
			.desc = CARK_DESC_FACE,\
			.compCount = 3,\
			.pCompArr = (CarkCompInfo[]){\
				{\
					.name = "start",\
					.desc = CARK_COMP_DESC_IDX,\
					.type = CARK_TYPE_I32,\
					.refCount = 1,\
					.refArr = {\
						{\
							.stageIdx = -1,\
							.structIdx = 1\
						}/*TODO add uv ref back once supported in cark-vis*/\
					}\
				},\
				{\
					.name = "size",\
					.desc = CARK_COMP_DESC_SIZE,\
					.type = CARK_TYPE_I32\
				},\
				{\
					.name = "island",\
					.desc = CARK_COMP_DESC_ID,\
					.type = CARK_TYPE_I32,\
				}\
			}\
		},\
		{\
			.name = "corner",\
			.desc = CARK_DESC_CORNER,\
			.compCount = 1,\
			.pCompArr = &(CarkCompInfo){\
				.name = "vert",\
				.desc = CARK_COMP_DESC_IDX,\
				.type = CARK_TYPE_I32,\
				.refCount = 1,\
				.refArr = {\
					{\
						.stageIdx = -1,\
						.structIdx = 2,\
						.compIdx = -1\
					},\
				}\
			}\
		},\
		{\
			.name = "pos",\
			.desc = CARK_DESC_POS,\
			.compCount = 3,\
			.pCompArr = (CarkCompInfo[]) {\
				{\
					.name = "x",\
					.desc = CARK_COMP_DESC_VEC_X,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "y",\
					.desc = CARK_COMP_DESC_VEC_Y,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "z",\
					.desc = CARK_COMP_DESC_VEC_Z,\
					.type = CARK_TYPE_F32\
				}\
			}\
		},\
		{\
			.name = "uv",\
			.desc = CARK_DESC_UV,\
			.compCount = 2,\
			.pCompArr = (CarkCompInfo[]) {\
				{\
					.name = "u",\
					.desc = CARK_COMP_DESC_VEC_X,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "v",\
					.desc = CARK_COMP_DESC_VEC_Y,\
					.type = CARK_TYPE_F32\
				}\
			}\
		}\
	}\
}
#define STUC_STAGE_BUFMESH_INIT_STRUCTS 6
#define STUC_STAGE_INFO_BUFMESH_INIT (CarkStructInfoArr){\
	.size = STUC_STAGE_BUFMESH_INIT_STRUCTS,\
	.pArr = (CarkStructInfo[STUC_STAGE_BUFMESH_INIT_STRUCTS]){\
		{\
			.name = "face",\
			.desc = CARK_DESC_FACE,\
			.compCount = 3,\
			.pCompArr = (CarkCompInfo[]){\
				{\
					.name = "start",\
					.desc = CARK_COMP_DESC_IDX,\
					.type = CARK_TYPE_I32,\
					.refCount = 1,\
					.refArr = {\
						{\
							.stageIdx = -1,\
							.structIdx = 1\
						}\
					}\
				},\
				{\
					.name = "size",\
					.desc = CARK_COMP_DESC_SIZE,\
					.type = CARK_TYPE_I32\
				},\
				{\
					.name = "island",\
					.desc = CARK_COMP_DESC_ID,\
					.type = CARK_TYPE_I32\
				}\
			}\
		},\
		{\
			.name = "corner",\
			.desc = CARK_DESC_CORNER,\
			.compCount = 1,\
			.pCompArr = (CarkCompInfo[]){\
				{\
					.name = "vert",\
					.refCount = 1,\
					.refArr = {\
						{\
							.stageIdx = -1,\
							.structIdx = 2,\
							.compIdx = -1\
						}\
					},\
					.desc = CARK_COMP_DESC_IDX,\
					.type = CARK_TYPE_I32\
				}\
			}\
		},\
		{\
			.name = "intersect pos",\
			.desc = CARK_DESC_POS,\
			.compCount = 3,\
			.pCompArr = (CarkCompInfo[]) {\
				{\
					.name = "x",\
					.desc = CARK_COMP_DESC_VEC_X,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "y",\
					.desc = CARK_COMP_DESC_VEC_Y,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "z",\
					.desc = CARK_COMP_DESC_VEC_Z,\
					.type = CARK_TYPE_F32\
				}\
			}\
		},\
		{\
			.name = "on-edge pos",\
			.desc = CARK_DESC_POS,\
			.compCount = 3,\
			.pCompArr = (CarkCompInfo[]) {\
				{\
					.name = "x",\
					.desc = CARK_COMP_DESC_VEC_X,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "y",\
					.desc = CARK_COMP_DESC_VEC_Y,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "z",\
					.desc = CARK_COMP_DESC_VEC_Z,\
					.type = CARK_TYPE_F32\
				}\
			}\
		},\
		{\
			.name = "origin vert",\
			.desc = CARK_DESC_POS,\
			.compCount = 3,\
			.pCompArr = (CarkCompInfo[]) {\
				{\
					.name = "x",\
					.desc = CARK_COMP_DESC_VEC_X,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "y",\
					.desc = CARK_COMP_DESC_VEC_Y,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "z",\
					.desc = CARK_COMP_DESC_VEC_Z,\
					.type = CARK_TYPE_F32\
				}\
			}\
		},\
		{\
			.name = "overlap vert",\
			.desc = CARK_DESC_POS,\
			.compCount = 3,\
			.pCompArr = (CarkCompInfo[]) {\
				{\
					.name = "x",\
					.desc = CARK_COMP_DESC_VEC_X,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "y",\
					.desc = CARK_COMP_DESC_VEC_Y,\
					.type = CARK_TYPE_F32\
				},\
				{\
					.name = "z",\
					.desc = CARK_COMP_DESC_VEC_Z,\
					.type = CARK_TYPE_F32\
				}\
			}\
		},\
	}\
}