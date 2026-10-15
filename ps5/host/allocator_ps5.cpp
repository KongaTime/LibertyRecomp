// Liberty Recompiled on the PS5 - the title's C++ allocations: operator new
// and delete with a cache of free blocks in each thread.
//
// The console's heap (the payload SDK's ps5platform heap: dlmalloc arenas, a
// pthread mutex each, shared round robin by the title's threads) makes a
// thread that frees another thread's blocks take that thread's arena lock.
// The renderer's worker frees every command the game's threads queue for it,
// in the arenas those threads are allocating from at the same moment, and a
// contended mutex on the console is a system call: the sampler
// (rex/platform/sampler_prospero.h) found the worker in mspace_free's lock for
// about a third of its time in the city at 4-16 fps, build d1069a98.
//
// Here a freed block goes to the freeing thread's cache, and caches pass
// blocks to each other through a shared depot in whole lists, so a lock is
// taken once a list instead of once a block. Blocks up to 64 KiB come in size
// classes (at most a quarter wasted); bigger ones, and over-aligned ones, go
// to the heap as they are. Blocks a class holds stay in it.
//
// Each block has a header before it: the class, and for a block from the
// heap, where the heap's block starts. operator new's blocks are aligned to
// __STDCPP_DEFAULT_NEW_ALIGNMENT__, which is 32 on the PS5 target (not x86-64's
// usual 16): code built for it moves new's blocks with aligned 32-byte loads
// and stores, and a 16-byte aligned block faulted at start-up (fd7989ae).
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <new>

namespace {

// The alignment of new's blocks, and the header's size, which keeps a block
// after it at that alignment (the heap's blocks are 32-byte aligned)
constexpr size_t kAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
constexpr size_t kHeaderBytes = kAlignment < 16 ? 16 : kAlignment;
static_assert(kHeaderBytes % 16 == 0 && kHeaderBytes <= 32);
constexpr uint32_t kMagic = 0x4c524e57;
constexpr size_t kLargestClassBytes = 64 * 1024;
// 32..128 in steps of 32, then four steps in each power of two up to 64 KiB:
// every class a multiple of 32, so carved blocks stay aligned
constexpr int kClassCount = 4 + (16 - 7) * 4;
constexpr uint32_t kHeapBlock = 0xffffffffu;

struct Header {
  uint32_t magic;
  uint32_t size_class;  // kHeapBlock: base is the heap's block
  uintptr_t base;
  unsigned char padding[kHeaderBytes - 16];
};
static_assert(sizeof(Header) == kHeaderBytes);

constexpr size_t ClassBytes(int size_class) {
  if (size_class < 4) {
    return size_t(size_class + 1) * 32;
  }
  const int power = 7 + (size_class - 4) / 4;
  const int step = (size_class - 4) % 4 + 1;
  return (size_t(1) << power) + size_t(step) * (size_t(1) << (power - 2));
}
static_assert(ClassBytes(kClassCount - 1) == kLargestClassBytes);
static_assert(ClassBytes(4) % 32 == 0 && ClassBytes(5) % 32 == 0);

inline int ClassOf(size_t bytes) {
  if (bytes <= 128) {
    return int((bytes + 31) >> 5) - 1;
  }
  // 2^power < bytes <= 2^(power + 1)
  const int power = 63 - __builtin_clzll(uint64_t(bytes - 1));
  const size_t step_bytes = size_t(1) << (power - 2);
  const size_t step = (bytes - (size_t(1) << power) + step_bytes - 1) / step_bytes;
  return 4 + (power - 7) * 4 + int(step) - 1;
}

// How many free blocks of a class a thread keeps before it hands them on
constexpr uint32_t CacheLimit(int size_class) {
  const size_t limit = (512 * 1024) / ClassBytes(size_class);
  return uint32_t(limit < 8 ? 8 : limit > 512 ? 512 : limit);
}

// A free block, and in the first block of a chain the depot holds, the next
// chain and how many blocks this one has (every class has room for them)
struct FreeBlock {
  FreeBlock* next;
  FreeBlock* next_chain;
  uint32_t chain_count;
};
static_assert(sizeof(FreeBlock) <= 32);

struct ThreadCache {
  FreeBlock* head[kClassCount];
  uint32_t count[kClassCount];
};
// Zero-initialized and without a destructor: a thread that ends leaves its
// cache's blocks behind (at most CacheLimit of each class)
thread_local ThreadCache cache;

// Whole chains, as threads handed them on: a thread's list moves in one step,
// never walked (the renderer's worker frees far more than it allocates, and
// cutting a batch off its list walked hundreds of cold blocks: 7 % of its
// time in build e441f37d)
struct Depot {
  std::mutex lock;
  FreeBlock* chains = nullptr;
};
Depot depots[kClassCount];

Header* HeaderOf(void* block) {
  return reinterpret_cast<Header*>(static_cast<char*>(block) - kHeaderBytes);
}

// New blocks for a class, carved from one heap block that is never returned,
// aligned here rather than trusting the heap's alignment
bool Carve(ThreadCache& thread, int size_class, uint32_t count) {
  const size_t stride = kHeaderBytes + ClassBytes(size_class);
  char* raw = static_cast<char*>(std::malloc(stride * count + kAlignment));
  if (!raw) {
    return false;
  }
  char* slab = reinterpret_cast<char*>((reinterpret_cast<uintptr_t>(raw) + kAlignment - 1) &
                                       ~uintptr_t(kAlignment - 1));
  for (uint32_t i = 0; i < count; ++i) {
    char* at = slab + stride * i;
    *reinterpret_cast<Header*>(at) = Header{kMagic, uint32_t(size_class), 0, {}};
    auto* block = reinterpret_cast<FreeBlock*>(at + kHeaderBytes);
    block->next = thread.head[size_class];
    thread.head[size_class] = block;
  }
  thread.count[size_class] += count;
  return true;
}

// For a thread whose list of the class is empty: a chain from the depot, or
// new blocks
bool Refill(ThreadCache& thread, int size_class) {
  Depot& depot = depots[size_class];
  FreeBlock* chain;
  {
    std::lock_guard lock(depot.lock);
    chain = depot.chains;
    if (chain) {
      depot.chains = chain->next_chain;
    }
  }
  if (chain) {
    thread.head[size_class] = chain;
    thread.count[size_class] = chain->chain_count;
    return true;
  }
  return Carve(thread, size_class, CacheLimit(size_class) / 2);
}

// The thread's whole list of the class to the depot, as one chain
void Spill(ThreadCache& thread, int size_class) {
  FreeBlock* chain = thread.head[size_class];
  chain->chain_count = thread.count[size_class];
  thread.head[size_class] = nullptr;
  thread.count[size_class] = 0;
  Depot& depot = depots[size_class];
  std::lock_guard lock(depot.lock);
  chain->next_chain = depot.chains;
  depot.chains = chain;
}

void* AllocateFromHeap(size_t bytes, size_t alignment) {
  if (alignment < kAlignment) {
    alignment = kAlignment;
  }
  // Room for the header before an aligned block
  const size_t slack = kHeaderBytes + alignment;
  if (bytes > SIZE_MAX - slack) {
    return nullptr;
  }
  char* base = static_cast<char*>(std::malloc(bytes + slack));
  if (!base) {
    return nullptr;
  }
  uintptr_t user = reinterpret_cast<uintptr_t>(base) + kHeaderBytes;
  user = (user + alignment - 1) & ~uintptr_t(alignment - 1);
  *HeaderOf(reinterpret_cast<void*>(user)) =
      Header{kMagic, kHeapBlock, reinterpret_cast<uintptr_t>(base), {}};
  return reinterpret_cast<void*>(user);
}

void* Allocate(size_t bytes) noexcept {
  if (!bytes) {
    bytes = 1;
  }
  if (bytes > kLargestClassBytes) {
    return AllocateFromHeap(bytes, kAlignment);
  }
  const int size_class = ClassOf(bytes);
  ThreadCache& thread = cache;
  if (!thread.head[size_class] && !Refill(thread, size_class)) {
    return nullptr;
  }
  FreeBlock* block = thread.head[size_class];
  thread.head[size_class] = block->next;
  --thread.count[size_class];
  return block;
}

void* AllocateAligned(size_t bytes, size_t alignment) noexcept {
  if (alignment <= kAlignment) {
    return Allocate(bytes);
  }
  return AllocateFromHeap(bytes ? bytes : 1, alignment);
}

void Deallocate(void* pointer) noexcept {
  if (!pointer) {
    return;
  }
  Header* header = HeaderOf(pointer);
  if (header->magic != kMagic) {
    // Not one of these blocks: the heap's own
    std::free(pointer);
    return;
  }
  if (header->size_class == kHeapBlock) {
    std::free(reinterpret_cast<void*>(header->base));
    return;
  }
  const int size_class = int(header->size_class);
  ThreadCache& thread = cache;
  auto* block = static_cast<FreeBlock*>(pointer);
  block->next = thread.head[size_class];
  thread.head[size_class] = block;
  if (++thread.count[size_class] >= CacheLimit(size_class)) {
    Spill(thread, size_class);
  }
}

void* AllocateOrThrow(size_t bytes) {
  for (;;) {
    if (void* block = Allocate(bytes)) {
      return block;
    }
    std::new_handler handler = std::get_new_handler();
    if (!handler) {
      throw std::bad_alloc();
    }
    handler();
  }
}

void* AllocateAlignedOrThrow(size_t bytes, size_t alignment) {
  for (;;) {
    if (void* block = AllocateAligned(bytes, alignment)) {
      return block;
    }
    std::new_handler handler = std::get_new_handler();
    if (!handler) {
      throw std::bad_alloc();
    }
    handler();
  }
}

}  // namespace

void* operator new(size_t bytes) { return AllocateOrThrow(bytes); }
void* operator new[](size_t bytes) { return AllocateOrThrow(bytes); }
void* operator new(size_t bytes, const std::nothrow_t&) noexcept { return Allocate(bytes); }
void* operator new[](size_t bytes, const std::nothrow_t&) noexcept { return Allocate(bytes); }
void* operator new(size_t bytes, std::align_val_t alignment) {
  return AllocateAlignedOrThrow(bytes, size_t(alignment));
}
void* operator new[](size_t bytes, std::align_val_t alignment) {
  return AllocateAlignedOrThrow(bytes, size_t(alignment));
}
void* operator new(size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  return AllocateAligned(bytes, size_t(alignment));
}
void* operator new[](size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  return AllocateAligned(bytes, size_t(alignment));
}

void operator delete(void* pointer) noexcept { Deallocate(pointer); }
void operator delete[](void* pointer) noexcept { Deallocate(pointer); }
void operator delete(void* pointer, size_t) noexcept { Deallocate(pointer); }
void operator delete[](void* pointer, size_t) noexcept { Deallocate(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { Deallocate(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { Deallocate(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { Deallocate(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { Deallocate(pointer); }
void operator delete(void* pointer, size_t, std::align_val_t) noexcept { Deallocate(pointer); }
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept { Deallocate(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
  Deallocate(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
  Deallocate(pointer);
}
