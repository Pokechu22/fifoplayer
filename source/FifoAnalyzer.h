// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

//#include "FifoAnalyzer.h"

//#include "Core.h"

#include "VertexLoader.h"
#include "VertexLoader_Position.h"
#include "VertexLoader_Normal.h"
#include "VertexLoader_TextCoord.h"

struct CPMemory
{
	TVtxDesc vtxDesc;
	VAT vtxAttr[8];
	u32 arrayBases[16];
	u32 arrayStrides[16];
};
/*void Init()
{
	VertexLoader_Normal::Init();
	VertexLoader_Position::Init();
	VertexLoader_TextCoord::Init();
}*/

u8 ReadFifo8(u8 *&data)
{
	u8 value = data[0];
	data += 1;
	return value;
}

u16 ReadFifo16(u8 *&data)
{
	u16 value = be16toh(*(u16*)data);
	data += 2;
	return value;
}

u32 ReadFifo32(u8 *&data)
{
	u32 value = be32toh(*(u32*)data);
	data += 4;
	return value;
}

/*void InitBPMemory(BPMemory *bpMem)
{
	memset(bpMem, 0, sizeof(BPMemory));
	bpMem->bpMask = 0x00FFFFFF;
}

BPCmd DecodeBPCmd(u32 value, const BPMemory &bpMem)
{
	//handle the mask register
	int opcode = value >> 24;
	int oldval = ((u32*)&bpMem)[opcode];
	int newval = (oldval & ~bpMem.bpMask) | (value & bpMem.bpMask);
	int changes = (oldval ^ newval) & 0xFFFFFF;

	BPCmd bp = {opcode, changes, newval};

	return bp;
}

void LoadBPReg(const BPCmd &bp, BPMemory &bpMem)
{
	((u32*)&bpMem)[bp.address] = bp.newvalue;

	//reset the mask register
	if (bp.address != 0xFE)
		bpMem.bpMask = 0xFFFFFF;
}

void GetTlutLoadData(u32 &tlutAddr, u32 &memAddr, u32 &tlutXferCount, BPMemory &bpMem)
{
	tlutAddr = (bpMem.tmem_config.tlut_dest & 0x3FF) << 9;
	tlutXferCount = (bpMem.tmem_config.tlut_dest & 0x1FFC00) >> 5;

	// TODO - figure out a cleaner way.
	if (Core::g_CoreStartupParameter.bWii)
		memAddr = bpMem.tmem_config.tlut_src << 5;
	else
		memAddr = (bpMem.tmem_config.tlut_src & 0xFFFFF) << 5;
}
*/
void LoadCPReg(u32 subCmd, u32 value, CPMemory &cpMem)
{
	switch (subCmd & 0xF0)
	{
	case 0x50:
		// TODO: 0x50 and 0x60 still look wrong, they only have bitfields per byte reversed (not in the whole structure)
		cpMem.vtxDesc.Hex &= ~le64toh(0x1FFFF);  // keep the Upper bits
		cpMem.vtxDesc.Hex |= le64toh(value);
		break;

	case 0x60:
		cpMem.vtxDesc.Hex &= le64toh(0x1FFFF);  // keep the lower 17Bits
		cpMem.vtxDesc.Hex |= le64toh((u64)value * 131072);
		break;

	case 0x70:
//		_assert_((subCmd & 0x0F) < 8);
		cpMem.vtxAttr[subCmd & 7].g0.Hex = /*le32toh(*/value/*)*/;
		break;

	case 0x80:
//		_assert_((subCmd & 0x0F) < 8);
		cpMem.vtxAttr[subCmd & 7].g1.Hex = /*le32toh(*/value/*)*/;
		break;

	case 0x90:
//		_assert_((subCmd & 0x0F) < 8);
		cpMem.vtxAttr[subCmd & 7].g2.Hex = /*le32toh(*/value/*)*/;
		break;

	case 0xA0:
		// TODO: Endianness?
		cpMem.arrayBases[subCmd & 0xF] = le32toh(value);
		break;

	case 0xB0:
		// TODO: Endianness?
		cpMem.arrayStrides[subCmd & 0xF] = le32toh(value & 0xFF);
		printf("Be careful :p\n");
		break;
	}
}

void CalculateVertexElementSizes(int sizes[], int vatIndex, const CPMemory &cpMem)
{
	const TVtxDesc &vtxDesc = cpMem.vtxDesc;
	const VAT &vtxAttr = cpMem.vtxAttr[vatIndex];

	// Colors
	const u32 colDesc[2] = {vtxDesc.Color0, vtxDesc.Color1};
	const u32 colComp[2] = {vtxAttr.g0.Color0Comp, vtxAttr.g0.Color1Comp};

	const u32 tcElements[8] =
	{
		vtxAttr.g0.Tex0CoordElements, vtxAttr.g1.Tex1CoordElements, vtxAttr.g1.Tex2CoordElements, 
		vtxAttr.g1.Tex3CoordElements, vtxAttr.g1.Tex4CoordElements, vtxAttr.g2.Tex5CoordElements,
		vtxAttr.g2.Tex6CoordElements, vtxAttr.g2.Tex7CoordElements
	};

	const u32 tcFormat[8] =
	{
		vtxAttr.g0.Tex0CoordFormat, vtxAttr.g1.Tex1CoordFormat, vtxAttr.g1.Tex2CoordFormat, 
		vtxAttr.g1.Tex3CoordFormat, vtxAttr.g1.Tex4CoordFormat, vtxAttr.g2.Tex5CoordFormat,
		vtxAttr.g2.Tex6CoordFormat, vtxAttr.g2.Tex7CoordFormat
	};

	// Add position and texture matrix indices
	u64 vtxDescHex = cpMem.vtxDesc.Hex;
	for (int i = 0; i < 9; ++i)
	{
		sizes[i] = vtxDescHex & 1;
		vtxDescHex >>= 1;
	}

	// Position
	sizes[9] = VertexLoader_Position::GetSize(vtxDesc.Position, vtxAttr.g0.PosFormat, vtxAttr.g0.PosElements);

	// Normals
	if (vtxDesc.Normal != NOT_PRESENT)
	{
		sizes[10] = VertexLoader_Normal::GetSize(vtxDesc.Normal, vtxAttr.g0.NormalFormat, vtxAttr.g0.NormalElements, vtxAttr.g0.NormalIndex3);
	}
	else
	{
		sizes[10] = 0;
	}

	// Colors
	for (int i = 0; i < 2; i++)
	{
		int size = 0;

		switch (colDesc[i])
		{
		case NOT_PRESENT:
			break;
		case DIRECT:
			switch (colComp[i])
			{
			case FORMAT_16B_565:	size = 2; break;
			case FORMAT_24B_888:	size = 3; break;
			case FORMAT_32B_888x:	size = 4; break;
			case FORMAT_16B_4444:	size = 2; break;
			case FORMAT_24B_6666:	size = 3; break;
			case FORMAT_32B_8888:	size = 4; break;
			default: /*_assert_(0);*/ break;
			}
			break;
		case INDEX8:
			size = 1;
			break;
		case INDEX16:
			size = 2;
			break;
		}

		sizes[11 + i] = size;
	}

	// Texture coordinates
	vtxDescHex = vtxDesc.Hex >> 17;
	for (int i = 0; i < 8; i++)
	{
		sizes[13 + i] = VertexLoader_TextCoord::GetSize(vtxDescHex & 3, tcFormat[i], tcElements[i]);
		vtxDescHex >>= 2;
	}
}

u32 CalculateVertexSize(int vatIndex, const CPMemory &cpMem)
{
	u32 vertexSize = 0;

	int sizes[21];
	CalculateVertexElementSizes(sizes, vatIndex, cpMem);

	for (int i = 0; i < 21; ++i)
		vertexSize += sizes[i];

	return vertexSize;
}
