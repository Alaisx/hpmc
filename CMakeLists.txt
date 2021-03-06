cmake_minimum_required(VERSION 2.4)
if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)
PROJECT( hpmc )

OPTION( DEBUG "Create debug build" OFF )

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
        "${hpmc_SOURCE_DIR}")

INCLUDE_DIRECTORIES(
  "./hpmc/include"
)

FIND_PACKAGE( OpenGL REQUIRED )
FIND_PACKAGE( GLUT REQUIRED )
FIND_PACKAGE( GLEW REQUIRED )

FILE( GLOB HPMC_HDRS "hpmc/include/*.h" "hpmc/include/*.hpp" "hpmc/src/*.hpp" )
SOURCE_GROUP( "HPMC headers" FILES ${HPMC_HDRS} )
SET_SOURCE_FILES_PROPERTIES( ${HPMC_HDRS} PROPERTIES HEADER_FILE_ONLY TRUE )

FILE( GLOB HPMC_SRCS "hpmc/src/*.cpp" )
SOURCE_GROUP( "HPMC sources" FILES ${HPMC_SRCS} )


IF( DEBUG )
    SET( CMAKE_CXX_FLAGS "-DDEBUG ${CMAKE_CXX_FLAGS}" )
ENDIF( DEBUG )

ADD_LIBRARY( hpmc STATIC ${HPMC_HDRS} ${HPMC_SRCS} )
TARGET_LINK_LIBRARIES( hpmc ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES} ${GLUT_LIBRARIES} )

install( TARGETS  
    hpmc
    EXPORT HpmcTargets
    LIBRARY
    DESTINATION lib/ 
    ARCHIVE DESTINATION lib/
    COMPONENT hpmc)

install(FILES ${HPMC_HDRS} DESTINATION "include/" COMPONENT hpmc)

INCLUDE_DIRECTORIES( ${GLUT_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} )

ADD_EXECUTABLE( texture3d "apps/texture3d/texture3d.cpp" )
TARGET_LINK_LIBRARIES( texture3d hpmc ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES} ${GLUT_LIBRARIES} )

ADD_EXECUTABLE( cayley "apps/cayley/cayley.cpp" )
TARGET_LINK_LIBRARIES( cayley hpmc ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES} ${GLUT_LIBRARIES} )

ADD_EXECUTABLE( transform_feedback "apps/transform_feedback/transform_feedback.cpp" )
TARGET_LINK_LIBRARIES( transform_feedback hpmc ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES} ${GLUT_LIBRARIES} )

ADD_EXECUTABLE( metaballs "apps/metaballs/metaballs.cpp" )
TARGET_LINK_LIBRARIES( metaballs hpmc ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES} ${GLUT_LIBRARIES} )

ADD_EXECUTABLE( particles "apps/particles/particles.cpp" )
TARGET_LINK_LIBRARIES( particles hpmc ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES} ${GLUT_LIBRARIES} )

