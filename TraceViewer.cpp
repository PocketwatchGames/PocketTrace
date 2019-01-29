

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4668 4018)
#endif

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/examples/imgui_impl_sdl.h"
#include "imgui/examples/imgui_impl_opengl3.h"
#include "imgui/examples/libs/gl3w/GL/gl3w.h"
#include "SDL2/include/SDL.h"
#include "mio/mmap.hpp"
#include <stdio.h>
#include <vector>
#include <array>
#include <algorithm>
#include <assert.h>
#include <memory>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define FOURCC(a, b, c, d) ((uint32_t)(((uint32_t)(a)) + (((uint32_t)(b))<<8) + (((uint32_t)(c))<<16)+ (((uint32_t)(d))<<24)))

#undef min
#undef max

static SDL_Window* s_window;
static bool s_generate;
static float s_ww, s_wh;
static uint64_t s_minTicks;
static uint64_t s_totalTicks;
static uint64_t s_vpTimeScale;
static uint64_t s_vpDisjointDT;
static float s_vpInvTimeScale;
static std::array<uint64_t, 2> s_vpTimeBounds;
static std::array<uint64_t, 2> s_oldvpTimeBounds;
static float s_scrollpos;
static bool s_inFlameChart;
static int s_timeScaleIndex;

enum ESetSelectedTab {
	SELECT_TAB_NONE,
	SELECT_TAB_FLAME_CHART
};

static ESetSelectedTab s_setSelectedTab;

static const std::array<uint64_t, 24> TIMESCALES = {
	10, // 10 microseconds
	25, // 25 microseconds
	50, // 50 microseconds
	100, // 100 microseconds
	250, // 256 microseconds
	500, // 500 microseconds
	1000, // one millisecond
	2000, // two milliseconds
	4000, // four milliseconds
	8000, // eight milliseconds
	16000, // sixteen milliseconds
	32000, // 32 milliseconds
	64000, // 64 milliseconds
	128000, // 128 milliseconds
	256000, // 256 milliseconds
	500000, // 500 milliseconds
	1000*1000, // 1 second
	2 * 1000 * 1000, // 2 seconds
	4 * 1000 * 1000, // 4 seconds
	8 * 1000 * 1000, // 8 seconds
	16 * 1000 * 1000, // 16 seconds
	30 * 1000 * 1000, // 30 seconds
	60 * 1000 * 1000, // 60 seconds
};

static void SetTimeScale(int index, uint64_t fixedTime) {
	double dtw;

	if (fixedTime > s_minTicks) {
		fixedTime -= s_minTicks;
	} else {
		fixedTime = 0;
	}

	if (fixedTime > s_vpTimeBounds[0]) {
		dtw = (fixedTime - s_vpTimeBounds[0]) * s_vpInvTimeScale * s_ww;
	} else {
		dtw = (s_vpTimeBounds[0] - fixedTime) * s_vpInvTimeScale * s_ww;
	}

	s_vpTimeScale = TIMESCALES[index]; // 1/30th second of time
	s_vpInvTimeScale = (float)(1.0 / s_vpTimeScale);
	s_vpDisjointDT = (uint64_t)(s_vpTimeScale / s_ww);

	if (fixedTime > s_vpTimeBounds[0]) {
		const auto newdtw = (fixedTime - s_vpTimeBounds[0]) * s_vpInvTimeScale * s_ww;
		if (newdtw > dtw) {
			const auto delta = (uint64_t)((newdtw - dtw) / s_ww * s_vpTimeScale);
			s_vpTimeBounds[0] += delta;
		} else {
			const auto delta = (uint64_t)((dtw - newdtw) / s_ww * s_vpTimeScale);
			if (s_vpTimeBounds[0] > delta) {
				s_vpTimeBounds[0] -= delta;
			} else {
				s_vpTimeBounds[0] = 0;
			}
		}
	} else {
		const auto newdtw = (s_vpTimeBounds[0] - fixedTime) * s_vpInvTimeScale * s_ww;
		if (newdtw > dtw) {
			const auto delta = (uint64_t)((newdtw - dtw) / s_ww * s_vpTimeScale);
			s_vpTimeBounds[0] += delta;
		} else {
			const auto delta = (uint64_t)((dtw - newdtw) / s_ww * s_vpTimeScale);
			if (s_vpTimeBounds[0] > delta) {
				s_vpTimeBounds[0] -= delta;
			} else {
				s_vpTimeBounds[0] = 0;
			}
		}
	}

	s_vpTimeBounds[1] = s_vpTimeBounds[0] + s_vpTimeScale;

	const auto maxscroll = s_totalTicks * s_vpInvTimeScale * s_ww;
	s_scrollpos = (float)(s_vpTimeBounds[0] / (double)(s_totalTicks - s_vpTimeScale)) * maxscroll;
}

static void TimescaleUp(uint64_t fixedTime) {
	if (s_timeScaleIndex < ((int)TIMESCALES.size()) - 1) {
		++s_timeScaleIndex;
		SetTimeScale(s_timeScaleIndex, fixedTime);
	}
}

static void TimescaleDown(uint64_t fixedTime) {
	if (s_timeScaleIndex > 0) {
		--s_timeScaleIndex;
		SetTimeScale(s_timeScaleIndex, fixedTime);
	}
}

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

struct TimingRecord_t {
	uint64_t start;
	uint64_t end;
	uint64_t childtime;
	uint32_t stackframe;
	int parent;
	int numparents;
};

struct IndexBlock_t {
	int numindices;
	int indices[1];
};

struct Span_t {
	uint64_t start;
	uint64_t end;
	float x;
	float w;
	uint32_t stackindex;
};

struct TraceFile_t {
	TraceFile_t() = default;
	TraceFile_t(TraceFile_t&& other) = default;

	~TraceFile_t() {
		if (indices) {
			free(indices);
		}
		if (spans) {
			delete[] spans;
		}
	}

	char path[1024];
	mio::mmap_source mmap;

	int numstacks;
	int numblocks;
	int numindexblocks;
	int maxparents;
	uint64_t micro_start;
	uint64_t micro_end;
	uint64_t timebase;

	const uint32_t* stackFrameIDs;
	const TimingRecord_t* blocks;
	const StackFrame_t* stackFrames;
	const IndexBlock_t** indices;

	std::vector<Span_t>* spans;
	std::vector<int> stacksByWall;
	std::vector<int> stacksByBest;
	std::vector<int> stacksByWorst;
	std::vector<int> stacksBySelf;

	bool collapsed;
};

std::vector<std::unique_ptr<TraceFile_t>> s_files;

struct BuildSpan_t {
	uint64_t start;
	uint64_t end;
	uint32_t stackframe;
};

static void FlushSpan(const TraceFile_t& trace, const BuildSpan_t& span, std::vector<Span_t>& spans) {
	if (span.stackframe) {
		const auto minx = std::max(span.start - s_minTicks, s_vpTimeBounds[0]) - s_vpTimeBounds[0];
		const auto maxx = std::min(span.end - s_minTicks, s_vpTimeBounds[1]) - s_vpTimeBounds[0];
		const auto w = (maxx - minx) * s_vpInvTimeScale * s_ww;
		if (w > 1) {
			Span_t drawspan;
			drawspan.x = minx * s_vpInvTimeScale * s_ww;
			drawspan.w = std::min(w, s_ww);
			drawspan.start = span.start;
			drawspan.end = span.end;

			const auto pos = std::lower_bound(trace.stackFrameIDs, trace.stackFrameIDs + trace.numstacks, span.stackframe);
			if ((pos != (trace.stackFrameIDs + trace.numstacks)) && (*pos == span.stackframe)) {
				drawspan.stackindex = (int)(pos - trace.stackFrameIDs);
				spans.push_back(drawspan);
			}
		}
	}
}

inline void AddSpan(const TraceFile_t& trace, BuildSpan_t& span, uint64_t start, uint64_t end, uint32_t stackframe, std::vector<Span_t>& spans) {
	if (stackframe != span.stackframe) {
		FlushSpan(trace, span, spans);
		span.start = start;
		span.end = end;
		span.stackframe = stackframe;
	} else {
		// check for disjoint
		if (span.stackframe) {
			if ((start < span.end) || ((start - span.end) > s_vpDisjointDT)) {
				FlushSpan(trace, span, spans);
				span.start = start;
				span.end = end;
				span.stackframe = stackframe;
				return;
			}
		}
		span.end = end;
	}
}

static void GenerateSpans(TraceFile_t& trace) {
	for (int i = 0; i < trace.maxparents + 1; ++i) {
		trace.spans[i].clear();
	}

	auto buildSpans = (BuildSpan_t*)alloca(sizeof(BuildSpan_t)*(trace.maxparents + 1));
	memset(buildSpans, 0, sizeof(BuildSpan_t)*(trace.maxparents + 1));

	for (int i = 0; i < trace.numblocks; ++i) {
		const auto& block = trace.blocks[i];
		if (block.end) {
			break;
		}
		assert(block.numparents <= trace.maxparents);
		if (((block.start - s_minTicks) < s_vpTimeBounds[1]) && ((trace.micro_end - s_minTicks) > s_vpTimeBounds[0])) {
			AddSpan(trace, buildSpans[block.numparents], block.start, trace.micro_end, block.stackframe, trace.spans[block.numparents]);
		}
	}

	const auto indexStart = (int)((s_vpTimeBounds[0]+ s_minTicks) / trace.timebase);
	const auto indexEnd = std::min((int)((s_vpTimeBounds[1] + s_minTicks) / trace.timebase), trace.numindexblocks-1);

	const TimingRecord_t* block;
	if ((indexStart < trace.numindexblocks) && (indexStart <= indexEnd)) {
		int lastBlockIndex = -1;

		for (auto i = indexStart; i <= indexEnd; ++i) {
			const auto indices = trace.indices[i];
			for (auto ii = 0; ii < indices->numindices; ++ii) {
				const auto blockindex = indices->indices[ii];
				assert(blockindex < trace.numblocks);
				if (blockindex > lastBlockIndex) {
					lastBlockIndex = blockindex;
					block = &trace.blocks[blockindex];
					assert(block->numparents <= trace.maxparents);
					if (((block->start - s_minTicks) < s_vpTimeBounds[1]) && ((block->end - s_minTicks) > s_vpTimeBounds[0])) {
						AddSpan(trace, buildSpans[block->numparents], block->start, block->end, block->stackframe, trace.spans[block->numparents]);
					}
				}
			}
		}
	}

	for (int i = 0; i < trace.maxparents+1; ++i) {
		FlushSpan(trace, buildSpans[i], trace.spans[i]);
	}
}

static void ShowTime(uint64_t time) {
	s_setSelectedTab = SELECT_TAB_FLAME_CHART;
	if (time > s_minTicks) {
		time -= s_minTicks;
	} else {
		time = 0;
	}
	s_timeScaleIndex = 4;
	SetTimeScale(s_timeScaleIndex, 0);
	s_scrollpos = (float)(time / (double)(s_totalTicks - s_vpTimeScale)) * s_totalTicks * s_vpInvTimeScale * s_ww;
	s_vpTimeBounds[0] = time;
	s_vpTimeBounds[1] = s_vpTimeBounds[0] + s_vpTimeScale;
}

static void ShowCall(const TraceFile_t& trace, int callnum) {
	s_setSelectedTab = SELECT_TAB_FLAME_CHART;
	ShowTime(trace.blocks[callnum].start);
}

static void ShowFirstCall(const TraceFile_t& trace, uint32_t stackid) {
	for (int i = 0; i < trace.numblocks; ++i) {
		if (trace.blocks[i].stackframe == stackid) {
			ShowCall(trace, i);
			return;
		}
	}
}

static void OpenTraceFile(const char* nativePath) {
	for (auto& tf : s_files) {
		if (!strcmp(&tf->path[0], nativePath)) {
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "That file is already open.", s_window);
			return;
		}
	}

	std::error_code error;
	auto mmap = mio::make_mmap_source(nativePath, error);
	if (error) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Unable to open file.", s_window);
		return;
	}

	struct header_t {
		uint32_t magic;
		uint32_t version;
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
	};

	const auto base = (const uint8_t*)mmap.data();
	const auto header = (const header_t*)base;

	if (header->magic != FOURCC('T', 'R', 'A', 'C')) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Bad signature, cannot open file.", s_window);
		return;
	}

	if (header->version != 1) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Unsupported file version, cannot open file.", s_window);
		return;
	}

	s_files.push_back(std::make_unique<TraceFile_t>());
	auto& trace = *s_files.back();
	strcpy_s(trace.path, nativePath);
	trace.collapsed = false;

	trace.mmap = std::move(mmap);
	trace.numstacks = header->numstacks;
	trace.numblocks = header->numblocks;
	trace.numindexblocks = header->numindexblocks;
	trace.maxparents = header->maxparents;
	trace.micro_start = header->micro_start;
	trace.micro_end = header->micro_end;
	trace.timebase = header->timebase;

	trace.blocks = (const TimingRecord_t*)(base + sizeof(header_t));
	trace.stackFrameIDs = (const uint32_t*)(base + header->stackofs);
	trace.stackFrames = (const StackFrame_t*)(base + header->stackofs + (sizeof(uint32_t) * header->numstacks));

	trace.indices = (const IndexBlock_t**)malloc(sizeof(IndexBlock_t*) * header->numindexblocks);

	{
		const uint8_t* indexptr = (base + header->indexofs);
		for (int i = 0; i < header->numindexblocks; ++i) {
			const auto block = (const IndexBlock_t*)indexptr;
			trace.indices[i] = block;
			indexptr += sizeof(int) + (sizeof(int) * block->numindices);
		}
	}

	for (int i = 0; i < trace.numstacks; ++i) {
		trace.stacksByWall.push_back(i);
	}

	trace.stacksByBest = trace.stacksByWall;
	trace.stacksBySelf = trace.stacksByWall;
	trace.stacksByWorst = trace.stacksByWall;

	std::sort(trace.stacksByWall.begin(), trace.stacksByWall.end(), [&](int a, int b) {
		return trace.stackFrames[a].wallTime > trace.stackFrames[b].wallTime;
	});

	std::sort(trace.stacksBySelf.begin(), trace.stacksBySelf.end(), [&](int a, int b) {
		const auto aself = (trace.stackFrames[a].wallTime - trace.stackFrames[a].childTime);
		const auto bself = (trace.stackFrames[b].wallTime - trace.stackFrames[b].childTime);
		return aself > bself;
	});

	std::sort(trace.stacksByBest.begin(), trace.stacksByBest.end(), [&](int a, int b) {
		const auto aavg = (trace.stackFrames[a].wallTime / (double)trace.stackFrames[a].callCount);
		const auto adelta = trace.stackFrames[a].bestCallTime / aavg;
		const auto bavg = (trace.stackFrames[b].wallTime / (double)trace.stackFrames[b].callCount);
		const auto bdelta = trace.stackFrames[b].bestCallTime / bavg;
		return adelta < bdelta;
	});

	std::sort(trace.stacksByWorst.begin(), trace.stacksByWorst.end(), [&](int a, int b) {
		const auto aavg = (trace.stackFrames[a].wallTime / (double)trace.stackFrames[a].callCount);
		const auto adelta = trace.stackFrames[a].worstCallTime / aavg;
		const auto bavg = (trace.stackFrames[b].wallTime / (double)trace.stackFrames[b].callCount);
		const auto bdelta = trace.stackFrames[b].worstCallTime / bavg;
		return adelta > bdelta;
	});

	trace.spans = new std::vector<Span_t>[trace.maxparents + 1];

	s_minTicks = trace.micro_start;

	uint64_t maxticks = 0;

	for (auto& tr : s_files) {
		s_minTicks = std::min(s_minTicks, tr->micro_start);
		maxticks = std::max(maxticks, tr->micro_end);
	}

	s_totalTicks = maxticks - s_minTicks;
	s_generate = true;
}

static bool CollapseButton(ImGuiID id, const ImVec2& pos, bool collapsed) {
	ImGuiContext& g = *GImGui;
	ImGuiWindow* window = g.CurrentWindow;

	ImRect bb(pos, pos + ImVec2(g.FontSize, g.FontSize) + g.Style.FramePadding * 2.0f);
	ImGui::ItemAdd(bb, id);
	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);

	ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
	if (hovered || held)
		window->DrawList->AddCircleFilled(bb.GetCenter() + ImVec2(0.0f, -0.5f), g.FontSize * 0.5f + 1.0f, col, 9);
	ImGui::RenderArrow(bb.Min + g.Style.FramePadding, collapsed ? ImGuiDir_Right : ImGuiDir_Down, 1.0f);

	return pressed;
}

static float CustomScrollbar(ImGuiLayoutType direction, float scrollPos, float scrollMax) {
	ImGuiContext& g = *GImGui;
	ImGuiWindow* window = g.CurrentWindow;

	const bool horizontal = (direction == ImGuiLayoutType_Horizontal);
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(horizontal ? "#SCROLLX" : "#SCROLLY");

	// Render background
	bool other_scrollbar = (horizontal ? window->ScrollbarY : window->ScrollbarX);
	float other_scrollbar_size_w = other_scrollbar ? style.ScrollbarSize : 0.0f;
	const ImRect window_rect = window->Rect();
	const float border_size = window->WindowBorderSize;
	ImRect bb = horizontal
		? ImRect(window->Pos.x + border_size, window_rect.Max.y - style.ScrollbarSize, window_rect.Max.x - other_scrollbar_size_w - border_size, window_rect.Max.y - border_size)
		: ImRect(window_rect.Max.x - style.ScrollbarSize, window->Pos.y + border_size, window_rect.Max.x - border_size, window_rect.Max.y - other_scrollbar_size_w - border_size);
	if (!horizontal)
		bb.Min.y += window->TitleBarHeight() + ((window->Flags & ImGuiWindowFlags_MenuBar) ? window->MenuBarHeight() : 0.0f);
	if (bb.GetWidth() <= 0.0f || bb.GetHeight() <= 0.0f)
		return scrollPos;

	int window_rounding_corners;
	if (horizontal)
		window_rounding_corners = ImDrawCornerFlags_BotLeft | (other_scrollbar ? 0 : ImDrawCornerFlags_BotRight);
	else
		window_rounding_corners = (((window->Flags & ImGuiWindowFlags_NoTitleBar) && !(window->Flags & ImGuiWindowFlags_MenuBar)) ? ImDrawCornerFlags_TopRight : 0) | (other_scrollbar ? 0 : ImDrawCornerFlags_BotRight);
	window->DrawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_ScrollbarBg), window->WindowRounding, window_rounding_corners);
	bb.Expand(ImVec2(-ImClamp((float)(int)((bb.Max.x - bb.Min.x - 2.0f) * 0.5f), 0.0f, 3.0f), -ImClamp((float)(int)((bb.Max.y - bb.Min.y - 2.0f) * 0.5f), 0.0f, 3.0f)));

	// V denote the main, longer axis of the scrollbar (= height for a vertical scrollbar)
	float scrollbar_size_v = horizontal ? bb.GetWidth() : bb.GetHeight();
	float scroll_v = scrollPos;
	float win_size_avail_v = (horizontal ? window->SizeFull.x : window->SizeFull.y) - other_scrollbar_size_w;
	float win_size_contents_v = scrollMax;

	// Calculate the height of our grabbable box. It generally represent the amount visible (vs the total scrollable amount)
	// But we maintain a minimum size in pixel to allow for the user to still aim inside.
	IM_ASSERT(ImMax(win_size_contents_v, win_size_avail_v) > 0.0f); // Adding this assert to check if the ImMax(XXX,1.0f) is still needed. PLEASE CONTACT ME if this triggers.
	const float win_size_v = ImMax(ImMax(win_size_contents_v, win_size_avail_v), 1.0f);
	const float grab_h_pixels = ImClamp(scrollbar_size_v * (win_size_avail_v / win_size_v), style.GrabMinSize, scrollbar_size_v);
	const float grab_h_norm = grab_h_pixels / scrollbar_size_v;

	// Handle input right away. None of the code of Begin() is relying on scrolling position before calling Scrollbar().
	bool held = false;
	bool hovered = false;
	const bool previously_held = (g.ActiveId == id);
	ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_NoNavFocus);

	float scroll_max = ImMax(1.0f, win_size_contents_v - win_size_avail_v);
	float scroll_ratio = ImSaturate(scroll_v / scroll_max);
	float grab_v_norm = scroll_ratio * (scrollbar_size_v - grab_h_pixels) / scrollbar_size_v;
	if (held && grab_h_norm < 1.0f)
	{
		float scrollbar_pos_v = horizontal ? bb.Min.x : bb.Min.y;
		float mouse_pos_v = horizontal ? g.IO.MousePos.x : g.IO.MousePos.y;
		float* click_delta_to_grab_center_v = horizontal ? &g.ScrollbarClickDeltaToGrabCenter.x : &g.ScrollbarClickDeltaToGrabCenter.y;

		// Click position in scrollbar normalized space (0.0f->1.0f)
		const float clicked_v_norm = ImSaturate((mouse_pos_v - scrollbar_pos_v) / scrollbar_size_v);
		ImGui::SetHoveredID(id);

		bool seek_absolute = false;
		if (!previously_held)
		{
			// On initial click calculate the distance between mouse and the center of the grab
			if (clicked_v_norm >= grab_v_norm && clicked_v_norm <= grab_v_norm + grab_h_norm)
			{
				*click_delta_to_grab_center_v = clicked_v_norm - grab_v_norm - grab_h_norm * 0.5f;
			} else
			{
				seek_absolute = true;
				*click_delta_to_grab_center_v = 0.0f;
			}
		}

		// Apply scroll
		// It is ok to modify Scroll here because we are being called in Begin() after the calculation of SizeContents and before setting up our starting position
		const float scroll_v_norm = ImSaturate((clicked_v_norm - *click_delta_to_grab_center_v - grab_h_norm * 0.5f) / (1.0f - grab_h_norm));
		scroll_v = (float)(int)(0.5f + scroll_v_norm * scroll_max);//(win_size_contents_v - win_size_v));
		
		// Update values for rendering
		scroll_ratio = ImSaturate(scroll_v / scroll_max);
		grab_v_norm = scroll_ratio * (scrollbar_size_v - grab_h_pixels) / scrollbar_size_v;

		// Update distance to grab now that we have seeked and saturated
		if (seek_absolute)
			*click_delta_to_grab_center_v = clicked_v_norm - grab_v_norm - grab_h_norm * 0.5f;
	}

	// Render
	const ImU32 grab_col = ImGui::GetColorU32(held ? ImGuiCol_ScrollbarGrabActive : hovered ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab);
	ImRect grab_rect;
	if (horizontal)
		grab_rect = ImRect(ImLerp(bb.Min.x, bb.Max.x, grab_v_norm), bb.Min.y, ImMin(ImLerp(bb.Min.x, bb.Max.x, grab_v_norm) + grab_h_pixels, window_rect.Max.x), bb.Max.y);
	else
		grab_rect = ImRect(bb.Min.x, ImLerp(bb.Min.y, bb.Max.y, grab_v_norm), bb.Max.x, ImMin(ImLerp(bb.Min.y, bb.Max.y, grab_v_norm) + grab_h_pixels, window_rect.Max.y));
	window->DrawList->AddRectFilled(grab_rect.Min, grab_rect.Max, grab_col, style.ScrollbarRounding);

	return scroll_v;
}

// Tip: pass a non-visible label (e.g. "##dummy") then you can use the space to draw other text or image.
// But you need to make sure the ID is unique, e.g. enclose calls in PushID/PopID or use ##unique_id.
static bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size_arg, ImU32 color) {
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;

	if ((flags & ImGuiSelectableFlags_SpanAllColumns) && window->DC.ColumnsSet) // FIXME-OPT: Avoid if vertically clipped.
		ImGui::PopClipRect();

	ImGuiID id = window->GetID(label);
	ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
	ImVec2 size(label_size.x, size_arg.y != 0.0f ? size_arg.y : label_size.y);
	ImVec2 pos = window->DC.CursorPos;
	pos.y += window->DC.CurrentLineTextBaseOffset;
	ImRect bb_inner(pos, pos + size);
	ImGui::ItemSize(bb_inner);

	// Fill horizontal space.
	ImVec2 window_padding = window->WindowPadding;
	float max_x = (flags & ImGuiSelectableFlags_SpanAllColumns) ? ImGui::GetWindowContentRegionMax().x : ImGui::GetContentRegionMax().x;
	float w_draw = ImMax(label_size.x, window->Pos.x + max_x - window_padding.x - window->DC.CursorPos.x);

	ImVec2 size_draw(w_draw, size_arg.y != 0.0f ? size_arg.y : size.y);
	ImRect bb(pos, pos + size_draw);
	bb.Max.x += window_padding.x;

	ImVec2 size_inner(size_arg.x*w_draw, size_arg.y != 0.0f ? size_arg.y : size.y);
	ImRect bb_inner2(pos, pos + size_inner);
	//if (size_arg.x == 0.0f)
	//	bb_inner2.Max.x += window_padding.x;

	// Selectables are tightly packed together, we extend the box to cover spacing between selectable.
	float spacing_L = (float)(int)(style.ItemSpacing.x * 0.5f);
	float spacing_U = (float)(int)(style.ItemSpacing.y * 0.5f);
	float spacing_R = style.ItemSpacing.x - spacing_L;
	float spacing_D = style.ItemSpacing.y - spacing_U;
	bb.Min.x -= spacing_L;
	bb.Min.y -= spacing_U;
	bb.Max.x += spacing_R;
	bb.Max.y += spacing_D;
	if (!ImGui::ItemAdd(bb, id))
	{
		if ((flags & ImGuiSelectableFlags_SpanAllColumns) && window->DC.ColumnsSet)
			ImGui::PushColumnClipRect();
		return false;
	}

	// We use NoHoldingActiveID on menus so user can click and _hold_ on a menu then drag to browse child entries
	ImGuiButtonFlags button_flags = 0;
	if (flags & ImGuiSelectableFlags_NoHoldingActiveID) button_flags |= ImGuiButtonFlags_NoHoldingActiveID;
	if (flags & ImGuiSelectableFlags_PressedOnClick) button_flags |= ImGuiButtonFlags_PressedOnClick;
	if (flags & ImGuiSelectableFlags_PressedOnRelease) button_flags |= ImGuiButtonFlags_PressedOnRelease;
	if (flags & ImGuiSelectableFlags_Disabled) button_flags |= ImGuiButtonFlags_Disabled;
	if (flags & ImGuiSelectableFlags_AllowDoubleClick) button_flags |= ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnDoubleClick;
	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, button_flags);
	if (flags & ImGuiSelectableFlags_Disabled)
		selected = false;

	// Hovering selectable with mouse updates NavId accordingly so navigation can be resumed with gamepad/keyboard (this doesn't happen on most widgets)
	if (pressed || hovered)
		if (!g.NavDisableMouseHover && g.NavWindow == window && g.NavLayer == window->DC.NavLayerCurrent)
		{
			g.NavDisableHighlight = true;
			ImGui::SetNavID(id, window->DC.NavLayerCurrent);
		}
	if (pressed)
		ImGui::MarkItemEdited(id);

	// Render
	if (hovered || selected)
	{
		const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
		ImGui::RenderFrame(bb.Min, bb.Max, col, false, 0.0f);
		ImGui::RenderNavHighlight(bb, id, ImGuiNavHighlightFlags_TypeThin | ImGuiNavHighlightFlags_NoRounding);	
	}

	ImGui::RenderFrame(bb_inner2.Min, bb_inner2.Max, color, false, 0.0f);

	if ((flags & ImGuiSelectableFlags_SpanAllColumns) && window->DC.ColumnsSet)
	{
		ImGui::PushColumnClipRect();
		bb.Max.x -= (ImGui::GetContentRegionMax().x - max_x);
	}

	if (flags & ImGuiSelectableFlags_Disabled) ImGui::PushStyleColor(ImGuiCol_Text, g.Style.Colors[ImGuiCol_TextDisabled]);
	ImGui::RenderTextClipped(bb_inner.Min, bb.Max, label, NULL, &label_size, ImVec2(0.0f, 0.0f));
	if (flags & ImGuiSelectableFlags_Disabled) ImGui::PopStyleColor();

	// Automatically close popups
	if (pressed && (window->Flags & ImGuiWindowFlags_Popup) && !(flags & ImGuiSelectableFlags_DontClosePopups) && !(window->DC.ItemFlags & ImGuiItemFlags_SelectableDontClosePopup))
		ImGui::CloseCurrentPopup();
	return pressed;
}

static bool Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags, const ImVec2& size_arg, ImU32 color) {
	if (Selectable(label, *p_selected, flags, size_arg, color))
	{
		*p_selected = !*p_selected;
		return true;
	}
	return false;
}

static void DrawTrace(TraceFile_t& trace) {
	static constexpr float TRACK_HEIGHT = 30;
	static constexpr float TRACK_SPACE = 5;

	//ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (s_vpTimeBounds[0] * s_invTimeScale * s_ww));
	auto pos = ImGui::GetCursorPos();
	auto size = pos;

	const auto screenPos = ImGui::GetCursorScreenPos();
	const auto fontSize = ImGui::GetCurrentContext()->FontSize;
	const auto titleBarSize = fontSize * 1.5f;

	ImGui::ButtonEx(trace.path, ImVec2(s_ww, titleBarSize), ImGuiButtonFlags_Disabled);
	pos = ImGui::GetCursorPos();
	size = pos;

	if (CollapseButton(ImGui::GetID("#COLLAPSE"), ImVec2(screenPos.x + 4 + fontSize * 0.5f, screenPos.y + 2.0f), trace.collapsed)) {
		trace.collapsed = !trace.collapsed;
	}
	
	if (!trace.collapsed) {
		int id = 0;
		for (int i = 0; i <= trace.maxparents; ++i) {
			const auto& spans = trace.spans[i];
			for (const auto& span : spans) {
				++id;
				ImGui::SetCursorPos(ImVec2(pos.x + span.x, size.y));

				const auto rgbmask = trace.stackFrameIDs[(int)span.stackindex] | 0xFF000000;
				const auto r = (rgbmask & 0xff) / 255.f;
				const auto g = ((rgbmask >> 8) & 0xff) / 255.f;
				const auto b = ((rgbmask >> 16) & 0xff) / 255.f;
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r, g, b, 1));
				ImGui::PushID(id);
				ImGui::Button(trace.stackFrames[span.stackindex].label, ImVec2(span.w, TRACK_HEIGHT));
				if (ImGui::IsItemHovered()) {
					const auto delta = span.end - span.start;

					ImGui::SetTooltip(
						"[%s]\n[%s]\n\nWall Time: [%.2f ms] [%u us]\nStart: [%u us]\nEnd: [%u us]",
						trace.stackFrames[span.stackindex].label,
						trace.stackFrames[span.stackindex].location,
						delta / 1000.f,
						delta,
						span.start,
						span.end
					);
				}
				ImGui::PopID();
				ImGui::PopStyleColor();
				size.x = std::max(size.x, pos.x + span.x + span.w);
			}
			size.y += TRACK_HEIGHT + TRACK_SPACE;
		}
	}
}

static void DrawFrame(float ww, float wh) {
//	const auto& io = ImGui::GetIO();
	const auto& g = *GImGui;
	const auto& style = ImGui::GetStyle();

	s_inFlameChart = false;

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(ww, wh));
	//ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.f));

	ImGui::Begin("##TraceViewerMain", nullptr, ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
	if (ImGui::BeginTabBar("Main Tabs")) {
		if (ImGui::BeginTabItem("Flame Chart", nullptr, (s_setSelectedTab==SELECT_TAB_FLAME_CHART) ? ImGuiTabItemFlags_SetSelected : 0)) {
			if (s_setSelectedTab == SELECT_TAB_FLAME_CHART) {
				s_setSelectedTab = SELECT_TAB_NONE;
			}
			s_inFlameChart = true;

			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::Spacing();

			bool first = true;

			// regenerate spans?
			if ((s_ww != ww) ||
				(s_wh != wh) ||
				(s_oldvpTimeBounds[0] != s_vpTimeBounds[0]) ||
				(s_oldvpTimeBounds[1] != s_vpTimeBounds[1])) {
				s_oldvpTimeBounds = s_vpTimeBounds;
				s_ww = ww;
				s_wh = wh;
				s_generate = true;
			}

			if (s_generate) {
				s_generate = false;
				for (auto& trace : s_files) {
					GenerateSpans(*trace);
				}
			}

			{
				int id = 0;
				for (auto& trace : s_files) {
					ImGui::PushID(id++);
					if (!first) {
						ImGui::Spacing();
					}
					first = false;
					DrawTrace(*trace);
					ImGui::PopID();
				}
			}

			const auto maxscroll = s_totalTicks * s_vpInvTimeScale * s_ww;

			const auto newscroll = CustomScrollbar(ImGuiLayoutType_Horizontal, s_scrollpos, maxscroll);

			if (newscroll != s_scrollpos) {
				s_scrollpos = newscroll;
				if ((s_totalTicks > s_vpTimeScale) && (maxscroll > s_ww)) {
					double frac = (double)(s_scrollpos / maxscroll);
					s_vpTimeBounds[0] = (uint64_t)(frac * (s_totalTicks - s_vpTimeScale));
					s_vpTimeBounds[1] = s_vpTimeBounds[0] + s_vpTimeScale;
				} else {
					s_vpTimeBounds[0] = 0;
					s_vpTimeBounds[1] = s_vpTimeScale;
				}
			}

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Wall Time")) {

			if (!s_files.empty()) {
				const auto numblocks = (int)s_files.size();
				
				int numitems = 0;
				for (auto& trace : s_files) {
					numitems += trace->numstacks;
				}

				const float predictedItemSize = (g.FontSize + style.ItemSpacing.y) * (numblocks + numitems);

				const auto pos = ImGui::GetCursorPosY();
				const auto space = ImGui::GetContentRegionAvail().y / numblocks;

				int numColumns = 1;
				if (predictedItemSize > space) {
					numColumns = std::max((int)(predictedItemSize / space) + 1, 4);
				}

				for (auto& trace : s_files) {
					const double total = (double)(trace->micro_end - trace->micro_start);

					Selectable(trace->path, false, ImGuiSelectableFlags_Disabled, ImVec2(1, 0), ImGui::GetColorU32(ImGuiCol_Header));

					ImGui::Columns(numColumns, NULL, false);

					for (int i = 0; i < trace->numstacks; i++) {
						
						const auto& stackframe = trace->stackFrames[trace->stacksByWall[i]];
						const auto rgbmask = (ImU32)(trace->stackFrameIDs[trace->stacksByWall[i]] | 0xFF000000);
						const auto frac = (float)(stackframe.wallTime / total);

						ImGui::PushID(&stackframe);
						if (Selectable(stackframe.label, false, 0, ImVec2(frac, 0), rgbmask)) {
							ShowFirstCall(*trace, trace->stackFrameIDs[trace->stacksByWall[i]]);
						}
						ImGui::PopID();

						if ((ImGui::GetCursorPosY() - pos) >= space) {
							ImGui::NextColumn();
						}
					}

					ImGui::Columns(1);
				}
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Self Time")) {
			if (!s_files.empty()) {
				const auto numblocks = (int)s_files.size();

				int numitems = 0;
				for (auto& trace : s_files) {
					numitems += trace->numstacks;
				}

				const float predictedItemSize = (g.FontSize + style.ItemSpacing.y) * (numblocks + numitems);

				const auto pos = ImGui::GetCursorPosY();
				const auto space = ImGui::GetContentRegionAvail().y / numblocks;

				int numColumns = 1;
				if (predictedItemSize > space) {
					numColumns = std::max((int)(predictedItemSize / space) + 1, 4);
				}

				for (auto& trace : s_files) {
					const double total = (double)(trace->micro_end - trace->micro_start);

					Selectable(trace->path, false, ImGuiSelectableFlags_Disabled, ImVec2(1, 0), ImGui::GetColorU32(ImGuiCol_Header));

					ImGui::Columns(numColumns, NULL, false);

					for (int i = 0; i < trace->numstacks; i++) {

						const auto& stackframe = trace->stackFrames[trace->stacksBySelf[i]];
						const auto rgbmask = (ImU32)(trace->stackFrameIDs[trace->stacksBySelf[i]] | 0xFF000000);
						const auto frac = (float)((stackframe.wallTime - stackframe.childTime) / total);

						ImGui::PushID(&stackframe);
						if (Selectable(stackframe.label, false, 0, ImVec2(frac, 0), rgbmask)) {
							ShowFirstCall(*trace, trace->stackFrameIDs[trace->stacksBySelf[i]]);
						}
						ImGui::PopID();

						if ((ImGui::GetCursorPosY() - pos) >= space) {
							ImGui::NextColumn();
						}
					}

					ImGui::Columns(1);
				}
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Best vs Avg")) {
			if (!s_files.empty()) {
				const auto numblocks = (int)s_files.size();

				int numitems = 0;
				for (auto& trace : s_files) {
					numitems += trace->numstacks;
				}

				const float predictedItemSize = (g.FontSize + style.ItemSpacing.y) * (numblocks + numitems);

				const auto pos = ImGui::GetCursorPosY();
				const auto space = ImGui::GetContentRegionAvail().y / numblocks;

				int numColumns = 1;
				if (predictedItemSize > space) {
					numColumns = std::max((int)(predictedItemSize / space) + 1, 4);
				}

				for (auto& trace : s_files) {
					const double total = (double)(trace->micro_end - trace->micro_start);

					Selectable(trace->path, false, ImGuiSelectableFlags_Disabled, ImVec2(1, 0), ImGui::GetColorU32(ImGuiCol_Header));

					ImGui::Columns(numColumns, NULL, false);

					for (int i = 0; i < trace->numstacks; i++) {

						const auto& stackframe = trace->stackFrames[trace->stacksByBest[i]];
						const auto rgbmask = (ImU32)(trace->stackFrameIDs[trace->stacksByBest[i]] | 0xFF000000);
						const auto avg = stackframe.wallTime / (double)stackframe.callCount;
						const auto delta = 1.f-(float)std::min(stackframe.bestCallTime / avg, 1.);

						ImGui::PushID(&stackframe);
						if (Selectable(stackframe.label, false, 0, ImVec2(delta, 0), rgbmask)) {
							ShowCall(*trace, stackframe.bestcall);
						}
						ImGui::PopID();

						if ((ImGui::GetCursorPosY() - pos) >= space) {
							ImGui::NextColumn();
						}
					}

					ImGui::Columns(1);
				}
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Worst vs Avg")) {
			if (!s_files.empty()) {
				const auto numblocks = (int)s_files.size();

				int numitems = 0;
				for (auto& trace : s_files) {
					numitems += trace->numstacks;
				}

				const float predictedItemSize = (g.FontSize + style.ItemSpacing.y) * (numblocks + numitems);

				const auto pos = ImGui::GetCursorPosY();
				const auto space = ImGui::GetContentRegionAvail().y / numblocks;

				int numColumns = 1;
				if (predictedItemSize > space) {
					numColumns = std::max((int)(predictedItemSize / space) + 1, 4);
				}

				for (auto& trace : s_files) {
					const double total = (double)(trace->micro_end - trace->micro_start);

					Selectable(trace->path, false, ImGuiSelectableFlags_Disabled, ImVec2(1, 0), ImGui::GetColorU32(ImGuiCol_Header));

					ImGui::Columns(numColumns, NULL, false);

					for (int i = 0; i < trace->numstacks; i++) {

						const auto& stackframe = trace->stackFrames[trace->stacksByWorst[i]];
						const auto rgbmask = (ImU32)(trace->stackFrameIDs[trace->stacksByWorst[i]] | 0xFF000000);
						const auto avg = stackframe.wallTime / (double)stackframe.callCount;
						const auto delta = (float)std::min(stackframe.worstCallTime / avg, 8.0) / 8.f;

						ImGui::PushID(&stackframe);
						if (Selectable(stackframe.label, false, 0, ImVec2(delta, 0), rgbmask)) {
							ShowCall(*trace, stackframe.worstcall);
						}
						ImGui::PopID();

						if ((ImGui::GetCursorPosY() - pos) >= space) {
							ImGui::NextColumn();
						}
					}

					ImGui::Columns(1);
				}
			}
			ImGui::EndTabItem();
		}
		
		ImGui::EndTabBar();
	}
	
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

int main(int, char**) {
	// Setup SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
	{
		printf("Error: %s\n", SDL_GetError());
		return -1;
	}

	// Decide GL+GLSL versions
#ifdef __APPLE__
	// GL 3.2 Core + GLSL 150
	const char* glsl_version = "#version 150";
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
	// GL 3.0 + GLSL 130
	const char* glsl_version = "#version 130";
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

	// Create window with graphics context
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_DisplayMode current;
	SDL_GetCurrentDisplayMode(0, &current);
	s_window = SDL_CreateWindow("PocketTrace Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	SDL_GLContext gl_context = SDL_GL_CreateContext(s_window);
	SDL_GL_SetSwapInterval(1); // Enable vsync

	// Initialize OpenGL loader
	bool err = gl3wInit() != 0;
	if (err) {
		fprintf(stderr, "Failed to initialize OpenGL loader!\n");
		return 1;
	}

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer bindings
	ImGui_ImplSDL2_InitForOpenGL(s_window, gl_context);
	ImGui_ImplOpenGL3_Init(glsl_version);

	static const ImVec4 clear_color = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);

	bool firstFrame = true;

	// Main loop
	bool done = false;
	while (!done)
	{
		// Poll and handle events (inputs, window resize, etc.)
		// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
		// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
		// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
		// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			static bool mmdown = false;
			static int mx;

			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT) {
				done = true;
			} else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(s_window)) {
				done = true;
			} else if (event.type == SDL_DROPFILE) {
				OpenTraceFile(event.drop.file);
				s_setSelectedTab = SELECT_TAB_FLAME_CHART;
			} else if (s_inFlameChart) {
				if ((event.type == SDL_MOUSEBUTTONDOWN) && (event.button.button == SDL_BUTTON_MIDDLE)) {
					mx = event.button.x;
					mmdown = true;
				} else if ((event.type == SDL_MOUSEMOTION) && mmdown) {
					const auto dx = mx - event.button.x;
					const auto maxscroll = s_totalTicks * s_vpInvTimeScale * s_ww;
					auto dt = (dx / s_ww * s_vpTimeScale);

					if (dt >= 1) {
						const auto udt = (uint64_t)dt;
						s_vpTimeBounds[0] += udt;
						s_vpTimeBounds[1] = s_vpTimeBounds[0] + s_vpTimeScale;
						mx = event.button.x;
						s_scrollpos = (float)(s_vpTimeBounds[0] / (double)(s_totalTicks - s_vpTimeScale)) * maxscroll;
					} else if (dt <= -1) {
						const auto udt = (uint64_t)-dt;
						if (s_vpTimeBounds[0] > udt) {
							s_vpTimeBounds[0] -= udt;
						} else {
							s_vpTimeBounds[0] = 0;
						}
						s_vpTimeBounds[1] = s_vpTimeBounds[0] + s_vpTimeScale;
						mx = event.button.x;
						s_scrollpos = (float)(s_vpTimeBounds[0] / (double)(s_totalTicks - s_vpTimeScale)) * maxscroll;
					}
				} else if ((event.type == SDL_MOUSEBUTTONUP) && (event.button.button == SDL_BUTTON_MIDDLE)) {
					mmdown = false;
				} else if (event.type == SDL_MOUSEWHEEL) {
					int x;
					SDL_GetMouseState(&x, nullptr);

					const auto time = s_vpTimeBounds[0] + (uint64_t)(x / s_ww * s_vpTimeScale);
					if (event.wheel.y > 0) {
						TimescaleDown(time + s_minTicks);
					} else if (event.wheel.y < 0) {
						TimescaleUp(time + s_minTicks);
					}
				}
			} else {
				mmdown = false;
			}
		}

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(s_window);
		ImGui::NewFrame();

		if (firstFrame) {
			firstFrame = false;
			s_oldvpTimeBounds = s_vpTimeBounds;
			s_ww = io.DisplaySize.x;
			s_wh = io.DisplaySize.y;
			s_timeScaleIndex = 10;
			SetTimeScale(10, 0);

			//OpenTraceFile("F:\\Projects\\Dracarys5\\Temp\\Traces\\trace_00000.Main.19228.trace");
		}

		// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).

		DrawFrame(io.DisplaySize.x, io.DisplaySize.y);
		//ImGui::ShowDemoWindow(nullptr);

		// Rendering
		ImGui::Render();
		SDL_GL_MakeCurrent(s_window, gl_context);
		glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
		glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(s_window);
	}

	s_files.clear();

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(s_window);
	SDL_Quit();

	return 0;
}
