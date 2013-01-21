/* Copyright STIFTELSEN SINTEF 2012
 *
 * This file is part of the HPMC Library.
 *
 * Author(s): Christopher Dyken, <christopher.dyken@sintef.no>
 *
 * HPMC is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * HPMC is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * HPMC.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <string>
#include <hpmc.h>
#include <hpmc_internal.h>
#include "Constants.hpp"
#include "IsoSurface.hpp"
#include "HistoPyramid.hpp"

using std::endl;
using std::max;
using std::string;
using std::stringstream;
using std::cerr;

namespace hpmc {
    namespace resources {
        extern std::string downtraversal_110;
        extern std::string downtraversal_130;
        extern std::string baselevel_traversal_110;
        extern std::string baselevel_traversal_130;
    } // of namespace resources
} // of namespace hpmc


// -----------------------------------------------------------------------------
std::string
HPMCgenerateDefines( const struct HPMCIsoSurface* h )
{
    std::stringstream src;

    src << "// generated by HPMCgenerateDefines" << endl;
    //      voxel sizes of scalar function
    src << "#define HPMC_FUNC_X        " << h->field().sizeX() << endl;
    src << "#define HPMC_FUNC_X_F      float(HPMC_FUNC_X)" << endl;
    src << "#define HPMC_FUNC_Y        " << h->field().sizeY() << endl;
    src << "#define HPMC_FUNC_Y_F      float(HPMC_FUNC_Y)" << endl;
    src << "#define HPMC_FUNC_Z        " << h->field().sizeZ() << endl;
    src << "#define HPMC_FUNC_Z_F      float(HPMC_FUNC_Z)" << endl;
    //      cell grid dimension
    src << "#define HPMC_CELLS_X       " << h->field().cellsX() << endl;
    src << "#define HPMC_CELLS_X_F     float(HPMC_CELLS_X)" << endl;
    src << "#define HPMC_CELLS_Y       " << h->field().cellsY() << endl;
    src << "#define HPMC_CELLS_Y_F     float(HPMC_CELLS_Y)" << endl;
    src << "#define HPMC_CELLS_Z       " << h->field().cellsZ() << endl;
    src << "#define HPMC_CELLS_Z_F     float(HPMC_CELLS_Z)" << endl;
    //      cell grid extent
    src << "#define HPMC_GRID_EXT_X_F  float("<<h->field().extentX()<<")"<<endl;
    src << "#define HPMC_GRID_EXT_Y_F  float("<<h->field().extentY()<<")"<<endl;
    src << "#define HPMC_GRID_EXT_Z_F  float("<<h->field().extentZ()<<")"<<endl;
    //      tiling in base layer
    src << "#define HPMC_TILES_X       " << h->baseLevelBuilder().layoutX() << endl;
    src << "#define HPMC_TILES_X_F     float(HPMC_TILES_X)" << endl;
    src << "#define HPMC_TILES_Y       " << h->baseLevelBuilder().layoutY() << endl;
    src << "#define HPMC_TILES_Y_F     float(HPMC_TILES_Y)" << endl;
    //      tile size in base layer
    src << "#define HPMC_TILE_SIZE_X   " << h->baseLevelBuilder().tileSizeX() << endl;
    src << "#define HPMC_TILE_SIZE_X_F float(HPMC_TILE_SIZE_X)" << endl;
    src << "#define HPMC_TILE_SIZE_Y   " << h->baseLevelBuilder().tileSizeY() << endl;
    src << "#define HPMC_TILE_SIZE_Y_F float(HPMC_TILE_SIZE_Y)" << endl;
    //      histopyramid size
    src << "#define HPMC_HP_SIZE_L2  " << h->baseLevelBuilder().log2Size() << endl;
    src << "#define HPMC_HP_SIZE     " << (1<<h->baseLevelBuilder().log2Size()) << endl;
    if( h->field().isBinary() ) {
        src << "#define FIELD_BINARY 1" << endl;
    }
    if( h->field().hasGradient() ) {
        src << "#define FIELD_HAS_GRADIENT 1" << endl;
    }

    //std::cerr << src.str() << "\n";

    return src.str();
}




// -----------------------------------------------------------------------------
std::string
HPMCgenerateExtractVertexFunction( struct HPMCIsoSurface* h )
{
    HPMCTarget target = h->constants()->target();
    stringstream src;

    if( target < HPMC_TARGET_GL30_GLSL130 ) {
        src << "uniform sampler2D  HPMC_histopyramid;" << endl;
        src << "uniform float      HPMC_threshold;" << endl;
        src << "uniform sampler2D  HPMC_edge_table;" << endl;
        src << hpmc::resources::downtraversal_110;
        src << hpmc::resources::baselevel_traversal_110;
//        src << h->baseLevelBuilder().extractSource();
        src << "// generated by HPMCgenerateExtractShaderFunctions" << endl;
        src << "uniform float      HPMC_key_offset;" << endl;
        src << "void" << endl;
        src << "extractVertex( out vec3 a, out vec3 b, out vec3 p, out vec3 n )" << endl;
        src << "{" << endl;
        src << "    float key = gl_Vertex.x + HPMC_key_offset;" << endl;
        src << "    HPMC_baseLevelExtract( a, b, p, n, key );" << endl;
        src << "}" << endl;
        src << "void" << endl;
        src << "extractVertex( out vec3 p, out vec3 n )" << endl;
        src << "{" << endl;
        src << "    vec3 a, b;" << endl;
        src << "    extractVertex( a, b, p, n );" << endl;
        src << "}" << endl;
    }
    else {
        src << "uniform sampler2D  HPMC_histopyramid;" << endl;
        src << "uniform float      HPMC_threshold;" << endl;
        src << "uniform sampler2D  HPMC_edge_table;" << endl;
        src << hpmc::resources::downtraversal_130;
        src << hpmc::resources::baselevel_traversal_110;
//        src << h->baseLevelBuilder().extractSource();
        src << "// generated by HPMCgenerateExtractShaderFunctions" << endl;
        src << "void" << endl;
        src << "extractVertex( out vec3 a, out vec3 b, out vec3 p, out vec3 n )" << endl;
        src << "{" << endl;
        src << "    float key = gl_VertexID;" << endl;
        src << "    HPMC_baseLevelExtract( a, b, p, n, key );" << endl;
        src << "}" << endl;
        src << "void" << endl;
        src << "extractVertex( out vec3 p, out vec3 n )" << endl;
        src << "{" << endl;
        src << "    vec3 a, b;" << endl;
        src << "    extractVertex( a, b, p, n );" << endl;
        src << "}" << endl;
    }

    return src.str();
}
