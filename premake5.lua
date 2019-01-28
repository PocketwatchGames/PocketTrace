workspace "PocketTrace"
startproject "TraceViewer"
configurations { "Debug", "Release" }
location "Build"	
rtti "off"
exceptionhandling "off"
characterset "unicode"
flags {"FatalWarnings"}
symbols "On"
nativewchar "On"
editandcontinue "On"
language "C++"
cppdialect "C++11"
platforms {"x64"}

debugdir "."
pic "On"
targetdir "Build/Bin/%{cfg.system}/%{cfg.architecture}/%{cfg.buildcfg}"

filter "platforms:x64"
	architecture "x64"
filter {"system:windows"}
	warnings "Extra"
	defines { "WIN32", "_WIN32", "_ENABLE_EXTENDED_ALIGNED_STORAGE", "_CRT_SECURE_NO_WARNINGS", "_WIN32_WINNT=0x0600", "WINVER=0x0600" }
	disablewarnings {"4061"} -- (this is already covered wll by 4062) enumerator 'X' in switch of enum 'Y' is not explicitly handled by a case label
	disablewarnings {"4100"} -- unreferenced formal parameter
	disablewarnings {"4201"} -- anonymous struct/union
	disablewarnings {"4268"} -- 'const' static/global data initialized with compiler generated default constructor fills the object with zeros
	disablewarnings { "4365"} -- 'argument': conversion from 'X' to 'Y', signed/unsigned mismatch
	disablewarnings { "4388"} -- signed/unsigned mismatch
	disablewarnings { "4464"} -- relative include path contains '..'
	disablewarnings { "4505"} -- unreferenced local function has been removed
	disablewarnings { "4514"} -- unreferenced inline function has been removed
	disablewarnings { "4577"} -- 'noexcept' used with no exception handling mode specified; termination on exception is not guaranteed. Specify /EHsc
	disablewarnings { "4623"} -- default constructor was implicitly defined as deleted
	disablewarnings { "4625"} -- copy constructor was implicitly defined as deleted
	disablewarnings { "4626"} -- assignment operator was implicitly defined as deleted
	disablewarnings { "4647"} -- behavior change: __is_pod(type) has different value in previous versions
	disablewarnings { "4710"} -- function not inlined
	disablewarnings { "4711"} -- function 'X' selected for automatic inline expansion
	disablewarnings { "4774"} -- format string expected in argument 2 is not a string literal
	disablewarnings { "4820"} -- padding added after data member
	disablewarnings { "5026"} -- move constructor was implicitly defined as deleted
	disablewarnings { "5027"} -- move assignment operator was implicitly defined as deleted
	disablewarnings { "5038"} -- data member 'X' will be initialized after data member 'Y'
	disablewarnings { "5045"} -- Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
	linkoptions { "-IGNORE:4221" } -- This object file does not define any previously undefined public symbols, so it will not be used by any link operation that consumes this library
	buildoptions {"/Zp8"}
filter {"system:windows", "architecture:x64" }
	defines { "WIN64", "_WIN64" }
filter {"configurations:Debug"}
	defines {"_DEBUG"}
filter "configurations:Release"
	defines {"NDEBUG"}
	runtime "Release"
	editandcontinue "Off"
	optimize "Speed"
	flags {"LinkTimeOptimization", "NoIncrementalLink", "NoMinimalRebuild"}
filter {"toolset:gcc or clang"}
	removeflags {"FatalWarnings"}
	buildoptions {"-fpack-struct=8", "-fms-extensions"}
filter "system:macosx"
	systemversion("10.9")
filter "system:linux"
	toolset "clang"
	buildoptions {"-stdlib=libstdc++", "-fvisibility=default"}
filter "files:*.lua or *.md or *.txt"
	flags {"ExcludeFromBuild"}
filter {}

function link_opengl()
	filter {"kind:SharedLib or WindowedApp or ConsoleApp", "system:windows"}
		links {"opengl32", "glu32"}
	filter {"kind:SharedLib or WindowedApp or ConsoleApp", "system:macosx"}
		links {"OpenGL.framework"}
	filter{}
end

function link_sdl()
	filter {"kind:SharedLib or WindowedApp or ConsoleApp", "system:windows", "architecture:x64"}
		libdirs { "SDL2/win32/x64" }
		links {"SDL2.lib", "SDL2main.lib"}
		postbuildcommands { 'xcopy /D "$(SolutionDir)\\..\\SDL2\\win32\\x64\\SDL2.dll" "Bin\\%{cfg.system}\\%{cfg.architecture}\\%{cfg.buildcfg}\\"'}
	filter {"kind:SharedLib or WindowedApp or ConsoleApp", "system:windows", "architecture:x86"}
		libdirs { "SDL2/win32/x32" }
		links {"SDL2.lib", "SDL2main.lib"}
		postbuildcommands { 'xcopy /D "$(SolutionDir)\\..\\SDL2\\win32\\x32\\SDL2.dll" "Bin\\%{cfg.system}\\%{cfg.architecture}\\%{cfg.buildcfg}\\"'}
	filter {"kind:SharedLib or WindowedApp or ConsoleApp", "system:macosx"}
		frameworkdirs { "SDL2/macOS" }
		links {"SDL2.framework"}
	filter {"system:linux"}
		links {"SDL2"}
	filter{}
end

function mac_copy_dylib(name_list) 
	for i,v in ipairs(name_list) do
		postbuildcommands {'cp "${BUILT_PRODUCTS_DIR}/'..v..'" "${BUILT_PRODUCTS_DIR}/%{cfg.buildtarget.bundlename}/Contents/MacOS"'}
	end
end

function mac_copy_framework(name_list) 
	for i,v in ipairs(name_list) do
		postbuildcommands {'cp -Rf "${BUILT_PRODUCTS_DIR}/../../../../../ThirdParty/'..v..'" "${BUILT_PRODUCTS_DIR}/%{cfg.buildtarget.bundlename}/Contents/MacOS"'}
	end
end

project "TraceViewer"
-- NOTE: the library link order is sensitive because of linux linker fuckery
	kind "WindowedApp"
	files { "TraceViewer.cpp" }
	files {"premake5.lua", "README.md", "LICENSE.txt"}
	includedirs {
		"imgui/examples/libs/gl3w",
		"mio/include"
	}
	filter {"system:windows"}
		files { "*.rc" }
		entrypoint "WinMainCRTStartup"
	filter "system:macosx"
		files {"macOS_Info.plist"}
		links {"ApplicationServices.framework", "AppKit.framework"}
		linkoptions {"-rpath @executable_path/."}
	filter {"system:macosx", "configurations:*"} -- NOTE: configurations:* avoids a nil reference on macOS in premake5
		mac_copy_framework {"SDL2/macOS/SDL2.framework"}
	filter {}
		link_opengl()
		link_sdl()
		links {"imgui"}
	filter {"system:linux"}
		links {"pthread"}

project "imgui"
-- NOTE: the library link order is sensitive because of linux linker fuckery
	kind "StaticLib"
	warnings "High"
	includedirs {"imgui", "imgui/examples/libs/gl3w", "SDL2/include"}
	files { 
		"imgui/*.h",
		"imgui/imgui.cpp", 
		"imgui/imgui_demo.cpp", 
		"imgui/imgui_draw.cpp", 
		"imgui/imgui_widgets.cpp",
		"imgui/examples/imgui_impl_sdl.cpp",
		"imgui/examples/imgui_impl_opengl3.cpp",
		"imgui/examples/libs/gl3w/GL/gl3w.c"
	}
