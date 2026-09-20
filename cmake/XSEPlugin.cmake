add_compile_definitions(FALLOUT4)
set(CommonLibPath "extern/CommonLibF4/CommonLibF4")
set(CommonLibName "CommonLibF4")

# Enable CommonLibF4 options for flat + VR support
set(F4SE_SUPPORT_XBYAK ON CACHE BOOL "Enable xbyak trampoline support" FORCE)
# NOTE: Do NOT use FORCE here — it would override CMake preset values (e.g. VRONLY).
# The defaults below (all ON) match the ALL preset; VRONLY preset sets only VR=ON.
if(NOT DEFINED CACHE{ENABLE_FALLOUT_F4})
    set(ENABLE_FALLOUT_F4 ON CACHE BOOL "Enable Fallout 4 flat support")
endif()
if(NOT DEFINED CACHE{ENABLE_FALLOUT_NG})
    set(ENABLE_FALLOUT_NG ON CACHE BOOL "Enable Fallout 4 Next-Gen support")
endif()
if(NOT DEFINED CACHE{ENABLE_FALLOUT_VR})
    set(ENABLE_FALLOUT_VR ON CACHE BOOL "Enable Fallout 4 VR support")
endif()

add_library("${PROJECT_NAME}" SHARED)

target_compile_features(
	"${PROJECT_NAME}"
	PRIVATE
	cxx_std_23
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(AddCXXFiles)
add_cxx_files("${PROJECT_NAME}")

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h
	@ONLY
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc
	@ONLY
)

target_sources(
	"${PROJECT_NAME}"
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h
	${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc
)

target_precompile_headers(
	"${PROJECT_NAME}"
	PRIVATE
	include/PCH.h
)

# FO4 PORT: CommonLibF4 PCH is very large; limit parallel compilation to avoid
# exhausting the 32-bit cl.exe address space (C1076/C3859 errors).
# /MP4 limits to 4 parallel cl.exe processes instead of unlimited (/MP).
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

set(BUILD_TESTS OFF)

# Define _WINDOWS for all Windows builds (required by FidelityFX API loader)
if(WIN32)
	add_compile_definitions(_WINDOWS)
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio")
	add_compile_definitions(_UNICODE)

	target_compile_definitions(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:DEBUG>")

	set(SC_DEBUG_OPTS "/fp:strict;/ZI;/Od;/Gy")
	set(SC_RELEASE_OPTS "/Zi;/fp:fast;/GL;/Gy;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Ob2;/Oi;/Ot;/Oy;/fp:except-")

	target_compile_options(
		"${PROJECT_NAME}"
		PRIVATE
		/MP4
		/W4
		/WX-
		/permissive-
		/Zm2000
		/Zc:alignedNew
		/Zc:auto
		/Zc:__cplusplus
		/Zc:externC
		/Zc:externConstexpr
		/Zc:forScope
		/Zc:hiddenFriend
		/Zc:implicitNoexcept
		/Zc:lambda
		/Zc:noexceptTypes
		/Zc:preprocessor
		/Zc:referenceBinding
		/Zc:rvalueCast
		/Zc:sizedDealloc
		/Zc:strictStrings
		/Zc:ternary
		/Zc:threadSafeInit
		/Zc:trigraphs
		/Zc:wchar_t
		/wd4200 # nonstandard extension used : zero-sized array in struct/union
	)

	target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:${SC_DEBUG_OPTS}>")
	target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:RELEASE>:${SC_RELEASE_OPTS}>")

	target_link_options(
		${PROJECT_NAME}
		PRIVATE
		/WX:NO
		"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
		"$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif()

# Silence C++23 deprecation warnings from CommonLibF4 (std::aligned_storage_t)
add_compile_definitions(_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING)

add_subdirectory(${CommonLibPath} ${CommonLibName} EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)

target_include_directories(
	${PROJECT_NAME}
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/cmake
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	${PROJECT_NAME}
	PUBLIC
	CommonLibF4::CommonLibF4
)
