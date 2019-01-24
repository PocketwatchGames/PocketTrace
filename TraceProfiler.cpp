// Copyright (c) 2019 Pocketwatch Games, LLC.

#if defined(TRACE_PROFILER)

#include "TraceProfiler.h"

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
#undef max
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif

uint32_t crc_str_32(const char* str) {
	uint32_t crc;

	crc = 0xFFFFFFFFul;

	while (*str != 0) {
		crc = (crc >> 8) ^ crc_tab32[(crc ^ (uint32_t)*str++) & 0x000000FFul];
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

THREAD_LOCAL TraceThread_t* __tr_thread;

inline uint64_t GetRelativeMicros(uint64_t tsc) {
	return ((tsc - s_tscStart) / s_ticksPerMicro);
}

void TraceWriteBlocks() {
	auto thread = __tr_thread;
	thread->writeblocks.store(thread->numblocks, std::memory_order_release);
}

TraceThread_t* TraceThreadGrow() {
	auto thread = __tr_thread;

	if (!thread) {
		thread = (TraceThread_t*)malloc(sizeof(TraceThread_t) + sizeof(TraceBlock_t) * (TRACE_GROW_SIZE - 1));
		thread->realloced = nullptr;
		thread->maxblocks = TRACE_GROW_SIZE;
		thread->writeblocks.store(0, std::memory_order_relaxed);
		__tr_thread = thread;
		return thread;
	}

	auto grow = (TraceThread_t*)malloc(sizeof(TraceThread_t) + sizeof(TraceBlock_t) * (thread->maxblocks + TRACE_GROW_SIZE - 1));
	memcpy(grow, thread, sizeof(TraceThread_t) + sizeof(TraceBlock_t) * (thread->maxblocks - 1));
	grow->maxblocks += TRACE_GROW_SIZE;

	thread->realloced = grow;
	thread->writeblocks.store(-1, std::memory_order_release);
		
	__tr_thread = grow;

	return grow;
}

static void AddBlockToIndex(int blocknum, uint64_t start, uint64_t end, std::vector<std::vector<int>>& index) {
	uint64_t start_index = start / INDEX_TIMEBASE_IN_MICROS;
	uint64_t end_index = end / INDEX_TIMEBASE_IN_MICROS;

	if (index.size() <= end_index) {
		index.resize(end_index + 1);
	}

	for (auto i = start_index; i <= end_index; ++i) {
		index[i].push_back(blocknum);
	}
}

static void TraceThreadWriter(TraceThread_t* thread) {
	int curblock = 0;
	const auto fp = thread->fp;
	
	struct {
		uint32_t magic;
		int numstacks;
		int numblocks;
		int numindexblocks;
		int maxparents;
		int padd;
		uint64_t stackofs;
		uint64_t indexofs;
		uint64_t micro_start;
		uint64_t micro_end;
		uint64_t timebase;
	} header;

	struct StackFrame_t {
		char label[256];
		char location[256];
	};
	
	memset(&header, 0, sizeof(header));
	fwrite(&header, sizeof(header), 1, fp);

	std::vector<std::vector<int>> index;
	std::vector<StackFrame_t> stackFrames;
	std::vector<uint32_t> stackFrameIDs;
	
	for (;;) {
		const auto numblocks = thread->writeblocks.load(std::memory_order_acquire);
		if (numblocks == -1) {
			TRACE_ASSERT(thread->realloced);
			auto old = thread;
			thread = thread->realloced;
			free(old);
			continue;
		} else if (curblock < numblocks) {
			struct {
				uint32_t stackframe;
				uint64_t start;
				uint64_t end;
				int parent;
				int numparents;
			} file_block;

			for (; curblock < numblocks; ++curblock) {
				const auto& block = thread->blocks[curblock];

				file_block.stackframe = block.location.crc;
				file_block.start = GetRelativeMicros(block.start);
				file_block.end = block.end ? GetRelativeMicros(block.end) : 0;
				file_block.parent = block.parent;
				file_block.numparents = 0;

				TRACE_ASSERT(block.parent < curblock);
				for (auto parent = block.parent; parent != -1; parent = thread->blocks[parent].parent) {
					++file_block.numparents;
				}

				header.maxparents = std::max(header.maxparents, file_block.numparents);

				fwrite(&file_block, sizeof(file_block), 1, fp);

				if (block.end) {
					AddBlockToIndex(curblock, file_block.start, file_block.end, index);
				}

				const auto pos = std::lower_bound(stackFrameIDs.begin(), stackFrameIDs.end(), file_block.stackframe);
				if ((pos == stackFrameIDs.end()) || (*pos != file_block.stackframe)) {
					const auto idx = pos - stackFrameIDs.begin();
					stackFrameIDs.insert(pos, file_block.stackframe);
					
					StackFrame_t frame;
					memset(&frame, 0, sizeof(frame));
					strcpy_s(frame.label, block.label.str);
					strcpy_s(frame.location, block.location.str);

					stackFrames.insert(idx + stackFrames.begin(), frame);
				}
			}

		} else {
			if (thread->stack == -2) {
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

	const uint64_t stackOfs = ftello64(fp);
	if (stackFrames.size()) {
		TRACE_VERIFY(stackFrames.size() == stackFrameIDs.size());
		fwrite(&stackFrameIDs[0], sizeof(stackFrameIDs[0]), stackFrames.size(), fp);
		fwrite(&stackFrames[0], sizeof(stackFrames[0]), stackFrames.size(), fp);
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
	header.numstacks = (int)stackFrames.size();
	header.numblocks = thread->writeblocks;
	header.numindexblocks = (int)index.size();
	header.stackofs = stackOfs;
	header.indexofs = indexOfs;
	header.micro_start = thread->micro_start - s_microStart;
	header.micro_end = thread->micro_end - s_microStart;
	header.timebase = INDEX_TIMEBASE_IN_MICROS;
	fseek(fp, 0, SEEK_SET);
	fwrite(&header, sizeof(header), 1, fp);

	fclose(fp);
	free(thread);
}

void TraceBeginThread(const char* name, uint32_t id) {
	TRACE_ASSERT(!__tr_thread);
	
	auto thread = TraceThreadGrow();
	thread->id = id;
	thread->numblocks = 0;
	thread->stack = -1;
	thread->micro_start = GetMicroseconds();
	//thread->tsc_start = TRACE_RDTSC();

	char path[1024];
	sprintf_s(path, "%s.%s.%u.trace", &s_tracePath[0], name, id);
	thread->fp = fopen(&path[0], "wb");

	TRACE_VERIFY(thread->fp);
	
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

	strcpy(&s_tracePath[0], path);
	s_tscStart = TRACE_RDTSC();
	s_microStart = GetMicroseconds();
}

void TraceShutdown() {

	LOCK L(M);
	for (auto& thread : s_writeThreads) {
		thread.join();
	}
}

#endif
