function(add_cxx_files TARGET)
	if(DEFINED PRODUCT_INCLUDE_FILES)
		set(INCLUDE_FILES ${PRODUCT_INCLUDE_FILES})
	elseif(DEFINED PRODUCT_HEADER_FILES)
		# A standalone product owns one explicit header graph. Do not supplement it
		# with the repository-wide public include tree: merely listing those headers
		# on the target lets unrelated products leak back into IDE/build manifests.
		set(INCLUDE_FILES)
	else()
		file(GLOB_RECURSE INCLUDE_FILES
			LIST_DIRECTORIES false
			CONFIGURE_DEPENDS
			"include/*.h"
			"include/*.hpp"
			"include/*.hxx"
			"include/*.inl"
		)
	endif()

	if(DEFINED PRODUCT_HEADER_FILES)
		set(HEADER_FILES ${PRODUCT_HEADER_FILES})
	else()
		file(GLOB_RECURSE HEADER_FILES
			LIST_DIRECTORIES false
			CONFIGURE_DEPENDS
			"src/*.h"
			"src/*.hpp"
			"src/*.hxx"
			"src/*.inl"
		)
	endif()

	if(INCLUDE_FILES)
		source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include
			PREFIX "Header Files"
			FILES ${INCLUDE_FILES})
		target_sources("${TARGET}" PUBLIC ${INCLUDE_FILES})
	endif()

	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
		PREFIX "Header Files"
		FILES ${HEADER_FILES})

	target_sources("${TARGET}" PRIVATE ${HEADER_FILES})

	if(DEFINED PRODUCT_CPP_SOURCES)
		set(SOURCE_FILES ${PRODUCT_CPP_SOURCES})
	else()
		file(GLOB_RECURSE SOURCE_FILES
			LIST_DIRECTORIES false
			CONFIGURE_DEPENDS
			"src/*.cpp"
			"src/*.cxx"
		)
	endif()

	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src
		PREFIX "Source Files"
		FILES ${SOURCE_FILES})

	target_sources("${TARGET}" PRIVATE ${SOURCE_FILES})

	if(DEFINED PRODUCT_HLSL_FILES)
		set(HLSL_FILES ${PRODUCT_HLSL_FILES})
	else()
		file(GLOB_RECURSE HLSL_FILES
			LIST_DIRECTORIES false
			CONFIGURE_DEPENDS
			"Features/**/*.hlsl"
			"Features/**/*.hlsli"
			"Package/**/*.hlsl"
			"Package/**/*.hlsli"
		)
	endif()

	set(HLSL_FILES ${HLSL_FILES} PARENT_SCOPE)

	list(APPEND CPP_SOURCES ${INCLUDE_FILES})
	list(APPEND CPP_SOURCES ${HEADER_FILES})
	list(APPEND CPP_SOURCES ${SOURCE_FILES})
	set(CPP_SOURCES ${CPP_SOURCES} PARENT_SCOPE)

	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/
		PREFIX "HLSL Files"
		FILES ${HLSL_FILES})

	set_source_files_properties(${HLSL_FILES} PROPERTIES VS_TOOL_OVERRIDE "None")

	target_sources("${TARGET}" PRIVATE ${HLSL_FILES})
endfunction()
