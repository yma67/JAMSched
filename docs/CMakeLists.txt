find_package(Doxygen REQUIRED)

# Find all the public headers
get_target_property(JAMSCRIPT_PUBLIC_HEADER_DIR jamsched INTERFACE_INCLUDE_DIRECTORIES)
file(GLOB_RECURSE JAMSCRIPT_PUBLIC_HEADERS RELATIVE ${JAMSCRIPT_PUBLIC_HEADER_DIR} ${JAMSCRIPT_PUBLIC_HEADER_DIR}/*.h)
set(JAMSCRIPT_PUBLIC_HEADERS_LIST "")
foreach(jamcomponent_source ${JAMSCRIPT_PUBLIC_HEADERS})
    list(APPEND JAMSCRIPT_PUBLIC_HEADERS_LIST "${JAMSCRIPT_PUBLIC_HEADER_DIR}/${jamcomponent_source}")
endforeach(jamcomponent_source ${JAMSCRIPT_PUBLIC_HEADERS})
set(DOXYGEN_INPUT_DIR "\"${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/include\"")
set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/xml/index.xml)
set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
set(DOXYGEN_PROJECT_NAME JAMSched)
set(DOXYGEN_PROJECT_NUMBER "\"ECSE 458G6/027/028/029\"")
set(DOXYGEN_PROJECT_BRIEF "\"JAMScript Scheduling Framework & Runtime\"")
set(DOXYGEN_EXCLUDE_PATTERNS "*/ucontext/*")
set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${PROJECT_SOURCE_DIR}/README.md")

# Replace variables inside @@ with the current values
configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)
# Doxygen won't create this for us
file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR})

# Only regenerate Doxygen when the Doxyfile or public headers change
add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
		            DEPENDS ${JAMSCRIPT_PUBLIC_HEADERS_LIST}
        	        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
		            MAIN_DEPENDENCY ${DOXYFILE_OUT} ${DOXYFILE_IN}
        	        COMMENT "Generating docs"
		            VERBATIM)
# Nice named target so we can run the job easily
add_custom_target(Doxygen ALL DEPENDS ${DOXYGEN_INDEX_FILE})