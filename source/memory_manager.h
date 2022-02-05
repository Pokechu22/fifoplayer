#include <stdint.h>

// TODO: Don't use these typedefs
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

#define DEF_ALIGN 32
class aligned_buf
{
public:
	aligned_buf();
	aligned_buf(u32 alignment);
	~aligned_buf();

	aligned_buf(const aligned_buf& oth);

	void resize(u32 new_size);

	u8* buf;
	u32 size;

private:
	u32 alignment;
};

bool IntersectsMemoryRange(u32 start1, u32 size1, u32 start2, u32 size2);

u32 FixupMemoryAddress(u32 addr);

// Returns true if memory layout changed
bool PrepareMemoryLoad(u32 start_addr, u32 size);

// Must have been reserved via PrepareMemoryLoad first
u8* GetPointer(u32 addr);

void PrintMemoryMap();

extern bool memory_manager_allow_wii_addrs;
