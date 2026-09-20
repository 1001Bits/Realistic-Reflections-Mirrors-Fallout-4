cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0116 NEW)

project(
    RealisticReflectionsMirrors
    VERSION 1.0.0
    LANGUAGES CXX
)

set(PLUGIN_BINARY_NAME "RealisticReflectionsMirrors")
set(PLUGIN_DISPLAY_NAME "Realistic Reflections - Mirrors")

set(MIRRORS_FEATURE_ROOT "${CMAKE_SOURCE_DIR}/features/Mirrors of Fallout")
set(PRODUCT_PACKAGE_ROOT "${CMAKE_SOURCE_DIR}/package-mirrors")
set(PRODUCT_VR_PACKAGE_ROOT "${CMAKE_SOURCE_DIR}/package-mirrors-vr")
set(PRODUCT_HLSL_FILES
    "${MIRRORS_FEATURE_ROOT}/Shaders/MirrorsOfFallout/PlanarCandidateProofCS.hlsl"
    "${MIRRORS_FEATURE_ROOT}/Shaders/MirrorsOfFallout/PlanarMaterialResolveCS.hlsl"
    "${MIRRORS_FEATURE_ROOT}/Shaders/MirrorsOfFallout/PlanarMirrorDepthDownsampleCS.hlsl"
    "${MIRRORS_FEATURE_ROOT}/Shaders/MirrorsOfFallout/PlanarMirrorFrameDeltaCS.hlsl"
    "${MIRRORS_FEATURE_ROOT}/Shaders/MirrorsOfFallout/PlanarMirrorOverlayCS.hlsl"
    "${MIRRORS_FEATURE_ROOT}/Shaders/MirrorsOfFallout/PlanarMirrorOverlayVR.hlsl"
    "${MIRRORS_FEATURE_ROOT}/Shaders/MirrorsOfFallout/PlanarMirrorResolveVR.hlsl"
    "${MIRRORS_FEATURE_ROOT}/Shaders/MirrorsOfFallout/PlanarMirrorPlayerOverlayResolveCS.hlsl"
    "${MIRRORS_FEATURE_ROOT}/Shaders/MirrorsOfFallout/PlanarNativeLitPlayerOverlayCS.hlsl"
)
include("${CMAKE_SOURCE_DIR}/cmake/MirrorsOfFalloutSources.cmake")

# Compiles the shipped flat-mirror shader bytecode during packaging.
add_executable(MirrorShaderPrecompile tools/MirrorShaderPrecompile.cpp)
target_compile_features(MirrorShaderPrecompile PRIVATE cxx_std_20)
target_compile_definitions(MirrorShaderPrecompile PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(MirrorShaderPrecompile PRIVATE "${CMAKE_SOURCE_DIR}/src/Utils")
target_link_libraries(MirrorShaderPrecompile PRIVATE d3dcompiler)
set_target_properties(MirrorShaderPrecompile PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)

option(BUILD_MIRROR_CK_HELPER "Build the Creation Kit authoring helper" ON)
if(BUILD_MIRROR_CK_HELPER)
    add_subdirectory(tools/ck)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set_property(CACHE CMAKE_INSTALL_PREFIX PROPERTY VALUE "${CMAKE_CURRENT_BINARY_DIR}/aio")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(XSEPlugin)
set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES
        OUTPUT_NAME "${PLUGIN_BINARY_NAME}"
        PDB_NAME "${PLUGIN_BINARY_NAME}"
)

find_package(magic_enum CONFIG REQUIRED)
find_package(xbyak CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(EASTL CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)
find_package(unordered_dense CONFIG REQUIRED)
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)

target_compile_definitions(${PROJECT_NAME} PRIVATE MIRRORS_OF_FALLOUT_BUILD=1)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        "${MIRRORS_FEATURE_ROOT}/Shaders"
        ${DETOURS_INCLUDE_DIRS}
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        Microsoft::CppWinRT
        magic_enum::magic_enum
        xbyak::xbyak
        nlohmann_json::nlohmann_json
        imgui::imgui
        EASTL
        Microsoft::DirectXTex
        Microsoft::DirectXTK
        unordered_dense::unordered_dense
        ${DETOURS_LIBRARY}
        dxguid
)

if(MSVC_VERSION GREATER_EQUAL 1936 AND MSVC_IDE)
    file(
        CONFIGURE
        OUTPUT "${CMAKE_BINARY_DIR}/Directory.Build.props"
        CONTENT
            [==[
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <BuildStlModules>false</BuildStlModules>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
]==]
        @ONLY
    )
endif()

set(FEATURE_CONFIG_FILES "${MIRRORS_FEATURE_ROOT}/Shaders/Features/MirrorsOfFallout.ini")
foreach(FEATURE_PATH ${FEATURE_CONFIG_FILES})
    get_filename_component(FEATURE ${FEATURE_PATH} NAME_WE)
    file(READ "${FEATURE_PATH}" CONFIG_VALUE)
    string(STRIP "${CONFIG_VALUE}" CONFIG_VALUE)
    string(REGEX MATCH "Version = ([0-9]+)-([0-9]+)-([0-9]+)" _ "${CONFIG_VALUE}")
    if(DEFINED CMAKE_MATCH_1 AND DEFINED CMAKE_MATCH_2 AND DEFINED CMAKE_MATCH_3)
        list(APPEND FEATURE_VERSIONS
            "\t\t{\"${FEATURE}\"sv, {${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3}}}")
    else()
        message(FATAL_ERROR "Feature config file '${FEATURE_PATH}' has no valid version string.")
    endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FEATURE_CONFIG_FILES}")
string(REPLACE ";" ",\n" FEATURE_VERSIONS "${FEATURE_VERSIONS}")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FeatureVersions.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/FeatureVersions.h
    @ONLY
)
target_sources("${PROJECT_NAME}" PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/cmake/FeatureVersions.h)

# `cmake --install <build> --prefix <Data directory>` stages a deployable flat payload:
# the plugin, its content master, meshes, materials, MCM configuration, shaders and
# the precompiled shader bytecode. VR material overrides install separately through
# the VRAssets component.
install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/MirrorInstallPreflight.cmake" COMPONENT F4SE)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION F4SE/Plugins COMPONENT F4SE)
install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION F4SE/Plugins COMPONENT F4SE)
install(
    DIRECTORY "${PRODUCT_PACKAGE_ROOT}/" "${MIRRORS_FEATURE_ROOT}/"
    DESTINATION .
    COMPONENT Shaders
    PATTERN "*.[mM][dD]" EXCLUDE
    PATTERN "*.[tT][xX][tT]" EXCLUDE
    PATTERN "CORE" EXCLUDE
)
install(CODE "execute_process(COMMAND \"$<TARGET_FILE:MirrorShaderPrecompile>\" \"\${CMAKE_INSTALL_PREFIX}/Shaders/MirrorsOfFallout\" COMMAND_ERROR_IS_FATAL ANY)" COMPONENT Shaders)
install(DIRECTORY "${PRODUCT_VR_PACKAGE_ROOT}/" DESTINATION . COMPONENT VRAssets EXCLUDE_FROM_ALL
    PATTERN "*.[mM][dD]" EXCLUDE
    PATTERN "*.[tT][xX][tT]" EXCLUDE)
