#define ENABLE_CONSOLE 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>
#include <machine/endian.h>
#include <malloc.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <unistd.h>
#include <fat.h>
#include <dirent.h>
#include <network.h>
#include <cmath>

#include <png.h>
#include <zlib.h>

#include "protocol.h"
#include "BPMemory.h"
#include "DffFile.h"
#include "FifoDataFile.h"
#include "OpcodeDecoding.h"
#include "FifoAnalyzer.h"
#include "memory_manager.h"

#include "VideoInterface.h"

#define EFB_WIDTH 640
#define EFB_HEIGHT 528

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

f32 g_view_scale = 1.0f;
s32 g_x_offset = 0;
s32 g_y_offset = 0;

int g_screenshot_number = 1;

static vu16* const _viReg = (u16*)0xCC002000;
using namespace VideoInterface;

u32 TransformCPReg(u8 reg, u32 data, CPMemory& target_cpmem);
u32 TransformBPReg(u8 reg, u32 data, const FifoData& fifo_data);

void ApplyInitialState(const FifoData& fifo_data, CPMemory& target_cpmem, const std::vector<AnalyzedFrameInfo>& analyzed_frames)
{
	for (const FifoFrameData& frame : fifo_data.frames)
	{
		for (const DffMemoryUpdate& update : frame.memoryUpdates)
		{
			PrepareMemoryLoad(update.address, update.dataSize);
			//if (early_mem_updates)
			//	memcpy(GetPointer(update.address), &update.data[0], update.data.size());
			//DCFlushRange(GetPointer(update.address), update.dataSize);
		}
	}

	// Run through frames and find EFB copies, to prepare for their memory loads
	for (u32 cur_frame = 0; cur_frame < fifo_data.frames.size(); cur_frame++)
	{
		const FifoFrameData& frame = fifo_data.frames[cur_frame];
		const AnalyzedFrameInfo& cur_analyzed_frame = analyzed_frames[cur_frame];
		for (auto& cur_object : cur_analyzed_frame.objects)
		{
			for (const u32 cmd_start : cur_object.cmd_starts)
			{
				if (frame.fifoData[cmd_start] == GX_LOAD_BP_REG)
				{
					const u8 cmd2 = frame.fifoData[cmd_start + 1];
					if (cmd2 == BPMEM_EFB_ADDR)
					{
						const u32 value = *(u32*)&frame.fifoData[cmd_start + 1] & 0x00ffffff;
						const u32 addr = value << 5; // TODO
						PrepareMemoryLoad(addr, EFB_WIDTH*EFB_HEIGHT*4);  // TODO: size
					}
				}
			}
		}
	}

	// Actually apply initial state
	const std::vector<u32>& bpmem = fifo_data.bpmem;
	const std::vector<u32>& cpmem = fifo_data.cpmem;
	const std::vector<u32>& xfmem = fifo_data.xfmem;
	const std::vector<u32>& xfregs = fifo_data.xfregs;
	const std::vector<u16>& vimem = fifo_data.vimem;

	for (unsigned int i = 0; i < fifo_data.bpmem.size(); ++i)
	{
		if ((i == BPMEM_TRIGGER_EFB_COPY
			|| i == BPMEM_CLEARBBOX1
			|| i == BPMEM_CLEARBBOX2
			|| i == BPMEM_SETDRAWDONE
			|| i == BPMEM_PE_TOKEN_ID // TODO: Sure that we want to skip this one?
			|| i == BPMEM_PE_TOKEN_INT_ID
			|| i == BPMEM_LOADTLUT0
			|| i == BPMEM_LOADTLUT1
			|| i == BPMEM_TEXINVALIDATE
			|| i == BPMEM_PRELOAD_MODE
			|| i == BPMEM_CLEAR_PIXEL_PERF))
			continue;

		const u32 old_value = le32toh(bpmem[i]) & 0xffffff;
		const u32 new_value = TransformBPReg(i, old_value, fifo_data);

#if ENABLE_CONSOLE!=1
		wgPipe->U8 = GX_LOAD_BP_REG;
		wgPipe->U32 = (i<<24)|(new_value&0xffffff);
#endif
	}

	for (unsigned int i = 0; i < fifo_data.vimem.size(); ++i)
	{
		u16 new_value = vimem[i];

		// Patch texture addresses
		if ((2*i >= VI_FB_LEFT_TOP_HI && 2*i < VI_FB_LEFT_TOP_HI+4) ||
			(2*i >= VI_FB_LEFT_BOTTOM_HI && 2*i < VI_FB_LEFT_BOTTOM_HI+4))
		{
			u32 tempval;
			if (2*i == VI_FB_LEFT_TOP_HI)
			{
				// also swapping the two u16 values
				tempval = ((u32)le16toh(vimem[VI_FB_LEFT_TOP_HI/2])) | ((u32)le16toh(vimem[VI_FB_LEFT_TOP_LO/2]) << 16);
			}
			else if (2*i == VI_FB_LEFT_BOTTOM_HI)
			{
				// also swapping the two u16 values
				tempval = ((u32)le16toh(vimem[VI_FB_LEFT_BOTTOM_HI/2])) | ((u32)le16toh(vimem[VI_FB_LEFT_BOTTOM_LO/2]) << 16);
			}
			UVIFBInfoRegister* reg = (UVIFBInfoRegister*)&tempval;
			u32 addr = (reg->POFF) ? (reg->FBB << 5) : reg->FBB;
			u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(addr));
			reg->FBB = (reg->POFF) ? (new_addr >> 5) : new_addr;

			printf("XFB %s at %x (redirected to %x)\n", (2*i==VI_FB_LEFT_TOP_HI) ? "top" : "bottom", addr, new_addr);

			u16 new_value_hi = h16tole(tempval >> 16);
			u16 new_value_lo = h16tole(tempval & 0xFFFF);

#if ENABLE_CONSOLE!=1
			// "raw" register poking broken for some reason, using the easy method for now...
			/*u32 level;
			_CPU_ISR_Disable(level);
			_viReg[i] = new_value_hi;
			_viReg[i+1] = new_value_lo;
			_CPU_ISR_Restore(level);*/
//			VIDEO_SetNextFramebuffer(GetPointer(new_addr));
			//VIDEO_SetNextFramebuffer(GetPointer(efbcopy_target)); // and there go our haxx..
#endif

			++i;  // increase i by 2
			continue;
		}

#if ENABLE_CONSOLE!=1
		// TODO: Is this correct?
//		_viReg[i] = new_value;
#endif
	}

#if ENABLE_CONSOLE!=1
	auto load_cp_reg = [&](u8 reg) {
		const u32 old_value = le32toh(cpmem[reg]);
		const u32 new_value = TransformCPReg(reg, old_value, target_cpmem);
		wgPipe->U8 = GX_LOAD_CP_REG;
		wgPipe->U8 = reg;
		wgPipe->U32 = new_value;
	};

	load_cp_reg(MATINDEX_A);
	load_cp_reg(MATINDEX_B);
	load_cp_reg(VCD_LO);
	load_cp_reg(VCD_HI);

	for (u8 i = 0; i < 8; ++i)
	{
		load_cp_reg(CP_VAT_REG_A + i);
		load_cp_reg(CP_VAT_REG_B + i);
		load_cp_reg(CP_VAT_REG_C + i);
	}

	for (u8 i = 0; i < 16; ++i)
	{
		load_cp_reg(ARRAY_BASE + i);
		load_cp_reg(ARRAY_STRIDE + i);
	}

	for (unsigned int i = 0; i < xfmem.size(); i += 16)
	{
		wgPipe->U8 = GX_LOAD_XF_REG;
		wgPipe->U32 = 0xf0000 | (i&0xffff); // load 16*4 bytes at once
		for (int k = 0; k < 16; ++k)
		{
			wgPipe->U32 = le32toh(xfmem[i + k]);
		}
	}

	for (unsigned int i = 0; i < xfregs.size(); ++i)
	{
		wgPipe->U8 = GX_LOAD_XF_REG;
		wgPipe->U32 = 0x1000 | (i&0x0fff);
		u32 val = xfregs[i];
		if (i == 5) val = 1;
		wgPipe->U32 = le32toh(xfregs[i]);
	}

	// Flush WGP
	for (int i = 0; i < 7; ++i)
		wgPipe->U32 = 0;
	wgPipe->U16 = 0;
	wgPipe->U8 = 0;
#endif
}

// Removes redundant data from a fifo log
void OptimizeFifoData(FifoData& fifo_data)
{
	for (auto frame : fifo_data.frames)
	{
//		for (auto byte : frame.)
	}
}

#define DFF_FILENAME "sd:/dff/test.dff"

#define DEFAULT_FIFO_SIZE   (256*1024)
static void *frameBuffer[2] = { NULL, NULL};
static u32* screenshot_buffer = NULL;
static char* screenshot_dir = NULL;

void PrepareScreenshot(u32 left, u32 top, u32 width, u32 height);
void SaveScreenshot(int screenshot_number, u32 efb_width, u32 efb_height);

GXRModeObj *rmode;

u32 fb = 0;
u32 first_frame = 1;

void Init()
{
	VIDEO_Init();

	rmode = VIDEO_GetPreferredMode(NULL);
	first_frame = 1;
	fb = 0;
#if ENABLE_CONSOLE!=1
	frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode)); // TODO: Shouldn't require manual framebuffer management!
	frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	screenshot_buffer = (u32*)malloc(EFB_WIDTH * EFB_HEIGHT * sizeof(u32));
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(frameBuffer[fb]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
#endif

	fb ^= 1;

	void *gp_fifo = NULL;
	gp_fifo = memalign(32,DEFAULT_FIFO_SIZE);
	memset(gp_fifo,0,DEFAULT_FIFO_SIZE);

	GX_Init(gp_fifo,DEFAULT_FIFO_SIZE);

#if ENABLE_CONSOLE==1
	console_init(frameBuffer[0],20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
#endif

	WPAD_Init();

	if(!fatInitDefault())
	{
		printf("fatInitDefault failed!\n");
	}

	net_init();
}

bool CheckIfHomePressed()
{
/*	VIDEO_WaitVSync();
	fb ^= 1;
*/
	WPAD_ScanPads();

	if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
	{
		return true;
	}
	return false;
}

u32 TransformCPReg(u8 reg, u32 data, CPMemory& target_cpmem)
{
	// Note: we store the raw address, not the translated one
	target_cpmem.LoadReg(reg, data);

	if ((reg & 0xF0) == ARRAY_BASE)
		return MEM_VIRTUAL_TO_PHYSICAL(GetPointer(data));
	else
		return data;
}

// Data and return value should use only 24 bits
u32 TransformBPReg(u8 reg, u32 data, const FifoData& fifo_data)
{
	// Patch texture addresses
	if ((reg >= BPMEM_TX_SETIMAGE3   && reg < BPMEM_TX_SETIMAGE3 + 4) ||
		(reg >= BPMEM_TX_SETIMAGE3_4 && reg < BPMEM_TX_SETIMAGE3_4+4) ||
		reg == BPMEM_PRELOAD_ADDR || reg == BPMEM_LOADTLUT0 || reg == BPMEM_EFB_ADDR)
	{
		const u32 addr = data << 5;
		const u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(addr));
		const u32 new_value = new_addr >> 5;
		return new_value;
	}
	else
	{
		return data;
	}
}

void DrawFrame(u32 cur_frame, const FifoData& fifo_data, const std::vector<AnalyzedFrameInfo>& analyzed_frames, CPMemory& cpmem, bool screenshot_on_copy, bool screenshot_end) {
	const FifoFrameData& cur_frame_data = fifo_data.frames[cur_frame];
	const AnalyzedFrameInfo& cur_analyzed_frame = analyzed_frames[cur_frame];
	if (cur_frame == 0) // TODO: Check for first_frame instead and apply previous state changes
	{
		ApplyInitialState(fifo_data, cpmem, analyzed_frames);
	}

	u32 update_num = 0;
	for (auto& cur_object : cur_analyzed_frame.objects)
	{
		u32 num_cmds = cur_object.cmd_starts.size();
		for (u32 cmd_index = 0; cmd_index < num_cmds; cmd_index++)
		{
			const u32 cur_command = cur_object.cmd_starts[cmd_index];
			const u32 cur_command_end = (cmd_index + 1 < num_cmds) ? cur_object.cmd_starts[cmd_index + 1] : cur_object.last_cmd_byte + 1;
			const u8* cmd_data = &cur_frame_data.fifoData[cur_command];

			const FifoFrameData &frame = fifo_data.frames[cur_frame];
			while (update_num < frame.memoryUpdates.size())
			{
				const DffMemoryUpdate& update = frame.memoryUpdates[update_num];
				if (update.fifoPosition <= cur_command)
				{
//					PrepareMemoryLoad(update.address, update.dataSize);
					fseek(fifo_data.file, update.dataOffset, SEEK_SET);
					fread(GetPointer(update.address), update.dataSize, 1, fifo_data.file);

					// DCFlushRange expects aligned addresses
					u32 off = update.address % DEF_ALIGN;
					DCFlushRange(GetPointer(update.address) - off, update.dataSize + off);
					update_num++;
					if (update.type == DffMemoryUpdate::Type::TEXTURE_MAP)
					{
						// GX_InvalidateTexAll, except we aren't re-flushing the state
						// I don't 100% understand why this is needed, but maybe we're putting
						// things in memory in a different order that causes texture cache
						// problems?  This does break the HW fifoplayer for testing actual
						// texture cache issues though.
						wgPipe->U8 = GX_LOAD_BP_REG;
						wgPipe->U32 = 0x66001000;
						wgPipe->U8 = GX_LOAD_BP_REG;
						wgPipe->U32 = 0x66001100;
					}
				}
				else
				{
					break;
				}
			}

			if (!cur_object.cmd_enabled[cmd_index])
				continue;

			if (cmd_data[0] == GX_LOAD_BP_REG)
			{
				const u32 value = *(u32*)&cmd_data[1]; // TODO: Endianness (only works on Wii)
				const u8 cmd2 = (value >> 24);
				if (screenshot_on_copy && cmd2 == BPMEM_TRIGGER_EFB_COPY) {
					PrepareScreenshot(cur_analyzed_frame.efb_left, cur_analyzed_frame.efb_top, cur_analyzed_frame.efb_width, cur_analyzed_frame.efb_height);
					SaveScreenshot(g_screenshot_number++, cur_analyzed_frame.efb_width, cur_analyzed_frame.efb_height);
				}
				const u32 data = value & 0xffffff;
				const u32 new_data = TransformBPReg(cmd2, data, fifo_data);
				const u32 new_value = (cmd2 << 24) | (new_data & 0xffffff);
#if ENABLE_CONSOLE!=1
				wgPipe->U8 = GX_LOAD_BP_REG;
				wgPipe->U32 = new_value;
#endif
			}
			else if (cmd_data[0] == GX_LOAD_CP_REG)
			{
				const u8 cmd2 = cmd_data[1];
				const u32 value = *(u32*)&cmd_data[2]; // TODO: Endianness (only works on Wii)
				const u32 new_value = TransformCPReg(cmd2, value, cpmem);

#if ENABLE_CONSOLE!=1
				wgPipe->U8 = GX_LOAD_CP_REG;
				wgPipe->U8 = cmd2;
				wgPipe->U32 = new_value;
#endif
			}
			else if (cmd_data[0] == GX_LOAD_XF_REG)
			{
				// Load data directly instead of going through the loop again for no reason

				u32 cmd2 = *(u32*)&cmd_data[1]; // TODO: Endianness (only works on Wii)
				u8 streamSize = ((cmd2 >> 16) & 15) + 1;

#if ENABLE_CONSOLE!=1
				wgPipe->U8 = GX_LOAD_XF_REG;
				wgPipe->U32 = cmd2;

				for (u8 i = 0; i < streamSize; i++) {
					u32 address = (cmd2 & 0xffff) + i;
					f32 value_f;
					memcpy(&value_f, &cmd_data[5+4*i], sizeof(f32));
					if (address == 0x101a || address == 0x101b) {  // viewport width/height
						value_f *= g_view_scale;
					}
					if (address == 0x101d) {  // viewport x orig
						value_f -= s32(cur_analyzed_frame.efb_width) * g_x_offset;
					}
					if (address == 0x101e) {  // viewport y orig
						value_f -= s32(cur_analyzed_frame.efb_height) * g_y_offset;
					}
					wgPipe->F32 = value_f;
				}
#endif
			}
			else if(cmd_data[0] == GX_LOAD_INDX_A ||
					cmd_data[0] == GX_LOAD_INDX_B ||
					cmd_data[0] == GX_LOAD_INDX_C ||
					cmd_data[0] == GX_LOAD_INDX_D)
			{
#if ENABLE_CONSOLE!=1
				// Map the command byte to its ref array.
				// GX_LOAD_INDX_A (32 = 8*4) . CPArray::XF_A (4+8 = 12)
				// GX_LOAD_INDX_B (40 = 8*5) . CPArray::XF_B (5+8 = 13)
				// GX_LOAD_INDX_C (48 = 8*6) . CPArray::XF_C (6+8 = 14)
				// GX_LOAD_INDX_D (56 = 8*7) . CPArray::XF_D (7+8 = 15)
				const u8 ref_array = (cmd_data[0] / 8) + 8;
				// Map the array to the proper CP array
				const u32 array_base = cpmem.arrayBases[ref_array];
				const u32 stride = cpmem.arrayStrides[ref_array];

				const u32 value = *(u32*)&cmd_data[1]; // TODO: Endianness (only works on Wii)
				const u32 index = value >> 16;
				const u32 offset = index * stride;

				// Since we can't guarantee that CP arrays are contiguous
				// (the fifo recorder doesn't record the whole array here,
				// though it does for vertex attributes), we need to move the
				// array base so that it matches the expected location.
				// e.g. if array A was at address 0x8000 with stride 0x10,
				// we might remap 0x8000 to 0xC000 for index 0, but index 2
				// would originally be at 0x8020 but we might map it at 0xD000.
				// The normal CP array remapping would result in looking at
				// 0xC020, so we instead temporarily set the array location to
				// 0xCFD0 so that adding 0x20 results in 0xD000.
				const u32 load_address = array_base + offset;
				const u32 translated_load_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(load_address));
				const u32 translated_array_base = translated_load_addr - offset;

				// Load the modified array
				wgPipe->U8 = GX_LOAD_CP_REG;
				wgPipe->U8 = ARRAY_BASE | ref_array;
				wgPipe->U32 = translated_array_base;

				// Send the indexed load command
				wgPipe->U8 = cmd_data[0];
				wgPipe->U32 = value;

				// Restore the original array
				wgPipe->U8 = GX_LOAD_CP_REG;
				wgPipe->U8 = ARRAY_BASE | ref_array;
				wgPipe->U32 = array_base;
#endif
			}
			else if (cmd_data[0] & 0x80)
			{
				u32 vtxAttrGroup = cmd_data[0] & GX_VAT_MASK;
				int vertexSize = CalculateVertexSize(vtxAttrGroup, cpmem);

				u16 streamSize = *(u16*)&cmd_data[1]; // TODO: Endianness (only works on Wii)

#if ENABLE_CONSOLE!=1
				wgPipe->U8 = cmd_data[0];
				wgPipe->U16 = streamSize;
				for (int byte = 0; byte < streamSize * vertexSize; ++byte)
					wgPipe->U8 = cmd_data[3+byte];
#endif
			}
			else
			{
				u32 size = cur_command_end - cur_command;
#if ENABLE_CONSOLE!=1
				for (u32 addr = 0; addr < size; ++addr) {
					// TODO: Push u32s instead
					wgPipe->U8 = cmd_data[addr];
				}
#endif
			}
		}
	}

#if ENABLE_CONSOLE!=1
	if (fifo_data.version < 2)
	{
		if (screenshot_end)
		{
			PrepareScreenshot(cur_analyzed_frame.efb_left, cur_analyzed_frame.efb_top, cur_analyzed_frame.efb_width, cur_analyzed_frame.efb_height);
		}

		// finish frame for legacy dff files
		//
		// Note that GX_CopyDisp(frameBuffer[fb],GX_TRUE) uses an internal state
		// which is out of sync with the dff_data, so we're manually writing
		// to the EFB copy registers instead.
		wgPipe->U8 = GX_LOAD_BP_REG;
		wgPipe->U32 = (BPMEM_EFB_ADDR << 24) | ((MEM_VIRTUAL_TO_PHYSICAL(frameBuffer[fb]) >> 5) & 0xFFFFFF);

		UPE_Copy copy{.Hex = 0};
		copy.clear = 1;
		copy.copy_to_xfb = 1;
		wgPipe->U8 = GX_LOAD_BP_REG;
		wgPipe->U32 = (BPMEM_TRIGGER_EFB_COPY << 24) | copy.Hex;

		GX_Flush();
		GX_DrawDone();

		// TODO: This isn't quite perfect, but it at least means that we
		// have the right width (height might be wrong, e.g. for NES games)
		rmode->fbWidth = cur_analyzed_frame.efb_width;
		rmode->viWidth = cur_analyzed_frame.efb_width;
		rmode->xfbHeight = cur_analyzed_frame.efb_height;
		rmode->efbHeight = cur_analyzed_frame.efb_height;
		rmode->viHeight = cur_analyzed_frame.efb_height;
		VIDEO_Configure(rmode);

		VIDEO_SetNextFramebuffer(frameBuffer[fb]);
		if (first_frame)
		{
			VIDEO_SetBlack(FALSE);
			first_frame = 0;
		}
	}
#endif
	VIDEO_WaitVSync();
	VIDEO_Flush();
	fb ^= 1;
}

int main()
{
	Init();

	printf("Init done!\n");
	int server_socket;
	int client_socket = WaitForConnection(server_socket);
	u8 dummy;
	net_recv(client_socket, &dummy, 1, 0);
	if (RET_SUCCESS == ReadHandshake(client_socket))
		printf("Successfully exchanged handshake token!\n");
	else
		printf("Failed to exchanged handshake token!\n");

	net_recv(client_socket, &dummy, 1, 0);
	ReadStreamedDff(client_socket, CheckIfHomePressed);

	FifoData fifo_data;
	LoadDffData(DFF_FILENAME, fifo_data);
	printf("Loaded dff data\n");
	memory_manager_allow_wii_addrs = fifo_data.wii;

	FifoDataAnalyzer analyzer;
	std::vector<AnalyzedFrameInfo> analyzed_frames;
	analyzer.AnalyzeFrames(fifo_data, analyzed_frames);
	printf("Analyzed dff data\n");

	CPMemory cpmem; // TODO: Should be removed...

	bool processing = true;
	int first_frame = 0;
	int last_frame = first_frame + fifo_data.frames.size()-1;
	int cur_frame = first_frame;
	while (processing)
	{
		CheckForNetworkEvents(server_socket, client_socket, fifo_data.frames, analyzed_frames);

		DrawFrame(cur_frame, fifo_data, analyzed_frames, cpmem, false, false);

		// TODO: Menu stuff
		// reset GX state
		// draw menu
		// restore GX state

		// input checking
		// A = select menu point
		// B = menu back
		// plus = pause
		// minus = hide menu
		// home = stop
		WPAD_ScanPads();

//		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
//			processing = false;

		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
		{
			printf("\n");
			fclose(fifo_data.file);
			exit(0);
		}

		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_UP)
			g_y_offset--;
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_DOWN)
			g_y_offset++;
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_LEFT)
			g_x_offset--;
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_RIGHT)
			g_x_offset++;
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_MINUS)
			g_view_scale /= 2;
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_PLUS)
			g_view_scale *= 2;
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_A)
		{
			f32 old_view_scale = g_view_scale;
			s32 old_x_offset = g_x_offset;
			s32 old_y_offset = g_y_offset;
			g_view_scale = 4.0f;
			for (int y = -2; y <= 2; y++) {
				for (int x = -2; x <= 2; x++) {
					g_x_offset = x;
					g_y_offset = y;
					DrawFrame(cur_frame, fifo_data, analyzed_frames, cpmem, false, true);
					SaveScreenshot(g_screenshot_number, analyzed_frames[cur_frame].efb_width, analyzed_frames[cur_frame].efb_height);
				}
			}
			// To combine images, use ImageMagick - see https://superuser.com/a/290679
			// montage -mode concatenate -tile 5x5 1_y{-2..2}_x{-2..2}_scale2.png 1.png
			g_view_scale = old_view_scale;
			g_x_offset = old_x_offset;
			g_y_offset = old_y_offset;
			g_screenshot_number++;
		}
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_B)
		{
			DrawFrame(cur_frame, fifo_data, analyzed_frames, cpmem, false, true);
			SaveScreenshot(g_screenshot_number++, analyzed_frames[cur_frame].efb_width, analyzed_frames[cur_frame].efb_height);
		}
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_2)
		{
			// Calls SaveScreenshot internally
			DrawFrame(cur_frame, fifo_data, analyzed_frames, cpmem, true, false);
		}

		++cur_frame;
		cur_frame = first_frame + ((cur_frame-first_frame) % (last_frame-first_frame+1));
	}

	fclose(fifo_data.file);

	return 0;
}

void PrepareScreenshot(u32 left, u32 top, u32 width, u32 height) {
	GX_Flush();
	GX_DrawDone();

	GXColor color;
	for (u32 y = 0; y < height; y++)
	{
		for (u32 x = 0; x < width; x++)
		{
			GX_PeekARGB(x + left, y + top, &color);
			u32 val = ((u32) color.a) << 24;
			val |= ((u32) color.r) << 16;
			val |= ((u32) color.g) << 8;
			val |= color.b;

			screenshot_buffer[x + y * EFB_WIDTH] = val;
		}
	}
}

void SaveScreenshot(int screenshot_number, u32 efb_width, u32 efb_height) {
	if (screenshot_dir == NULL) {
		time_t rawtime;
		time(&rawtime);
		struct tm* curtime = localtime(&rawtime);
		screenshot_dir = (char*)malloc(100);
		strftime(screenshot_dir, 100, "sd:/Test_%y%m%d_%H%M%S", curtime);
		mkdir(screenshot_dir, 0777);
	}

	char filename[256];
	if (g_view_scale != 1.0f)
	{
		// Order of y then x matters for combining images
		snprintf(filename, sizeof(filename), "%s/%d_y%d_x%d_scale%d.png",
		         screenshot_dir, screenshot_number, g_y_offset, g_x_offset, (int)log2(g_view_scale));
	}
	else
	{
		snprintf(filename, sizeof(filename), "%s/%d_y%d_x%d.png",
		         screenshot_dir, screenshot_number, g_y_offset, g_x_offset);
	}

	png_bytep *row_pointers = (png_bytep *) malloc(efb_height * sizeof(png_bytep));

	FILE *fp = fopen(filename, "wb");
	if (!fp)
		return;

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
	if (!png_ptr)
		return;

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
		return;

	if (setjmp(png_jmpbuf(png_ptr)))
		return;

	png_init_io(png_ptr, fp);

	png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
	png_set_IHDR(png_ptr, info_ptr, efb_width, efb_height, 8,
				PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
				PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_write_info(png_ptr, info_ptr);

	// NOTE: We use the *full* EFB_WIDTH here, not the fifolog's current efb_width
	for (u32 i = 0; i < efb_height; ++i)
		row_pointers[i] = (png_bytep) (screenshot_buffer + i * EFB_WIDTH);

	png_set_swap_alpha(png_ptr);

	png_write_image(png_ptr, row_pointers);
	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
	free(row_pointers);
	fclose(fp);
}
