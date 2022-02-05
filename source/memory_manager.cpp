#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>

#include <vector>
#include <map>

#include "memory_manager.h"

// #define ZERO_BUFFER
#define FALLBACK_BUFFER_SIZE 0x2000

bool memory_manager_allow_wii_addrs = false;
static std::map<u32, aligned_buf> memory_map; // map of memory chunks (indexed by starting address)
static u8* fallback_buffer = NULL;
static std::map<u32, u32> fallback_addresses;

aligned_buf::aligned_buf() : buf(NULL), size(0), alignment(DEF_ALIGN)
{

}

aligned_buf::aligned_buf(int alignment) : buf(NULL), size(0), alignment(alignment)
{

}

aligned_buf::~aligned_buf()
{
	free(buf);
}

aligned_buf::aligned_buf(const aligned_buf& oth)
{
	if (oth.buf)
	{
		buf = (u8*)memalign(oth.alignment, oth.size);
#ifdef ZERO_BUFFER
		memset(buf, 0, oth.size);
#endif
		printf("copied to %p (%x) \n", buf, MEM_VIRTUAL_TO_PHYSICAL(buf));
		memcpy(buf, oth.buf, oth.size);
	}
	else buf = NULL;
	size = oth.size;
	alignment = oth.alignment;
}

void aligned_buf::resize(int new_size)
{
	if (!buf)
	{
		buf = (u8*)memalign(alignment, new_size);
#ifdef ZERO_BUFFER
		memset(buf, 0, new_size);
#endif
		printf("allocated to %p (%x) - size %x \n", buf, MEM_VIRTUAL_TO_PHYSICAL(buf), new_size);

	}
	else
	{
		u8* old_buf = buf;
		buf = (u8*)memalign(alignment, new_size);
#ifdef ZERO_BUFFER
		memset(buf, 0, new_size);
#endif
		memcpy(buf, old_buf, std::min(new_size, size));
		printf("reallocated to %p (%x)\n", buf, MEM_VIRTUAL_TO_PHYSICAL(buf));
		free(old_buf);
	}
	size = new_size;
}


bool IntersectsMemoryRange(u32 start1, u32 size1, u32 start2, u32 size2)
{
	return size1 && size2 && ((start1 >= start2 && start1 < start2 + size2) ||
			(start2 >= start1 && start2 < start1 + size1));
}

u32 FixupMemoryAddress(u32 addr)
{
	switch (addr >> 28)
	{
		case 0x0:
		case 0x8:
			addr &= 0x01FFFFFF; // RAM_MASK
			break;

		case 0x1:
		case 0x9:
		case 0xd:
			if (memory_manager_allow_wii_addrs)
			{
				addr &= 0x03FFFFFF; // EXRAM_MASK
				// Include the top bit that indicates that this is in MEM2
				// (otherwise, MEM1 and MEM2 get mapped to the same place and clash with each other)
				addr |= 0x10000000;
			}
			else
			{
				// Several GameCube games still set this bit (e.g. Wario World, Mario Kart Double Dash, Mario Party 5)
				// This has been seen with BPMEM_LOADTLUT1 at least
				addr &= 0x01FFFFFF; // RAM_MASK
			}
			break;

		default:
			printf("CRITICAL: Unkown memory location %x!\n", addr);
			exit(0); // I'd rather exit than not noticing this kind of issue...
			break;
	}
	return addr;
}

// TODO: Needs to take care of alignment, too!
// Returns true if memory layout changed
bool PrepareMemoryLoad(u32 start_addr, u32 size)
{
	bool ret = false;

	start_addr = FixupMemoryAddress(start_addr);

	// Make sure alignment of data inside the memory block is preserved
	u32 off = start_addr % DEF_ALIGN;
	start_addr = start_addr - off;
	size += off;

	std::vector<u32> affected_elements;
	u32 new_start_addr = start_addr;
	u32 new_end_addr = start_addr + size;

	// Find overlaps with existing memory chunks
	for (auto& entry : memory_map)
	{
		const u32 other_addr = entry.first;
		const u32 other_size = entry.second.size;
		const u32 other_end_addr = other_addr + other_size;
		if (IntersectsMemoryRange(other_addr, other_size, start_addr, size))
		{
			affected_elements.push_back(other_addr);
			new_start_addr = std::min(new_start_addr, other_addr);
			new_end_addr = std::max(new_end_addr, other_end_addr);
		}
	}

	aligned_buf& new_memchunk(memory_map[new_start_addr]); // creates a new vector or uses the existing one
	u32 new_size = new_end_addr - new_start_addr;

	// if the new memory range is inside an existing chunk, there's nothing to do
	if (new_memchunk.size == new_size)
		return false;

	// resize chunk to required size, move old content to it, replace old arrays with new one
	// NOTE: can't do reserve here because not the whole memory might be covered by existing memory chunks
	new_memchunk.resize(new_size);
	while (!affected_elements.empty())
	{
		u32 addr = affected_elements.back();

		// first chunk is already in new_memchunk
		if (addr != new_start_addr)
		{
			aligned_buf& src = memory_map[addr];
			memcpy(&new_memchunk.buf[addr - new_start_addr], &src.buf[0], src.size);
			memory_map.erase(addr);

			ret = true;
		}
		affected_elements.pop_back();
	}

	// TODO: Handle critical case where memory allocation fails!

	return ret;
}

// Must have been reserved via PrepareMemoryLoad first
u8* GetPointer(u32 addr)
{
	addr = FixupMemoryAddress(addr);

	for (auto& entry : memory_map)
	{
		const u32 start_addr = entry.first;
		const u32 size = entry.second.size;
		const u32 end_addr = start_addr + size;
		if (addr >= start_addr && addr < end_addr)
			return &entry.second.buf[addr - start_addr];
	}

	// This case is hit if a game sets a texture, doesn't draw
	// anything with it, and then sets a new texture; if the texture is never used,
	// its content isn't recorded into the dff file (I think), so no memory update
	// exists to be translated.
	// It's also hit for null pointers, which presumably won't actually be used.
	if (fallback_addresses[addr]++ == 0)
	{
		printf("Failed to find pointer for addr %x\n", addr);
	}
	if (fallback_buffer == nullptr)
	{
		fallback_buffer = (u8*)memalign(DEF_ALIGN, FALLBACK_BUFFER_SIZE);
		memset(fallback_buffer, 0, FALLBACK_BUFFER_SIZE);
	}
	return fallback_buffer;
}

void PrintMemoryMap() {
	printf("%d entries:\n", memory_map.size());
	u32 prev_end_addr = 0;
	for (auto& entry : memory_map)
	{
		const u32 start_addr = entry.first;
		const u32 size = entry.second.size;
		const u32 end_addr = start_addr + size;
		printf("- %x to %x (size %x): mapped to %p\n", start_addr, end_addr, size, entry.second.buf);
		if (prev_end_addr > start_addr)
		{
			printf("Overlaps!!!!!\n");
		}
		prev_end_addr = end_addr;
	}
}
