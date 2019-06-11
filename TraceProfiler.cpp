// Copyright (c) 2019 Pocketwatch Games, LLC.

#if defined(TRACE_PROFILER) || defined(BUILDING_TRACE_PROFILER)

#include "TraceProfiler.h"

// Optimized for page size
// Should occupy 16,385*4 pages
#define TRACE_BLOCK_SIZE (((1024*1024)+45) * 4)

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4365 4548 4774)
#endif

#include <stdio.h>
#include <chrono>
#include <vector>
#include <thread>
#include <algorithm>
#include <mutex>

#if !defined(TRACE_ASSERT) || !defined(TRACE_VERIFY)
#include <assert.h>
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifndef TRACE_ASSERT
#define TRACE_ASSERT assert
#endif

#ifndef TRACE_VERIFY
#define TRACE_VERIFY TRACE_ASSERT
#endif

#define TRACE_FOURCC(a, b, c, d) ((uint32_t)(((uint32_t)(a)) + (((uint32_t)(b))<<8) + (((uint32_t)(c))<<16)+ (((uint32_t)(d))<<24)))

#ifdef _WIN32
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4668)
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ftello64 _ftelli64
#define fseeko64 _fseeki64
#undef max
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif

static void trace_vDebugWrite(const char* msg, va_list args) {
#ifdef _MSC_VER
	auto c = _vscprintf(msg, args);
	auto buf = (char*)_alloca((size_t)c + 1);
	vsprintf_s(buf, c + 1, msg, args);
	buf[c] = 0;
	OutputDebugStringA(buf);
#else
	vfprintf(stderr, msg, args);
#endif
}

static void trace_DebugWrite(const char* msg, ...) {
	va_list args;
	va_start(args, msg);
	trace_vDebugWrite(msg, args);
	va_end(args);
}

static void trace_vDebugWriteLine(const char* msg, va_list args) {
#ifdef _MSC_VER
	auto c = _vscprintf(msg, args);
	auto buf = (char*)_alloca((size_t)c + 2);
	vsprintf_s(buf, c + 2, msg, args);
	buf[c] = '\n';
	buf[c + 1] = 0;
	OutputDebugStringA(buf);
#else
	vfprintf(stderr, msg, args);
	fprintf(stderr, "\n");
#endif
}

static void trace_DebugWriteLine(const char* msg, ...) {
	va_list args;
	va_start(args, msg);
	trace_vDebugWriteLine(msg, args);
	va_end(args);
}

uint32_t trace_crc_str_32(const char* str) {
	uint32_t crc;

	crc = 0xFFFFFFFFul;

	while (*str != 0) {
		crc = (crc >> 8) ^ trace_crc_tab32[(crc ^ (uint32_t)*str++) & 0x000000FFul];
	}

	return (crc ^ 0xFFFFFFFFul);
}

static constexpr uint64_t INDEX_TIMEBASE_IN_MICROS = 1000 * 1000;

static inline uint64_t GetMicroseconds () {
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}

typedef std::unique_lock<std::mutex> LOCK;
static char s_tracePath[1024];
static std::mutex M;
static std::vector<std::thread> s_writeThreads;
static uint64_t s_microStart;
static uint64_t s_tscStart;
static uint64_t s_ticksPerMicro;
static bool s_init = false;

THREAD_LOCAL TraceThread_t* __tr_thread;

inline uint64_t GetRelativeMicros(uint64_t tsc) {
	return ((tsc - s_tscStart) / s_ticksPerMicro);
}

void TraceWriteBlocks(int reset) {
	auto thread = __tr_thread;
	if (thread->reset >= reset) {
		thread->writeblocks.store(thread->numblocks, std::memory_order_release);
	}
}

TraceThread_t* TraceThreadGrow() {
	auto thread = __tr_thread;

	if (!thread) {
		thread = (TraceThread_t*)malloc(sizeof(TraceThread_t) + sizeof(TraceBlock_t) * (TRACE_BLOCK_SIZE - 1));
		thread->prev = nullptr;
		thread->next = nullptr;
		thread->reset = 0;
		thread->blockbase = 0;
		thread->maxblocks = TRACE_BLOCK_SIZE;
		thread->writeblocks.store(0, std::memory_order_relaxed);
		__tr_thread = thread;
		return thread;
	}

	auto grow = (TraceThread_t*)malloc(sizeof(TraceThread_t) + sizeof(TraceBlock_t) * (TRACE_BLOCK_SIZE - 1));
	memcpy(grow, thread, sizeof(TraceThread_t));
	grow->prev = thread;
	grow->next = nullptr;
	grow->blockbase = thread->maxblocks;
	grow->maxblocks = thread->maxblocks + TRACE_BLOCK_SIZE;

	thread->next = grow;
	thread->writeblocks.store(-1, std::memory_order_release);
		
	__tr_thread = grow;

	return grow;
}

static void UnsortedAddBlockToIndex(int blocknum, uint64_t start, uint64_t end, std::vector<std::vector<int>>& index) {
	uint64_t start_index = start / INDEX_TIMEBASE_IN_MICROS;
	uint64_t end_index = end / INDEX_TIMEBASE_IN_MICROS;

	if (index.size() <= end_index) {
		index.resize(end_index + 1);
	}

	for (auto i = start_index; i <= end_index; ++i) {
		index[i].push_back(blocknum);
	}
}

static void SortedAddBlockToIndex(int blocknum, uint64_t start, uint64_t end, std::vector<std::vector<int>>& index) {
	uint64_t start_index = start / INDEX_TIMEBASE_IN_MICROS;
	uint64_t end_index = end / INDEX_TIMEBASE_IN_MICROS;

	if (index.size() <= end_index) {
		index.resize(end_index + 1);
	}

	for (auto i = start_index; i <= end_index; ++i) {
		const auto pos = std::lower_bound(index[i].begin(), index[i].end(), blocknum);
		if ((pos == index[i].end()) || (*pos != blocknum)) {
			index[i].insert(pos, blocknum);
		}
	}
}

static void TraceThreadWriter(TraceThread_t* thread) {
	int curblock = 0;
	const auto fp = thread->fp;
	
	struct {
		uint32_t magic;
		uint32_t version;
		int numstacks;
		int numtags;
		int numblocks;
		int numindexblocks;
		int maxparents;
		int padd;
		uint64_t stackofs;
		uint64_t tagofs;
		uint64_t indexofs;
		uint64_t micro_start;
		uint64_t micro_end;
		uint64_t timebase;
	} header;

	struct block_t {
		uint64_t start;
		uint64_t end;
		uint64_t childTime;
		uint32_t stackframe;
		uint32_t tag;
		int parent;
		int numparents;
	};

	struct StackFrame_t {
		char label[256];
		char location[256];
		uint64_t wallTime;
		uint64_t childTime;
		uint64_t callCount;
		uint64_t bestCallTime;
		uint64_t worstCallTime;
		int bestcall;
		int worstcall;
	};

	struct Tag_t {
		char string[256];
	};
	
	memset(&header, 0, sizeof(header));
	fwrite(&header, sizeof(header), 1, fp);

	std::vector<std::vector<int>> index;
	std::vector<StackFrame_t> stackFrames;
	std::vector<Tag_t> tags;
	std::vector<uint32_t> stackFrameIDs;
	std::vector<uint32_t> tagIDs;
	std::vector<int> rewriteBlocks;
	
	for (;;) {
		const auto numblocks = thread->writeblocks.load(std::memory_order_acquire);
		if (numblocks == -1) {
			TRACE_ASSERT(thread->next);
			thread = thread->next;
			continue;
		} else if (curblock < numblocks) {
			const auto count = numblocks - curblock;

			//trace_DebugWriteLine("--- Begin (%i blocks) ---", count);
			
			for (; curblock < numblocks; ++curblock) {
				const auto* block = TraceGetBlockNum(thread, curblock);
				block_t file_block;

				file_block.stackframe = block->location.crc;
				file_block.tag = block->tag ? trace_crc_str_32(block->tag) : 0;
				file_block.start = GetRelativeMicros(block->start);
				file_block.end = block->end ? GetRelativeMicros(block->end) : 0;
				file_block.parent = block->parent;
				file_block.numparents = 0;

				if (file_block.end) {
					file_block.childTime = block->childTime;
				} else {
					file_block.childTime = 0;
					// this block is currently unterminated and will not
					// have correct timing counts in child stack frames.
					rewriteBlocks.push_back(curblock);
				}

				for (auto parent = block->parent; parent != -1; parent = TraceGetBlockNum(thread, parent)->parent) {
					TRACE_ASSERT(parent < curblock);
					++file_block.numparents;
				}

				header.maxparents = std::max(header.maxparents, file_block.numparents);

				fwrite(&file_block, sizeof(file_block), 1, fp);

				if (file_block.end) {
					UnsortedAddBlockToIndex(curblock, file_block.start, file_block.end, index);
				}

				{
					const auto pos = std::lower_bound(stackFrameIDs.begin(), stackFrameIDs.end(), file_block.stackframe);
					if ((pos == stackFrameIDs.end()) || (*pos != file_block.stackframe)) {
						const auto idx = pos - stackFrameIDs.begin();
						stackFrameIDs.insert(pos, file_block.stackframe);

						StackFrame_t frame;
						memset(&frame, 0, sizeof(frame));
						strcpy_s(frame.label, block->label.str);
						strcpy_s(frame.location, block->location.str);
						frame.callCount = 1;
						frame.wallTime = file_block.end ? file_block.end - file_block.start : 0;
						frame.bestCallTime = frame.wallTime;
						frame.worstCallTime = frame.wallTime;
						frame.bestcall = curblock;
						frame.worstcall = curblock;
						stackFrames.insert(idx + stackFrames.begin(), frame);
					} else {
						const auto idx = pos - stackFrameIDs.begin();
						auto& stackFrame = stackFrames[idx];
						++stackFrame.callCount;
						if (file_block.end) {
							const auto wallTime = file_block.end - file_block.start;
							stackFrame.wallTime += wallTime;
							if (wallTime < stackFrame.bestCallTime) {
								stackFrame.bestCallTime = wallTime;
								stackFrame.bestcall = curblock;
							}
							if (wallTime > stackFrame.worstCallTime) {
								stackFrame.worstCallTime = wallTime;
								stackFrame.worstcall = curblock;
							}
						}
					}
				}

				if (block->tag) {
					const auto pos = std::lower_bound(tagIDs.begin(), tagIDs.end(), file_block.tag);
					if ((pos == tagIDs.end()) || (*pos != file_block.tag)) {
						const auto idx = pos - tagIDs.begin();
						tagIDs.insert(pos, file_block.tag);

						Tag_t t;
						strcpy_s(t.string, block->tag);
						tags.insert(idx + tags.begin(), t);
					}
				}

				if (file_block.end) {
					if (block->parent != -1) {
						const auto* parentBlock = TraceGetBlockNum(thread, block->parent);
						const auto stackpos = std::lower_bound(stackFrameIDs.begin(), stackFrameIDs.end(), parentBlock->location.crc);
						TRACE_ASSERT(stackpos != stackFrameIDs.end());
						TRACE_ASSERT(*stackpos == parentBlock->location.crc);
						const auto idx = stackpos - stackFrameIDs.begin();
						auto& stackFrame = stackFrames[idx];
						stackFrame.childTime += file_block.end - file_block.start;
					}
				}
			}

			//trace_DebugWriteLine("--- End (%i blocks) ---", count);
		} else {
			if (thread->stack == -2) {
				break;
			}

			//trace_DebugWriteLine("Trace: sleeping.");
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	const uint64_t stackOfs = ftello64(fp);

	trace_DebugWriteLine("Trace: indexing file [%s]...", thread->path);
	for (auto& ii : index) {
		std::sort(ii.begin(), ii.end());
	}
	trace_DebugWriteLine("Rewriting %i block(s)...", (int)rewriteBlocks.size());

	// rewrite blocks!
	for (const auto blocknum : rewriteBlocks) {
		const int64_t ofs = sizeof(header) + (blocknum * sizeof(block_t));
		fseeko64(fp, ofs, SEEK_SET);

		const auto* block = TraceGetBlockNum(thread, blocknum);

		TRACE_ASSERT(block->end);

		block_t file_block;
		file_block.stackframe = block->location.crc;
		file_block.tag = block->tag ? trace_crc_str_32(block->tag) : 0;
		file_block.start = GetRelativeMicros(block->start);
		file_block.end = GetRelativeMicros(block->end);
		file_block.parent = block->parent;
		file_block.childTime = block->childTime;
		file_block.numparents = 0;
		
		for (auto parent = block->parent; parent != -1; parent = TraceGetBlockNum(thread, parent)->parent) {
			TRACE_ASSERT(parent < curblock);
			++file_block.numparents;
		}

		if (file_block.end) {
			SortedAddBlockToIndex(blocknum, file_block.start, file_block.end, index);

			const auto pos = std::lower_bound(stackFrameIDs.begin(), stackFrameIDs.end(), file_block.stackframe);
			TRACE_ASSERT(pos != stackFrameIDs.end());
			TRACE_ASSERT(*pos == file_block.stackframe);

			{
				const auto idx = pos - stackFrameIDs.begin();
				auto& stackFrame = stackFrames[idx];
				
				const auto wallTime = file_block.end - file_block.start;
				stackFrame.wallTime += wallTime;
				if (wallTime < stackFrame.bestCallTime) {
					stackFrame.bestCallTime = wallTime;
					stackFrame.bestcall = curblock;
				}
				if (wallTime > stackFrame.worstCallTime) {
					stackFrame.worstCallTime = wallTime;
					stackFrame.worstcall = curblock;
				}
			}

			if (block->parent != -1 ){
				const auto* parentBlock = TraceGetBlockNum(thread, block->parent);
				const auto stackpos = std::lower_bound(stackFrameIDs.begin(), stackFrameIDs.end(), parentBlock->location.crc);
				TRACE_ASSERT(stackpos != stackFrameIDs.end());
				TRACE_ASSERT(*stackpos == parentBlock->location.crc);
				const auto idx = stackpos - stackFrameIDs.begin();
				auto& stackFrame = stackFrames[idx];
				stackFrame.childTime += file_block.end - file_block.start;
			}
		}

		fwrite(&file_block, sizeof(file_block), 1, fp);
	}

	fseeko64(fp, stackOfs, SEEK_SET);

	if (stackFrames.size()) {
		TRACE_VERIFY(stackFrames.size() == stackFrameIDs.size());
		fwrite(&stackFrameIDs[0], sizeof(stackFrameIDs[0]), stackFrames.size(), fp);
		fwrite(&stackFrames[0], sizeof(stackFrames[0]), stackFrames.size(), fp);
	}

	const uint64_t tagOfs = ftello64(fp);

	if (tags.size()) {
		TRACE_VERIFY(tags.size() == tagIDs.size());
		fwrite(&tagIDs[0], sizeof(tagIDs[0]), tagIDs.size(), fp);
		fwrite(&tags[0], sizeof(tags[0]), tags.size(), fp);
	}

	const uint64_t indexOfs = ftello64(fp);

	// write index at end of file
	for (auto& i : index) {
		int count = (int)i.size();
		fwrite(&count, sizeof(count), 1, fp);
		if (count > 0) {
			fwrite(&i[0], sizeof(i[0]), count, fp);
		}
	}

	header.magic = TRACE_FOURCC('T', 'R', 'A', 'C');
	header.version = 2;
	header.numstacks = (int)stackFrames.size();
	header.numtags = (int)tags.size();
	header.numblocks = thread->writeblocks;
	header.numindexblocks = (int)index.size();
	header.stackofs = stackOfs;
	header.tagofs = tagOfs;
	header.indexofs = indexOfs;
	header.micro_start = thread->micro_start - s_microStart;
	header.micro_end = thread->micro_end - s_microStart;
	header.timebase = INDEX_TIMEBASE_IN_MICROS;
	fseek(fp, 0, SEEK_SET);
	fwrite(&header, sizeof(header), 1, fp);

	fclose(fp);

	trace_DebugWriteLine("Trace: wrote %i stack frames to [%s].", header.numblocks, thread->path);

	TraceThread_t* prev = nullptr;
	for (; thread; thread = prev) {
		prev = thread->prev;
		free(thread);
	}
}

void TraceThreadReset(int reset) {
	auto thread = __tr_thread;
	if (thread && (thread->reset < reset) && (thread->blockbase == 0) && (thread->stack >= 0)) {
		thread->micro_start = GetMicroseconds();
		thread->micro_end = 0;
		thread->reset = reset;
		thread->numblocks = thread->stack + 1;
		thread->_blocks[thread->stack].childTime = 0;
	}
}

void TraceBeginThread(const char* name, uint32_t id) {

	TRACE_ASSERT(!__tr_thread);
	TRACE_VERIFY(s_init);
	
	auto thread = TraceThreadGrow();
	thread->id = id;
	thread->numblocks = 0;
	thread->stack = -1;
	thread->micro_start = GetMicroseconds();
	//thread->tsc_start = TRACE_RDTSC();

	sprintf_s(thread->path, "%s.%s.%u.trace", &s_tracePath[0], name, id);

#ifdef _WIN32
	if (fopen_s(&thread->fp, thread->path, "wb")) {
		thread->fp = nullptr;
	}
#else
	thread->fp = fopen(thread->path, "wb");
#endif

	TRACE_VERIFY(thread->fp);
	trace_DebugWriteLine("TraceProfiler opened [%s]", thread->path);
	
	LOCK L(M);
	s_writeThreads.push_back(std::thread(TraceThreadWriter, thread));
}

void TraceEndThread() {
	auto thread = __tr_thread;

	TRACE_ASSERT(thread);
	TRACE_ASSERT(thread->stack == -1);
	
	thread->micro_end = GetMicroseconds();
	//thread->tsc_end = TRACE_RDTSC();
	thread->stack = -2;
	thread->writeblocks.store(thread->numblocks, std::memory_order_release);
	thread = nullptr;
}

uint32_t TraceGetCurrentThreadID() {
#ifdef _WIN32
	return (uint32_t)GetCurrentThreadId();
#endif
}

void TraceInit(const char* path) {
	TRACE_VERIFY(!s_init);

	if (!s_init) {
		s_init = true;
		const auto micro_start = GetMicroseconds();
		const auto tsc_start = TRACE_RDTSC();

		uint64_t micro_end;
		uint64_t tsc_end;

		for (;;) {
			tsc_end = TRACE_RDTSC();
			micro_end = GetMicroseconds();
			if ((micro_end - micro_start) > 100000) {
				break;
			}
		}

		s_ticksPerMicro = (tsc_end - tsc_start) / (micro_end - micro_start);

#ifdef _WIN32
		strcpy_s(s_tracePath, path);
#else
		strcpy(&s_tracePath[0], path);
#endif
		s_tscStart = TRACE_RDTSC();
		s_microStart = GetMicroseconds();
	}
}

void TraceShutdown() {
	trace_DebugWriteLine("TraceProfiler flushing trace data...");
	s_init = false;
	LOCK L(M);
	for (auto& thread : s_writeThreads) {
		thread.join();
	}
	trace_DebugWriteLine("TraceProfiler done.");
}

#define TRACE_NULL_API
__TRACEPUSHFN(TRACE_NULL_API, __TracePush)
__TRACEPOPFN(TRACE_NULL_API, __TracePop)

#endif
