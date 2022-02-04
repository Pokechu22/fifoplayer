// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _CPMEMORY_H
#define _CPMEMORY_H

//#include "Common.h"

enum
{
	// These commands use the high nybble for the command itself, and the lower nybble is an argument.
	// TODO: However, Dolphin's implementation (in LoadCPReg) and YAGCD disagree about what values are
	// valid for the lower nybble.

	// YAGCD mentions 0x20 as "?", and does not mention the others
	// Libogc has 0x00 and 0x20, where 0x00 is tied to GX_ClearVCacheMetric and 0x20 related to
	// cpPerfMode. 0x10 may be GX_SetVCacheMetric, but that function is empty. In any case, these all
	// are probably for perf queries, and no title seems to actually need a full implementation.
	UNKNOWN_00 = 0x00,
	UNKNOWN_10 = 0x10,
	UNKNOWN_20 = 0x20,
	// YAGCD says 0x30 only; LoadCPReg allows any
	MATINDEX_A = 0x30,
	// YAGCD says 0x40 only; LoadCPReg allows any
	MATINDEX_B = 0x40,
	// YAGCD says 0x50-0x57 for distinct VCDs; LoadCPReg allows any for a single VCD
	VCD_LO = 0x50,
	// YAGCD says 0x60-0x67 for distinct VCDs; LoadCPReg allows any for a single VCD
	VCD_HI = 0x60,
	// YAGCD and LoadCPReg both agree that only 0x70-0x77 are valid
	CP_VAT_REG_A = 0x70,
	// YAGCD and LoadCPReg both agree that only 0x80-0x87 are valid
	CP_VAT_REG_B = 0x80,
	// YAGCD and LoadCPReg both agree that only 0x90-0x97 are valid
	CP_VAT_REG_C = 0x90,
	// YAGCD and LoadCPReg agree that 0xa0-0xaf are valid
	ARRAY_BASE = 0xa0,
	// YAGCD and LoadCPReg agree that 0xb0-0xbf are valid
	ARRAY_STRIDE = 0xb0,

	CP_COMMAND_MASK = 0xf0,
	CP_NUM_VAT_REG = 0x08,
	CP_VAT_MASK = 0x07,
	CP_NUM_ARRAYS = 0x10,
	CP_ARRAY_MASK = 0x0f,
};

// Vertex array numbers
enum
{
	ARRAY_POSITION	= 0,
	ARRAY_NORMAL	= 1,
	ARRAY_COLOR		= 2,
	ARRAY_COLOR2	= 3,
	ARRAY_TEXCOORD0	= 4,
};

// Vertex components
enum
{
	NOT_PRESENT = 0,
	DIRECT		= 1,
	INDEX8		= 2,
	INDEX16		= 3,
};

enum
{
	FORMAT_UBYTE		= 0,	// 2 Cmp
	FORMAT_BYTE			= 1,	// 3 Cmp
	FORMAT_USHORT		= 2,
	FORMAT_SHORT		= 3,
	FORMAT_FLOAT		= 4,
};

enum
{
	FORMAT_16B_565		= 0,	// NA
	FORMAT_24B_888		= 1,	
	FORMAT_32B_888x		= 2,
	FORMAT_16B_4444		= 3,
	FORMAT_24B_6666		= 4,
	FORMAT_32B_8888		= 5,
};

enum
{
	VAT_0_FRACBITS = 0x3e0001f0,
	VAT_1_FRACBITS = 0x07c3e1f0,
	VAT_2_FRACBITS = 0xf87c3e1f,
};

#pragma pack(4)
union TVtxDesc
{
	u64 Hex;
	struct
	{
// Reversed
#if BYTE_ORDER == BIG_ENDIAN
		u32				:31;
		u32 Tex7Coord	: 2;
		u32 Tex6Coord	: 2;
		u32 Tex5Coord	: 2;
		u32 Tex4Coord	: 2;
		u32 Tex3Coord	: 2;
		u32 Tex2Coord	: 2;
		u32 Tex1Coord	: 2;
		u32 Tex0Coord	: 2;
		u32 Color1		: 2;
		u32 Color0		: 2;
		u32 Normal		: 2;
		u32 Position	: 2;
		u32 Tex7MatIdx	: 1;
		u32 Tex6MatIdx	: 1;
		u32 Tex5MatIdx	: 1;
		u32 Tex4MatIdx	: 1;
		u32 Tex3MatIdx	: 1;
		u32 Tex2MatIdx	: 1;
		u32 Tex1MatIdx	: 1;
		u32 Tex0MatIdx	: 1;
		u32 PosMatIdx	: 1;
#elif BYTE_ORDER == LITTLE_ENDIAN
		u32 PosMatIdx	: 1;
		u32 Tex0MatIdx	: 1;
		u32 Tex1MatIdx	: 1;
		u32 Tex2MatIdx	: 1;
		u32 Tex3MatIdx	: 1;
		u32 Tex4MatIdx	: 1;
		u32 Tex5MatIdx	: 1;
		u32 Tex6MatIdx	: 1;
		u32 Tex7MatIdx	: 1;

		u32 Position	: 2;
		u32 Normal		: 2;
		u32 Color0		: 2;
		u32 Color1		: 2;
		u32 Tex0Coord	: 2;
		u32 Tex1Coord	: 2;
		u32 Tex2Coord	: 2;
		u32 Tex3Coord	: 2;
		u32 Tex4Coord	: 2;
		u32 Tex5Coord	: 2;
		u32 Tex6Coord	: 2;
		u32 Tex7Coord	: 2;
		u32				:31;
#else
#error endianness not defined
#endif
	};

	struct
	{
		u32 Hex0, Hex1;
	};
	u8 byte[8];
};

union UVAT_group0
{
	u32 Hex;
	struct
	{
// TODO: Is it necessary to reverse the bitfield order? oO
#if BYTE_ORDER == BIG_ENDIAN
		// 25:31
		u32 NormalIndex3		: 1;
		u32 ByteDequant			: 1;
		u32 Tex0Frac			: 5;

		// 17:24
		u32 Tex0CoordFormat		: 3;
		u32 Tex0CoordElements	: 1;
		u32 Color1Comp			: 3; 
		u32 Color1Elements		: 1;

		// 9:16
		u32 Color0Comp			: 3; 
		u32 Color0Elements		: 1;
		u32 NormalFormat		: 3; 
		u32 NormalElements		: 1; 

		// 0:8
		u32 PosFrac				: 5; 
		u32 PosFormat			: 3; 
		u32 PosElements			: 1;
#elif BYTE_ORDER == LITTLE_ENDIAN
        // 1:8
        u32 PosElements         : 1;
        u32 PosFormat           : 3; 
        u32 PosFrac             : 5; 
        // 9:12
        u32 NormalElements      : 1; 
        u32 NormalFormat        : 3; 
        // 13:16
        u32 Color0Elements      : 1;
        u32 Color0Comp          : 3; 
        // 17:20
        u32 Color1Elements      : 1;
        u32 Color1Comp          : 3; 
        // 21:29
        u32 Tex0CoordElements   : 1;
        u32 Tex0CoordFormat     : 3;
        u32 Tex0Frac            : 5;
        // 30:31
        u32 ByteDequant         : 1;
        u32 NormalIndex3        : 1;
#else
#error endianness not defined
#endif
	};
	u8 byte[4];
};

union UVAT_group1
{
	u32 Hex;
	struct 
	{
#if BYTE_ORDER == BIG_ENDIAN
// Reversed

		// 
		u32						: 1;

		// 27:30
		u32 Tex4CoordFormat		: 3;
		u32 Tex4CoordElements	: 1;

		// 18:26
		u32 Tex3Frac			: 5;
		u32 Tex3CoordFormat		: 3;
		u32 Tex3CoordElements	: 1;

		// 9:17
		u32 Tex2Frac			: 5;
		u32 Tex2CoordFormat		: 3;
		u32 Tex2CoordElements	: 1;

		// 0:8
		u32 Tex1Frac			: 5;
		u32 Tex1CoordFormat		: 3;
		u32 Tex1CoordElements	: 1;

#elif BYTE_ORDER == LITTLE_ENDIAN
		// 0:8
		u32 Tex1CoordElements	: 1;
		u32 Tex1CoordFormat		: 3;
		u32 Tex1Frac			: 5;
		// 9:17
		u32 Tex2CoordElements	: 1;
		u32 Tex2CoordFormat		: 3;
		u32 Tex2Frac			: 5;
		// 18:26
		u32 Tex3CoordElements	: 1;
		u32 Tex3CoordFormat		: 3;
		u32 Tex3Frac			: 5;
		// 27:30
		u32 Tex4CoordElements	: 1;
		u32 Tex4CoordFormat		: 3;
		// 
		u32						: 1;
#else
#error endianness not defined
#endif
	};
};

union UVAT_group2
{
	u32 Hex;
	struct 
	{
// reversed

#if BYTE_ORDER == BIG_ENDIAN
		// 23:31
		u32 Tex7Frac			: 5;
		u32 Tex7CoordFormat		: 3;
		u32 Tex7CoordElements	: 1;
		// 14:22
		u32 Tex6Frac			: 5;
		u32 Tex6CoordFormat		: 3;
		u32 Tex6CoordElements	: 1;
		// 5:13
		u32 Tex5Frac			: 5;
		u32 Tex5CoordFormat		: 3;
		u32 Tex5CoordElements	: 1;
		// 0:4
		u32 Tex4Frac			: 5;
#elif BYTE_ORDER == LITTLE_ENDIAN
		// 0:4
		u32 Tex4Frac			: 5;
		// 5:13
		u32 Tex5CoordElements	: 1;
		u32 Tex5CoordFormat		: 3;
		u32 Tex5Frac			: 5;
		// 14:22
		u32 Tex6CoordElements	: 1;
		u32 Tex6CoordFormat		: 3;
		u32 Tex6Frac			: 5;
		// 23:31
		u32 Tex7CoordElements	: 1;
		u32 Tex7CoordFormat		: 3;
		u32 Tex7Frac			: 5;
#else
#error endianness not defined
#endif
	};
};

struct ColorAttr
{
	u8 Elements;
	u8 Comp;
};

struct TexAttr
{
	u8 Elements;
	u8 Format;
	u8 Frac;
};

struct TVtxAttr
{
	u8 PosElements;
	u8 PosFormat; 
	u8 PosFrac; 
	u8 NormalElements;
	u8 NormalFormat; 
	ColorAttr color[2];
	TexAttr texCoord[8];
	u8 ByteDequant;
	u8 NormalIndex3;
};

// Matrix indices
union TMatrixIndexA
{
	// TODO: Bit order!

	struct
	{
		u32 PosNormalMtxIdx : 6;
		u32 Tex0MtxIdx : 6;
		u32 Tex1MtxIdx : 6;
		u32 Tex2MtxIdx : 6;
		u32 Tex3MtxIdx : 6;
	};
	struct
	{
		u32 Hex : 30;
		u32 unused : 2;
	};
};

union TMatrixIndexB
{
	// TODO: Bit order!
	struct
	{
		u32 Tex4MtxIdx : 6;
		u32 Tex5MtxIdx : 6;
		u32 Tex6MtxIdx : 6;
		u32 Tex7MtxIdx : 6;
	};
	struct
	{
		u32 Hex : 24;
		u32 unused : 8;
	};
};

#pragma pack()

extern u32 arraybases[16];
extern u8 *cached_arraybases[16];
extern u32 arraystrides[16];
extern TMatrixIndexA MatrixIndexA;
extern TMatrixIndexB MatrixIndexB;

struct VAT
{
	UVAT_group0 g0;
	UVAT_group1 g1;
	UVAT_group2 g2;
};

extern TVtxDesc g_VtxDesc;
extern VAT g_VtxAttr[8];

// Might move this into its own file later.
//void LoadCPReg(u32 SubCmd, u32 Value);

// Fills memory with data from CP regs
//void FillCPMemoryArray(u32 *memory);

#endif // _CPMEMORY_H
