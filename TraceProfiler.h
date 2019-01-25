// Copyright (c) 2019 Pocketwatch Games, LLC.

#pragma once

#if defined(TRACE_PROFILER) || defined(BUILDING_TRACE_PROFILER)

#ifdef TRACE_INCLUDE_FIRST
#include TRACE_INCLUDE_FIRST
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4365 4548 4774)
#endif
#include <stdint.h>
#include <atomic>

#ifndef TRACE_ASSERT
#define __TRACE_DEFINED_ASSERT
#define TRACE_ASSERT(_x)
#endif

#ifdef _MSC_VER
#include <intrin.h>
#define THREAD_LOCAL __declspec(thread)
#ifdef TRACE_DLL
#define TRACE_DLL_EXPORT __declspec(dllexport)
#define TRACE_DLL_IMPORT __declspec(dllimport)
#else
#define TRACE_DLL_IMPORT
#define TRACE_DLL_EXPORT
#endif
#else
#define TRACE_DLL_IMPORT
#define TRACE_DLL_EXPORT
#endif

#ifdef BUILDING_TRACE_PROFILER
#define TRACE_API TRACE_DLL_EXPORT
#else
#define TRACE_API TRACE_DLL_IMPORT
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define TRACE_RDTSC() __rdtsc()

class TraceNotCopyable {
public:
	TraceNotCopyable() = default;
	TraceNotCopyable(const TraceNotCopyable&) = delete;
	TraceNotCopyable& operator = (const TraceNotCopyable&) = delete;
};

/*
=======================================
Compile Time CRC32, adapted from:

// https://github.com/lammertb/libcrc
// https://github.com/lammertb/libcrc/blob/master/LICENSE
* Library: libcrc
* File:    include/checksum.h
* Author:  Lammert Bies
*
* This file is licensed under the MIT License as stated below
*
* Copyright (c) 1999-2016 Lammert Bies
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Description
* -----------
* The headerfile include/checksum.h contains the definitions and prototypes
* for routines that can be used to calculate several kinds of checksums.

=======================================
*/

static constexpr uint32_t trace_crc_tab32[256] = {
	0x00000000ul,
	0x77073096ul,
	0xEE0E612Cul,
	0x990951BAul,
	0x076DC419ul,
	0x706AF48Ful,
	0xE963A535ul,
	0x9E6495A3ul,
	0x0EDB8832ul,
	0x79DCB8A4ul,
	0xE0D5E91Eul,
	0x97D2D988ul,
	0x09B64C2Bul,
	0x7EB17CBDul,
	0xE7B82D07ul,
	0x90BF1D91ul,
	0x1DB71064ul,
	0x6AB020F2ul,
	0xF3B97148ul,
	0x84BE41DEul,
	0x1ADAD47Dul,
	0x6DDDE4EBul,
	0xF4D4B551ul,
	0x83D385C7ul,
	0x136C9856ul,
	0x646BA8C0ul,
	0xFD62F97Aul,
	0x8A65C9ECul,
	0x14015C4Ful,
	0x63066CD9ul,
	0xFA0F3D63ul,
	0x8D080DF5ul,
	0x3B6E20C8ul,
	0x4C69105Eul,
	0xD56041E4ul,
	0xA2677172ul,
	0x3C03E4D1ul,
	0x4B04D447ul,
	0xD20D85FDul,
	0xA50AB56Bul,
	0x35B5A8FAul,
	0x42B2986Cul,
	0xDBBBC9D6ul,
	0xACBCF940ul,
	0x32D86CE3ul,
	0x45DF5C75ul,
	0xDCD60DCFul,
	0xABD13D59ul,
	0x26D930ACul,
	0x51DE003Aul,
	0xC8D75180ul,
	0xBFD06116ul,
	0x21B4F4B5ul,
	0x56B3C423ul,
	0xCFBA9599ul,
	0xB8BDA50Ful,
	0x2802B89Eul,
	0x5F058808ul,
	0xC60CD9B2ul,
	0xB10BE924ul,
	0x2F6F7C87ul,
	0x58684C11ul,
	0xC1611DABul,
	0xB6662D3Dul,
	0x76DC4190ul,
	0x01DB7106ul,
	0x98D220BCul,
	0xEFD5102Aul,
	0x71B18589ul,
	0x06B6B51Ful,
	0x9FBFE4A5ul,
	0xE8B8D433ul,
	0x7807C9A2ul,
	0x0F00F934ul,
	0x9609A88Eul,
	0xE10E9818ul,
	0x7F6A0DBBul,
	0x086D3D2Dul,
	0x91646C97ul,
	0xE6635C01ul,
	0x6B6B51F4ul,
	0x1C6C6162ul,
	0x856530D8ul,
	0xF262004Eul,
	0x6C0695EDul,
	0x1B01A57Bul,
	0x8208F4C1ul,
	0xF50FC457ul,
	0x65B0D9C6ul,
	0x12B7E950ul,
	0x8BBEB8EAul,
	0xFCB9887Cul,
	0x62DD1DDFul,
	0x15DA2D49ul,
	0x8CD37CF3ul,
	0xFBD44C65ul,
	0x4DB26158ul,
	0x3AB551CEul,
	0xA3BC0074ul,
	0xD4BB30E2ul,
	0x4ADFA541ul,
	0x3DD895D7ul,
	0xA4D1C46Dul,
	0xD3D6F4FBul,
	0x4369E96Aul,
	0x346ED9FCul,
	0xAD678846ul,
	0xDA60B8D0ul,
	0x44042D73ul,
	0x33031DE5ul,
	0xAA0A4C5Ful,
	0xDD0D7CC9ul,
	0x5005713Cul,
	0x270241AAul,
	0xBE0B1010ul,
	0xC90C2086ul,
	0x5768B525ul,
	0x206F85B3ul,
	0xB966D409ul,
	0xCE61E49Ful,
	0x5EDEF90Eul,
	0x29D9C998ul,
	0xB0D09822ul,
	0xC7D7A8B4ul,
	0x59B33D17ul,
	0x2EB40D81ul,
	0xB7BD5C3Bul,
	0xC0BA6CADul,
	0xEDB88320ul,
	0x9ABFB3B6ul,
	0x03B6E20Cul,
	0x74B1D29Aul,
	0xEAD54739ul,
	0x9DD277AFul,
	0x04DB2615ul,
	0x73DC1683ul,
	0xE3630B12ul,
	0x94643B84ul,
	0x0D6D6A3Eul,
	0x7A6A5AA8ul,
	0xE40ECF0Bul,
	0x9309FF9Dul,
	0x0A00AE27ul,
	0x7D079EB1ul,
	0xF00F9344ul,
	0x8708A3D2ul,
	0x1E01F268ul,
	0x6906C2FEul,
	0xF762575Dul,
	0x806567CBul,
	0x196C3671ul,
	0x6E6B06E7ul,
	0xFED41B76ul,
	0x89D32BE0ul,
	0x10DA7A5Aul,
	0x67DD4ACCul,
	0xF9B9DF6Ful,
	0x8EBEEFF9ul,
	0x17B7BE43ul,
	0x60B08ED5ul,
	0xD6D6A3E8ul,
	0xA1D1937Eul,
	0x38D8C2C4ul,
	0x4FDFF252ul,
	0xD1BB67F1ul,
	0xA6BC5767ul,
	0x3FB506DDul,
	0x48B2364Bul,
	0xD80D2BDAul,
	0xAF0A1B4Cul,
	0x36034AF6ul,
	0x41047A60ul,
	0xDF60EFC3ul,
	0xA867DF55ul,
	0x316E8EEFul,
	0x4669BE79ul,
	0xCB61B38Cul,
	0xBC66831Aul,
	0x256FD2A0ul,
	0x5268E236ul,
	0xCC0C7795ul,
	0xBB0B4703ul,
	0x220216B9ul,
	0x5505262Ful,
	0xC5BA3BBEul,
	0xB2BD0B28ul,
	0x2BB45A92ul,
	0x5CB36A04ul,
	0xC2D7FFA7ul,
	0xB5D0CF31ul,
	0x2CD99E8Bul,
	0x5BDEAE1Dul,
	0x9B64C2B0ul,
	0xEC63F226ul,
	0x756AA39Cul,
	0x026D930Aul,
	0x9C0906A9ul,
	0xEB0E363Ful,
	0x72076785ul,
	0x05005713ul,
	0x95BF4A82ul,
	0xE2B87A14ul,
	0x7BB12BAEul,
	0x0CB61B38ul,
	0x92D28E9Bul,
	0xE5D5BE0Dul,
	0x7CDCEFB7ul,
	0x0BDBDF21ul,
	0x86D3D2D4ul,
	0xF1D4E242ul,
	0x68DDB3F8ul,
	0x1FDA836Eul,
	0x81BE16CDul,
	0xF6B9265Bul,
	0x6FB077E1ul,
	0x18B74777ul,
	0x88085AE6ul,
	0xFF0F6A70ul,
	0x66063BCAul,
	0x11010B5Cul,
	0x8F659EFFul,
	0xF862AE69ul,
	0x616BFFD3ul,
	0x166CCF45ul,
	0xA00AE278ul,
	0xD70DD2EEul,
	0x4E048354ul,
	0x3903B3C2ul,
	0xA7672661ul,
	0xD06016F7ul,
	0x4969474Dul,
	0x3E6E77DBul,
	0xAED16A4Aul,
	0xD9D65ADCul,
	0x40DF0B66ul,
	0x37D83BF0ul,
	0xA9BCAE53ul,
	0xDEBB9EC5ul,
	0x47B2CF7Ful,
	0x30B5FFE9ul,
	0xBDBDF21Cul,
	0xCABAC28Aul,
	0x53B39330ul,
	0x24B4A3A6ul,
	0xBAD03605ul,
	0xCDD70693ul,
	0x54DE5729ul,
	0x23D967BFul,
	0xB3667A2Eul,
	0xC4614AB8ul,
	0x5D681B02ul,
	0x2A6F2B94ul,
	0xB40BBE37ul,
	0xC30C8EA1ul,
	0x5A05DF1Bul,
	0x2D02EF8Dul
};

constexpr uint32_t trace_crc_32_constexpr(const char *input_str, size_t num_bytes) {

	uint32_t crc = 0xFFFFFFFFul;
	const char *ptr = input_str;

	for (size_t a = 0; a < num_bytes; a++) {

		crc = (crc >> 8) ^ trace_crc_tab32[(crc ^ (uint32_t)*ptr++) & 0x000000FFul];
	}

	return (crc ^ 0xFFFFFFFFul);

}

uint32_t trace_crc_str_32(const char* str);

/*
===============================================================================
trace_crcstr_t, crcname_t

These are workhorse classes for easy-to-use compile time CRCs in game code.

trace_crcstr_t keeps a const char* to the originating string which works for static 
string data. Or data that does not relocate while it is referenced by a trace_crcstr_t.

If you want to CRC data that is loaded at runtime using temporary buffers then use 
a crcname_t which has an enough internal storage to keep the first 32 characters 
including the null terminator and will allow debugger inspection of the string
data. crcname_t can also work like trace_crcstr_t for static string data that is crc'ed
at compile time (without the 32 character limit).

When BARE_CRCNAME is defined, then crcname_t will no longer have a reference to
any string data.
===============================================================================
*/

static const struct trace_crc_runtime_tag_t {} trace_crc_runtime_tag;
static const struct trace_crc_null_tag_t {} trace_crc_null_tag;

// trace_crcstr_t can only be used for static string data
// or data that doesn't move for as long as it is
// referenced by a trace_crcstr_t.

struct trace_crcstr_t {
	typedef void (trace_crcstr_t::*bool_type)();

	constexpr trace_crcstr_t() : crc(), str() {}
	constexpr trace_crcstr_t(const trace_crcstr_t&) = default;
	constexpr trace_crcstr_t(const char* s, uint32_t c) : str(s), crc(c) {}
	trace_crcstr_t(const char* arr, const trace_crc_runtime_tag_t&) : trace_crcstr_t(arr, trace_crc_str_32(arr)) {}
	explicit constexpr trace_crcstr_t(const trace_crc_null_tag_t&) : crc(0), str(nullptr) {}

	template <size_t N>
	constexpr trace_crcstr_t(const char(&arr)[N]) : str(arr), crc(trace_crc_32_constexpr(arr, N - 1)) {}
		
	trace_crcstr_t& operator = (const trace_crcstr_t& s) {
		*const_cast<const char**>(&str) = s.str;
		*const_cast<uint32_t*>(&crc) = s.crc;
		return *this;
	}

	bool operator == (const trace_crcstr_t& s) const {
		return crc == s.crc;
	}

	bool operator != (const trace_crcstr_t& s) const {
		return crc != s.crc;
	}

	bool operator < (const trace_crcstr_t& s) const {
		return crc < s.crc;
	}

	operator bool_type() const {
		return crc ? &trace_crcstr_t::boolfn : nullptr;
	}

	const char* const str;
	const uint32_t crc;
private:
	void boolfn() {}
};

static constexpr trace_crcstr_t trace_crcstr_null(trace_crc_null_tag);

struct TraceBlock_t {
	trace_crcstr_t label;
	trace_crcstr_t location;
	uint64_t start;
	uint64_t end;
	int parent;
};

struct TraceThread_t {
	uint64_t micro_start;
	uint64_t micro_end;
	int numblocks;
	int maxblocks;
	int stack;
	uint32_t id;
	TraceThread_t* realloced;
	FILE* fp;
	std::atomic_int writeblocks;
	TraceBlock_t blocks[1];
};

#define TRACE_GROW_SIZE (1024*1024)

TRACE_API TraceThread_t* TraceThreadGrow();
TRACE_API void TraceInit(const char* path);
TRACE_API void TraceBeginThread(const char* name, uint32_t id);
TRACE_API void TraceEndThread();
TRACE_API void TraceWriteBlocks();
TRACE_API void TraceShutdown();
TRACE_API uint32_t TraceGetCurrentThreadID();

#define __TRACEPUSHFN(_linkage, _name) \
_linkage void _name(trace_crcstr_t label, trace_crcstr_t location) { \
	auto thread = __tr_thread; \
	auto index = thread->numblocks; \
	if (index + 1 >= thread->maxblocks) {\
		thread = TraceThreadGrow();\
	}\
	thread->numblocks = index + 1;\
	auto& block = thread->blocks[index];\
	block.label = label;\
	block.location = location;\
	block.parent = thread->stack;\
	thread->stack = index;\
	block.end = 0;\
	block.start = TRACE_RDTSC();\
}

#define __TRACEPOPFN(_linkage, _name) \
_linkage void _name() {\
	auto thread = __tr_thread;\
	TRACE_ASSERT(thread->stack >= 0);\
	TRACE_ASSERT(thread->stack < thread->numblocks);\
	auto& block = thread->blocks[thread->stack];\
	thread->stack = block.parent;\
	block.end = TRACE_RDTSC();\
}

#ifdef TRACE_INLINE
extern TRACE_API THREAD_LOCAL TraceThread_t* __tr_thread;
__TRACEPUSHFN(inline, __TracePushInline)
__TRACEPOPFN(inline, __TracePopInline)
#define __TRACEPUSHFNNAME __TracePushInline
#define __TRACEPOPFNNAME __TracePopInline
#else
TRACE_API void __TracePush(trace_crcstr_t label, trace_crcstr_t location);
TRACE_API void __TracePop();
#define __TRACEPUSHFNNAME __TracePush
#define __TRACEPOPFNNAME __TracePop
#endif

struct __TR_BLOCKS : TraceNotCopyable {
	int count;

	~__TR_BLOCKS() {
		while (--count >= 0) {
			__TRACEPOPFNNAME();
		}
	}
};

struct __TR_BLOCKPOP : TraceNotCopyable {
	__TR_BLOCKPOP(__TR_BLOCKS* blocks) : _blocks(blocks) {}
	~__TR_BLOCKPOP() {
		--_blocks->count;
		__TRACEPOPFNNAME();
	}

private:
	__TR_BLOCKS* _blocks;
};

struct __TR_THREADPOP : TraceNotCopyable {
	~__TR_THREADPOP() {
		TraceEndThread();
	}
};

#define __TRPUSH(_label, _location) \
	{ ++__tr_blocks.count;\
		static constexpr trace_crcstr_t crclabel(_label);\
		static constexpr trace_crcstr_t crclocation(_location);\
		__TRACEPUSHFNNAME(crclabel, crclocation); \
	} ((void)0)

#define __TRLABEL(_label, _location) \
	if (__tr_blocks.count > 1) {--__tr_blocks.count; }\
	__TRPUSH(_label, _location)

#define TRLABEL(_label) __TRLABEL(_label, __FILE__ ":" STRINGIZE(__LINE__))

#define __TRBLOCK(_label, _location) \
	__TRPUSH(_label, _location); \
	__TR_BLOCKPOP __tr_block_pop_##__COUNTER__(&__tr_blocks)

#define TRBLOCK(_label) __TRBLOCK(_label, __FILE__ ":" STRINGIZE(__LINE__))

#define __TRACE(_label, _location) \
	__TR_BLOCKS __tr_blocks;\
	__tr_blocks.count = 0;\
	__TRPUSH(_label, _location)

#define TRACE() __TRACE(__FUNCTION__, __FILE__ ":" STRINGIZE(__LINE__))

#define TRACE_WRITEBLOCKS() TraceWriteBlocks()

#define TRTHREADPROC(_name) \
	__TR_THREADPOP __tr_pop; \
	TraceBeginThread(_name, TraceGetCurrentThreadID())

#ifdef __TRACE_DEFINED_ASSERT
#undef __TRACE_DEFINED_ASSERT
#undef TRACE_ASSERT
#endif

#else

#define TRBLOCK(_label) ((void)0)
#define TRLABEL(_label) ((void)0)
#define TRACE() ((void)0)
#define TRTHREADPROC(_label) ((void)0)
#define TRACE_WRITEBLOCKS() ((void)0)

#endif
