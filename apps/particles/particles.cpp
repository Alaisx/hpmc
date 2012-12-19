/* -*- mode: C++; tab-width:4; c-basic-offset: 4; indent-tabs-mode:nil -*-
 ***********************************************************************
 *
 *  File: particles.cpp
 *
 *  Created: 24. June 2009
 *
 *  Version: $Id: $
 *
 *  Authors: Christopher Dyken <christopher.dyken@sintef.no>
 *
 *  This file is part of the HPMC library.
 *  Copyright (C) 2009 by SINTEF.  All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  ("GPL") version 2 as published by the Free Software Foundation.
 *  See the file LICENSE.GPL at the root directory of this source
 *  distribution for additional information about the GNU GPL.
 *
 *  For using HPMC with software that can not be combined with the
 *  GNU GPL, please contact SINTEF for aquiring a commercial license
 *  and support.
 *
 *  SINTEF, Pb 124 Blindern, N-0314 Oslo, Norway
 *  http://www.sintef.no
 *********************************************************************/

// Morphing algebraic shapes that emits particles.
//
// This example demonstrates using the surface generated by HPMC as input to a
// geometry shader that emits particles randomly over the surface. The particles
// are pulled by gravity, and uses the scalar field passed to HPMC to determine
// when particles hit the surface, and in this case, they bounce. To test if a
// particle hits the surface is done by evaluating the sign of the scalar field
// at the position of the particle at the beginning of the timestep and at the
// end. This approach is a bit too simple for these shapes, as they usually have
// a great deal of regions with multiple zeros, and this leads to the artefact
// of particles falling through the surface at some places.
//
// The following render loop is used:
// - Use HPMC to determine the iso-surface of the current scalar field
// - Render the iso surface, but tap vertex position and normals into a
//   transform feedback buffer.
// - Pass this buffer into a geometry shader that emits particles (points) at
//   some of the triangles, output stored in another transform feedback buffer.
// - Pass the particles from the previous frame into a geometry shader that
//   does a series of Euler-steps to integrate velocity and position, checking
//   for collisions in-between. The output of this pass is concatenated at the
//   end of the newly created particles using transform feedback.
// - Render the particles using a geometry shader that expands the point
//   positions into quadrilateral screen-aligned billboards.

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <GL/glew.h>
#ifdef __APPLE__
#include <glut.h>
#else
#include <GL/glut.h>
#endif
#include "hpmc.h"
#include "../common/common.cpp"

using std::min;
using std::max;
using std::cerr;
using std::endl;
using std::vector;
using std::string;
using std::ofstream;

// adjust this variable to change the amount of particles
static int particle_flow = 4000;


int volume_size_x;
int volume_size_y;
int volume_size_z;

GLuint mc_tri_vbo;
GLsizei mc_tri_vbo_N;

GLuint particles_vbo[2]; // two buffers which are used round-robin
GLuint particles_vbo_p;  // tells which of the two buffers that are current
GLuint particles_vbo_n;  // number of particles in buffer
GLuint particles_vbo_N;  // size of particle buffer

struct HPMCConstants* hpmc_c;
struct HPMCIsoSurface* hpmc_h;
struct HPMCIsoSurfaceRenderer* hpmc_th;

// -----------------------------------------------------------------------------
std::string fetch_code =
        // evaluates the scalar field
        "uniform float shape[12];\n"
        "float\n"
        "HPMC_fetch( vec3 p )\n"
        "{\n"
        "    p -= 0.5;\n"
        "    p *= 2.2;\n"
        "    return -( shape[0]*p.x*p.x*p.x*p.x*p.x +\n"
        "              shape[1]*p.x*p.x*p.x*p.x +\n"
        "              shape[2]*p.y*p.y*p.y*p.y +\n"
        "              shape[3]*p.z*p.z*p.z*p.z +\n"
        "              shape[4]*p.x*p.x*p.y*p.y +\n"
        "              shape[5]*p.x*p.x*p.z*p.z +\n"
        "              shape[6]*p.y*p.y*p.z*p.z +\n"
        "              shape[7]*p.x*p.y*p.z +\n"
        "              shape[8]*p.x*p.x +\n"
        "              shape[9]*p.y*p.y +\n"
        "              shape[10]*p.z*p.z +\n"
        "              shape[11] );\n"
        "}\n"
       "vec4\n"
        // evaluates the gradient as well as the scalar field
        "HPMC_fetchGrad( vec3 p )\n"
        "{\n"
        "    p -= 0.5;\n"
        "    p *= 2.2;\n"
        "    return -vec4( 5.0*shape[0]*p.x*p.x*p.x*p.x +\n"
        "                  4.0*shape[1]*p.x*p.x*p.x +\n"
        "                  2.0*shape[4]*p.x*p.y*p.y +\n"
        "                  2.0*shape[5]*p.x*p.z*p.z +\n"
        "                      shape[7]*p.y*p.z +\n"
        "                  2.0*shape[8]*p.x,\n"
        "\n"
        "                  4.0*shape[2]*p.y*p.y*p.y +\n"
        "                  2.0*shape[4]*p.x*p.x*p.y +\n"
        "                  2.0*shape[6]*p.y*p.z*p.z +\n"
        "                      shape[7]*p.x*p.z +\n"
        "                  2.0*shape[9]*p.y,\n"
        "\n"
        "                  4.0*shape[3]*p.z*p.z*p.z +\n"
        "                  2.0*shape[5]*p.x*p.x*p.z +\n"
        "                  2.0*shape[6]*p.y*p.z*p.z +\n"
        "                      shape[7]*p.x*p.y +\n"
        "                  2.0*shape[10]*p.z,\n"
        "\n"
        "                  shape[0]*p.x*p.x*p.x*p.x*p.x +\n"
        "                  shape[1]*p.x*p.x*p.x*p.x +\n"
        "                  shape[2]*p.y*p.y*p.y*p.y +\n"
        "                  shape[3]*p.z*p.z*p.z*p.z +\n"
        "                  shape[4]*p.x*p.x*p.y*p.y +\n"
        "                  shape[5]*p.x*p.x*p.z*p.z +\n"
        "                  shape[6]*p.y*p.y*p.z*p.z +\n"
        "                  shape[7]*p.x*p.y*p.z +\n"
        "                  shape[8]*p.x*p.x +\n"
        "                  shape[9]*p.y*p.y +\n"
        "                  shape[10]*p.z*p.z +\n"
        "                  shape[11] );\n"
        "}\n";

// -----------------------------------------------------------------------------
// a small vertex shader that calls the provided extraction function
GLuint onscreen_v;
GLuint onscreen_f;
GLuint onscreen_p;
std::string custom_vertex_shader =
        "varying out vec3 normal_cs;\n"
        "varying out vec3 position_cs;\n"
        "void\n"
        "main()\n"
        "{\n"
        "    vec3 p, n;\n"
        "    extractVertex( p, n );\n"
        "    vec4 pp = gl_ModelViewMatrix * vec4( p, 1.0 );\n"
        "    vec3 cn = normalize( gl_NormalMatrix * n );\n"
        "    normal_cs = cn;\n"
        "    position_cs = (1.0/pp.w)*pp.xyz;\n"
        "    gl_Position = gl_ProjectionMatrix * pp;\n"
        "}\n";
std::string custom_fragment_shader =
        "varying in vec3 normal_cs;\n"
        "varying in float grad_length;\n"
        "void\n"
        "main()\n"
        "{\n"
        "    const vec3 v = vec3(0.0, 0.0, 1.0 );\n"
        "    vec3 l = normalize(vec3(1.0, 1.0, 1.0));\n"
        "    vec3 h = normalize( v + l );\n"
        "    vec3 cn = normalize( normal_cs );\n"
        "    float diff = max(0.0,dot( cn, l ) )\n"
        "               + max(0.0,dot(-cn, l ) );\n"
        "    float spec = pow( max( 0.0, dot( cn, h) ), 30.0 )\n"
        "               + pow( max( 0.0, dot(-cn, h) ), 30.0 );\n"
        "    gl_FragColor = vec4( 0.1, 0.2, 0.7, 0.0) * diff\n"
        "                 + vec4( 1.0, 1.0, 1.0, 0.0) * spec;\n"
        "}\n";

// -----------------------------------------------------------------------------
GLuint emitter_v;
GLuint emitter_g;
GLuint emitter_p;
GLuint emitter_query;
string emitter_vertex_shader =
        // interleaved arrays with GL_N3F_V3F is assumed
        "varying out vec3 normal;\n"
        "void\n"
        "main()\n"
        "{\n"
        "    normal = gl_Normal;\n"
        "    gl_Position = vec4( gl_Vertex.xyz, 1.0 );\n"
        "}\n";
// geometry shader is run once per triangle and emits one or nil points
string emitter_geometry_shader =
        // an offset we use to randomize which primitive that generates points
        "uniform int off;\n"
        // governs how likely it is that a triangle will produce a point
        "uniform int threshold;\n"
        "varying in  vec3 normal[3];\n"
        // varyings that will be recorded
        "varying out vec2 info;\n"
        "varying out vec3 vel;\n"
        "varying out vec3 pos;\n"
        "void\n"
        "main()\n"
        "{\n"
        "    if( int(off + gl_PrimitiveIDIn) % threshold == 0 ) {\n"
        "        int side = (gl_PrimitiveIDIn / threshold) %2;\n"
        "        info = vec2( 1.0, 1.0 );\n"
        //       position new particle on center of triangle
        "        pos = (1.0/3.0)*( gl_PositionIn[0].xyz +\n"
        "                          gl_PositionIn[1].xyz +\n"
        "                          gl_PositionIn[2].xyz )\n"
        //       and push it slightly off the surface along the normal direction
        "            + (side?0.02:-0.02)*normalize( normal[0] +\n"
        "                                           normal[1] +\n"
        "                                           normal[2] );\n"
        //       initial velocity is zero
        "        vel = vec3(0.0);\n"
        "        gl_Position = gl_ProjectionMatrix * vec4(pos, 1.0);\n"
        "        EmitVertex();\n"
        "    }\n"
        "}\n";

// --- particle animation shader program ---------------------------------------
GLuint anim_v;
GLuint anim_g;
GLuint anim_p;
GLuint anim_query;

string anim_vertex_shader =
        // input from interleaved GL_T2F_N3F_V3F buffer
        // pass output to GS, position in gl_Position
        "varying out vec3 invel;\n"
        "varying out vec2 ininfo;\n"
        "void\n"
        "main()\n"
        "{\n"
        "    invel = gl_Normal;\n"
        "    ininfo = gl_MultiTexCoord0.xy;\n"
        "    gl_Position = gl_Vertex;\n"
        "}\n";

string anim_geometry_shader =
        // input passed from VS
        "varying in vec3 invel[1];\n"                                          \
        "varying in vec2 ininfo[1];\n"                                         \
        // output varyings that get fed back
        "varying out vec3 pos;\n"                                              \
        "varying out vec3 vel;\n"                                              \
        "varying out vec2 info;\n"                                             \
        // timestep
        "uniform float dt;\n"                                                  \
        "uniform float iso;\n"\
        "void\n" \
        "main()\n" \
        "{\n" \
        "    info = ininfo[0] - vec2( 0.1*dt, dt );\n"\
        "    vec3 vel_a_c = invel[0];\n"
        "    vec3 pos_a_c = gl_PositionIn[0].xyz;\n"
        "    vec3 acc_b_c = vec3( 0.0, -0.6, 0.0 );\n"
        "    vec3 vel_b_c;\n"
        "    vec3 pos_b_c;\n"
        "    const int steps = 32;\n"
        "    float sdt = (1.0/float(steps))*dt;\n"
        //   object space pos of a
        "    vec4 pos_a_ho = gl_ModelViewMatrixInverse * vec4( pos_a_c, 1.0 );\n"
        "    vec3 pos_a_o = (1.0/pos_a_ho.w)*pos_a_ho.xyz;\n"
        "    for( int s=0; s<steps; s++ ) {\n"
        //       integrate
        "        vel_b_c = vel_a_c + sdt * acc_b_c;\n"
        "        pos_b_c = pos_a_c + sdt * vel_b_c;\n"
        //       calc object space pos of b
        "        vec4 pos_b_ho = gl_ModelViewMatrixInverse * vec4( pos_b_c, 1.0 );\n"
        "        vec3 pos_b_o = (1.0/pos_b_ho.w)*pos_b_ho.xyz;\n"
        //       surface interaction only happen inside object space unit cube
        "        if( all( lessThan( abs(pos_b_o-vec3(0.5)), vec3(0.5) ) ) ) {\n"
        //           First, find the direction towards the surface
        "            vec4 gradsample_a = HPMC_fetchGrad( pos_a_o )-vec4(0.0,0.0,0.0,iso);\n"
        "            vec3 to_surf_o = -0.01*sign(gradsample_a.w)*normalize(gradsample_a.xyz);\n"
        "            vec3 to_surf_c = gl_NormalMatrix * to_surf_o;\n"
        //           Check if particle is moving towards the surface
        "            if( dot(vel_b_c, to_surf_c) > 0.0 ) {\n"
        //              Then, check the scalar feld a small step towards the surface
        "                vec3 to_surf_pos = pos_a_o + to_surf_o;\n"
        "                float to_surf_field = HPMC_fetch( to_surf_pos )-iso;\n"
        //               If field changes sign, move particle slighly backwards
        //               to minimize the risk of the particle falling through
        //               the surface. Note that just checking signs might have
        //               problems at multiple zeros, which happen with these
        //               algebraic surfaces.
        "                if( (to_surf_field)*(gradsample_a.w) <= 0.0 ) {\n"
        //                   Determine closest point on surface
        "                    float t = -gradsample_a.w/(to_surf_field-gradsample_a.w);\n"
        //                   And move the particle a small step backwards
        "                    pos_a_o = mix( pos_a_o, to_surf_pos, t ) - to_surf_o;\n"
        //                   Update camera-space position,
        "                    vec4 pos_a_hc = gl_ModelViewMatrix * vec4( pos_a_o, 1.0 );\n"
        "                    vec3 new_pos_a_c = (1.0/pos_a_hc.w)*pos_a_hc.xyz;\n"
        //                   Find direction we pushed in camera-space
        "                    vec3 to_surf_n_c = normalize( to_surf_c );\n"
        //                   Determine amount of movement towards the surface
        "                    float to_surf_vel = dot( vel_b_c, to_surf_n_c );\n"
        //                   Kill velocity into the surface
        "                    vel_b_c -= to_surf_vel*to_surf_n_c;\n"
        //                   update position of a
        "                    pos_a_c = new_pos_a_c;\n"
        "                    pos_b_c = pos_a_c + sdt * vel_b_c;\n"
        "                    vec4 pos_b_ho = gl_ModelViewMatrixInverse * vec4( pos_b_c, 1.0 );\n"
        "                    pos_b_o = (1.0/pos_b_ho.w)*pos_b_ho.xyz;\n"
        "                    info.y = 1.0;\n"
        "                }\n"
        "            }\n"
        "            float field_a = HPMC_fetch( pos_a_o ) - iso;\n"
        "            float field_b = HPMC_fetch( pos_b_o ) - iso;\n"
        //           check for sign change
        "            if( field_a*field_b <= 0.0 ) {\n"
        //               determine zero-crossing
        "                float t = -field_a/(field_b-field_a);\n"
        //               step back to intersection
        "                pos_b_c -= (1.0-t)*dt*vel_b_c;\n"
        //               point of intersection in object space
        "                vec3 pos_i_o = mix( pos_a_o, pos_b_o, t );\n"
        //               gradient at intersection used to get surface normal
        "                vec3 nrm_i_c = normalize( gl_NormalMatrix * HPMC_fetchGrad( pos_i_o ).xyz );\n"
        //               reflect velocity
        "                vel_b_c = reflect( vel_b_c, nrm_i_c );\n"
        //               step rest of timestep in reflected direction
        "                pos_b_c += (1.0-t)*dt*vel_b_c;\n"
        "                vel_b_c *= 0.98;\n"
        "                info.y = 1.0;\n"
        "            }\n"
        "        }\n"
        "        vel_a_c = vel_b_c;\n"
        "        pos_a_c = pos_b_c;\n"
        "        pos_a_o = pos_b_o;\n"
        "    }\n"
        "    vel = vel_b_c;\n"
        "    pos = pos_b_c;\n"
        "    gl_Position = gl_ProjectionMatrix * vec4(pos, 1.0);\n"
        "    vec3 norm = (1.0/gl_Position.w)*gl_Position.xyz;\n"
        //   only emit particles inside the frustum and that are not too old
        "    if( (info.x > 0.0) && \n" \
        "        all( lessThan( abs(norm), vec3(1.0) ) ) )\n"
        "    {\n"\
        "        EmitVertex();\n"                                              \
        "    }\n"\
        "}\n";

// --- particle billboard render shader program --------------------------------
GLuint billboard_v;
GLuint billboard_g;
GLuint billboard_f;
GLuint billboard_p;
string billboard_vertex_shader =
        // input from interleaved GL_T2F_N3F_V3F buffer
        // pass output to GS, position in gl_Position
        "varying out vec3 invel;\n"
        "varying out vec2 ininfo;\n"
        "void\n"
        "main()\n"
        "{\n"
        "    invel = gl_Normal;\n"
        "    ininfo = gl_MultiTexCoord0.xy;\n"
        "    gl_Position = gl_Vertex;\n"
        "}\n";
string billboard_geometry_shader =
        "varying in vec3 invel[1];\n"
        "varying in vec2 ininfo[1];\n"
        "varying out vec2 tp;\n"
        "varying out float depth;\n"
        "void\n"
        "main()\n"
        "{\n"
        "    float i = ininfo[0].x;\n"
        //   determine size of billboard
        "    float r = 0.005 + 0.005*max(0.0,pow(i,30.0));\n"
        //   color of particle
        "    gl_FrontColor.xyz = vec3( pow(i,30.0), ininfo[0].y, 0.8 );\n"
        "    vec4 p = gl_PositionIn[0];\n"
        //   calculate depth, see note in fragment shader
        "    vec4 ppp = (gl_ProjectionMatrix * p);\n"
        "    depth = 0.5*((ppp.z)/ppp.w)+0.5;\n"
        //   emit a billboard quad as a tiny triangle strip
        "    tp = vec2(-1.0,-1.0);\n"
        "    gl_Position = gl_ProjectionMatrix*(p + vec4(-r,-r, 0.0, 1.0 ));\n"
        "    EmitVertex();\n"
        "    tp = vec2(-1.0, 1.0);\n"
        "    gl_Position = gl_ProjectionMatrix*(p + vec4(-r, r, 0.0, 1.0 ));\n"
        "    EmitVertex();\n"
        "    tp = vec2( 1.0,-1.0);\n"
        "    gl_Position = gl_ProjectionMatrix*(p + vec4( r,-r, 0.0, 1.0 ));\n"
        "    EmitVertex();\n"
        "    tp = vec2( 1.0, 1.0);\n"
        "    gl_Position = gl_ProjectionMatrix*(p + vec4( r, r, 0.0, 1.0 ));\n"
        "    EmitVertex();\n"
        "}\n";

string billboard_fragment_shader =
        "varying in vec2 tp;\n"
        "varying in float depth;\n"
        "void\n"
        "main()\n"
        "{\n"
        "    gl_FragColor = pow((max(1.0-length(tp),0.0)),2.0)*gl_Color;\n"
        // for some reason the depth test doesn't work as expected if the depth
        // isn't written... at least on my setup.
        "    gl_FragDepth = depth;\n"
        "}\n";

void
init()

{
    // --- check for extensions ------------------------------------------------
    bool has_extensions = true;
    cerr << "GL_NV_transform_feedback: "
         << (GLEW_NV_transform_feedback ? "present" : "missing") << endl;
    has_extensions = has_extensions && GLEW_NV_transform_feedback;
    cerr << "GL_EXT_geometry_shader4: "
         << (GLEW_EXT_geometry_shader4 ? "present" : "missing") << endl;
    has_extensions = has_extensions && GLEW_EXT_geometry_shader4;

    if( !has_extensions ) {
        cerr << "Required extensions missing, exiting." << endl;
        exit( EXIT_FAILURE );
    }

    // --- create HistoPyramid -------------------------------------------------
    hpmc_c = HPMCcreateConstants( HPMC_TARGET_GL20_GLSL110, HPMC_DEBUG_STDERR );
    hpmc_h = HPMCcreateIsoSurface( hpmc_c );

    HPMCsetLatticeSize( hpmc_h,
                        volume_size_x,
                        volume_size_y,
                        volume_size_z );

    HPMCsetGridSize( hpmc_h,
                     volume_size_x-1,
                     volume_size_y-1,
                     volume_size_z-1 );

    HPMCsetGridExtent( hpmc_h,
                       1.0f,
                       1.0f,
                       1.0f );

    HPMCsetFieldCustom( hpmc_h,
                        fetch_code.c_str(),
                        0,
                        GL_TRUE );
    ASSERT_GL;

    // --- create traversal vertex shader --------------------------------------
    hpmc_th = HPMCcreateIsoSurfaceRenderer( hpmc_h );

    char *traversal_code = HPMCisoSurfaceRendererShaderSource( hpmc_th );
    const char* vertex_shader[2] =
    {
        traversal_code,
        custom_vertex_shader.c_str()
    };
    onscreen_v = glCreateShader( GL_VERTEX_SHADER );
    glShaderSource( onscreen_v, 2, &vertex_shader[0], NULL );
    compileShader( onscreen_v, "onscreen vertex shader" );
    free( traversal_code );

    const char* fragment_shader[2] =
    {
        custom_fragment_shader.c_str()
    };
    onscreen_f = glCreateShader( GL_FRAGMENT_SHADER );
    glShaderSource( onscreen_f, 1, &fragment_shader[0], NULL );
    compileShader( onscreen_f, "onscreen fragment shader" );

    const GLchar* onscreen_varying_names[2] =
    {
        "normal_cs",
        "position_cs"
    };

    onscreen_p = glCreateProgram();
    glAttachShader( onscreen_p, onscreen_v );
    glAttachShader( onscreen_p, onscreen_f );
    activateVaryings( onscreen_p, 2, onscreen_varying_names );
    linkProgram( onscreen_p, "onscreen program" );
    setFeedbackVaryings( onscreen_p, 2, onscreen_varying_names );
    ASSERT_GL;

    // associate the linked program with the traversal handle
    HPMCsetIsoSurfaceRendererProgram( hpmc_th,
                                   onscreen_p,
                                   0, 1, 2 );
    ASSERT_GL;

    // --- set up particle emitter program -------------------------------------
    const GLchar* emitter_v_src[1] =
    {
        emitter_vertex_shader.c_str()
    };

    emitter_v = glCreateShader( GL_VERTEX_SHADER );
    glShaderSource( emitter_v,1, &emitter_v_src[0], NULL );
    compileShader( emitter_v, "emitter vertex shader" );

    const GLchar* emitter_g_src[1] =
    {
        emitter_geometry_shader.c_str()
    };
    emitter_g = glCreateShader( GL_GEOMETRY_SHADER_EXT );
    glShaderSource( emitter_g,1, &emitter_g_src[0], NULL );
    compileShader( emitter_g, "emitter geometry shader" );

    const GLchar* emitter_varying_names[3] =
        { "info",
          "vel",
          "pos"
        };

    emitter_p = glCreateProgram();
    glAttachShader( emitter_p, emitter_v );
    glAttachShader( emitter_p, emitter_g );
    glProgramParameteriEXT( emitter_p,
                            GL_GEOMETRY_INPUT_TYPE_EXT, GL_TRIANGLES );
    glProgramParameteriEXT( emitter_p,
                            GL_GEOMETRY_OUTPUT_TYPE_EXT, GL_POINTS );
    glProgramParameteriEXT( emitter_p,
                            GL_GEOMETRY_VERTICES_OUT_EXT, 1 );
    activateVaryings( emitter_p, 3, &emitter_varying_names[0] );
    linkProgram( emitter_p, "emitter program" );
    setFeedbackVaryings( emitter_p, 3, &emitter_varying_names[0] );
    ASSERT_GL;

    // --- set up particle animation program -----------------------------------
    const GLchar* anim_v_src[1] =
    {
        anim_vertex_shader.c_str()
    };
    anim_v = glCreateShader( GL_VERTEX_SHADER );
    glShaderSource( anim_v,1, &anim_v_src[0], NULL );
    compileShader( anim_v, "particle animation vertex shader" );

    const GLchar* anim_g_src[2] =
    {
        fetch_code.c_str(),
        anim_geometry_shader.c_str()
    };
    anim_g = glCreateShader( GL_GEOMETRY_SHADER_EXT );
    glShaderSource( anim_g, 2, &anim_g_src[0], NULL );
    compileShader( anim_g, "particle animation geometry shader" );

    const GLchar* anim_varying_names[3] =
        {
            "info",
            "vel",
            "pos"
        };
    anim_p = glCreateProgram();
    glAttachShader( anim_p, anim_v );
    glAttachShader( anim_p, anim_g );
    glProgramParameteriEXT( anim_p,
                            GL_GEOMETRY_INPUT_TYPE_EXT, GL_POINTS );
    glProgramParameteriEXT( anim_p,
                            GL_GEOMETRY_OUTPUT_TYPE_EXT, GL_POINTS );
    glProgramParameteriEXT( anim_p,
                            GL_GEOMETRY_VERTICES_OUT_EXT, 1 );
    activateVaryings( anim_p, 3, &anim_varying_names[0] );
    linkProgram( anim_p, "particle animation program" );
    setFeedbackVaryings( anim_p, 3, &anim_varying_names[0] );
    ASSERT_GL;

    // --- set up particle billboard render program ----------------------------
    const GLchar* billboard_v_src[1] =
    {
        billboard_vertex_shader.c_str()
    };
    billboard_v = glCreateShader( GL_VERTEX_SHADER );
    glShaderSource( billboard_v,1, &billboard_v_src[0], NULL );
    compileShader( billboard_v, "particle billboard render vertex shader" );

    const GLchar* billboard_g_src[2] =
    {
        fetch_code.c_str(),
        billboard_geometry_shader.c_str()
    };
    billboard_g = glCreateShader( GL_GEOMETRY_SHADER_EXT );
    glShaderSource( billboard_g, 2, &billboard_g_src[0], NULL );
    compileShader( billboard_g, "particle billboard render geometry shader" );

    const GLchar* billboard_f_src[1] =
    {
        billboard_fragment_shader.c_str()
    };
    billboard_f = glCreateShader( GL_FRAGMENT_SHADER );
    glShaderSource( billboard_f,1, &billboard_f_src[0], NULL );
    compileShader( billboard_f, "particle billboard render fragment shader" );

    billboard_p = glCreateProgram();
    glAttachShader( billboard_p, billboard_v );
    glAttachShader( billboard_p, billboard_g );
    glAttachShader( billboard_p, billboard_f );
    glProgramParameteriEXT( billboard_p,
                            GL_GEOMETRY_INPUT_TYPE_EXT, GL_POINTS );
    glProgramParameteriEXT( billboard_p,
                            GL_GEOMETRY_OUTPUT_TYPE_EXT, GL_TRIANGLE_STRIP );
    glProgramParameteriEXT( billboard_p,
                            GL_GEOMETRY_VERTICES_OUT_EXT, 4 );
    linkProgram( billboard_p, "particle billboard render program" );
    ASSERT_GL;

    // --- set up buffer to hold feedback data ---------------------------------

    // feedback of MC triangles
    glGenBuffers( 1, &mc_tri_vbo );
    glBindBuffer( GL_ARRAY_BUFFER, mc_tri_vbo );
    mc_tri_vbo_N = 3*1000;
    glBufferData( GL_ARRAY_BUFFER,
                  (3+3)*mc_tri_vbo_N * sizeof(GLfloat),
                  NULL,
                  GL_DYNAMIC_COPY );

    // buffer to hold particles
    glGenBuffers( 2, &particles_vbo[0] );
    particles_vbo_p = 0;
    particles_vbo_n = 0;
    particles_vbo_N = 20000;
    for(int i=0; i<2; i++) {
        glBindBuffer( GL_ARRAY_BUFFER, particles_vbo[i] );
        glBufferData( GL_ARRAY_BUFFER,
                      (2+3+3)*particles_vbo_N * sizeof(GLfloat),
                      NULL,
                      GL_DYNAMIC_COPY );
    }
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    // --- set up queries to track number of primitives produced ----------------

    // primitives produced by emitter
    glGenQueries( 1, &emitter_query );

    // primitives survived animation shader
    glGenQueries( 1, &anim_query );
}

// -----------------------------------------------------------------------------
void
render( float t, float dt, float fps )
{
    static int threshold = 500;
    if( t < 1e-6 ) {
        particles_vbo_n = 0.0f;
        threshold = 500;
        particles_vbo_p = 0;
        srand(42);
        std::cerr << "reset\n";
    }

    // --- clear screen and set up view ----------------------------------------
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glFrustum( -0.1*aspect_x, 0.1*aspect_x,
               -0.1*aspect_y, 0.1*aspect_y,
               0.5, 3.0 );

    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    glTranslatef( 0.0f, 0.0f, -2.0f );
    glRotatef( 20, 1.0, 0.0, 0.0 );
    glRotatef( 90*cos(0.3*t), 0.0, 1.0, 0.0 );
    glRotatef( 2.7*t, 1.0, 0.0, 0.0 );
    glRotatef( 50+4.0*t, 0.0, 0.0, 1.0 );
    glTranslatef( -0.5f, -0.5f, -0.5f );

    // ---- calc coefficients of shape -----------------------------------------

    // the algebraic shapes we morph between
    static GLfloat C[7][12] = {
        // x^5,	x^4,	y^4,	z^4,	x^2y^2,	x^2z^2,	y^2z^2,	xyz,	x^2,	y^2,	z^2,	1
        // helix
        { 0.0,	-2.0,	0.0,	0.0,	0.0,	0.0,	-1.0,	0.0,	6.0,	0.0,	0.0,	0.0 },
        // some in-between shapes
        { 0.0,	8.0,	0.5,	0.5,	4.0,	4.0,	-1.4,	0.0,	0.0,	0.0,	0.0,	0.0 },
        { 0.0,  16.0,	1.0,	1.0,	8.0,	8.0,	-2.0,	0.0,	-6.0,	0.0,	0.0,	0.0 },
        // daddel
        { 0.0,	0.0,	0.0,	0.0,	0.0,	0.0,	0.0,	0.0,	1.0,	1.0,	0.3,	-0.95 },
        // torus
        { 0.0,	1.0,	1.0,	1.0,	2.0,	2.0,	2.0,	0.0,	-1.01125, -1.01125, 0.94875, 0.225032 },
        // kiss
        { -0.5,	-0.5,	0.0,	0.0,	0.0,	0.0,	0.0,	0.0,	0.0,	1.0,	1.0,	0.0 },
        // cayley
        { 0.0,	0.0,	0.0,	0.0,	0.0,	0.0,	0.0,	16.0,	4.0,	4.0,	4.0,	-1.0 },
    };

    GLfloat CC[12];
    int shape1 = static_cast<int>(t/13.0f) % 7;
    int shape2 = static_cast<int>((t+1.0)/13.0f) % 7;
    float u = fmodf( (t+1.0f), 13.0f );
    for(int i=0; i<12; i++) {
        CC[i] = (1.0f-u)*C[shape1][i] + u*C[shape2][i];
    }


    // --- build HistoPyramid --------------------------------------------------
    double iso = 0.001f;//sin(0.1*t);//4.0*fabs(sin(0.4*t));
    GLuint builder = HPMCgetBuilderProgram( hpmc_h );
    glUseProgram( builder );
    glUniform1fv( glGetUniformLocation( builder, "shape" ), 12, &CC[0] );
    HPMCbuildIsoSurface( hpmc_h, iso );

    // Get number of vertices in MC triangulation, forces CPU-GPU sync.
    GLsizei N = HPMCacquireNumberOfVertices( hpmc_h );

    // Resize triangulatin VBO to be large enough to hold the triangulation.
    if( mc_tri_vbo_N < N ) {
        mc_tri_vbo_N = static_cast<GLsizei>( 1.1f*static_cast<float>(N) );
        cerr << "resizing mc_tri_vbo to hold "
             << mc_tri_vbo_N << " vertices." << endl;

        glBindBuffer( GL_ARRAY_BUFFER, mc_tri_vbo );
        glBufferData( GL_ARRAY_BUFFER,
                      (3+3) * mc_tri_vbo_N * sizeof(GLfloat),
                      NULL,
                      GL_DYNAMIC_COPY );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );
    }
    ASSERT_GL;

    glEnable( GL_DEPTH_TEST );

    // --- render solid surface ------------------------------------------------
    // render to screen and store triangles into mc_tri_vbo buffer. Since we
    // know the number of triangles that we are going to render, we don't have
    // to do a query on the result.
    glUseProgram( onscreen_p );
    glUniform1fv( glGetUniformLocation( onscreen_p, "shape" ), 12, &CC[0] );
    glBindBufferBaseNV( GL_TRANSFORM_FEEDBACK_BUFFER_NV, 0, mc_tri_vbo );
    HPMCextractVerticesTransformFeedbackNV( hpmc_th, GL_FALSE );
    ASSERT_GL;

    // --- emit particles ------------------------------------------------------
    // Threshold such that only every n'th triangle produce a particle. This
    // threshold is adjusted according to the number of points actually
    // produced.
    glUseProgram( emitter_p );
    glUniform1i( glGetUniformLocation( emitter_p, "threshold" ), threshold );
    int off = (int)(threshold*(rand()/(RAND_MAX+1.0f) ) );
    glUniform1i( glGetUniformLocation( emitter_p, "off" ), off );

    // Store emitted particles in the beginning of next frame's particle
    // buffer.
    glBindBufferBaseNV( GL_TRANSFORM_FEEDBACK_BUFFER_NV,
                          0, particles_vbo[ (particles_vbo_p+1)%2 ] );

    // Set up rendering of the triangles stored in the previous transform
    // feedback step.
    glBindBuffer( GL_ARRAY_BUFFER, mc_tri_vbo );
    glPushClientAttrib( GL_CLIENT_VERTEX_ARRAY_BIT );
    glInterleavedArrays( GL_N3F_V3F, 0, NULL );

    // We don't render particles generated this frame, so we discard all
    // geometry at the rasterizer stage.
    glEnable( GL_RASTERIZER_DISCARD_NV );

    // Begin feedback and query.
    glBeginQuery( GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN_NV, emitter_query );
    glBeginTransformFeedbackNV( GL_POINTS );
    glDrawArrays( GL_TRIANGLES, 0, N );
    glEndTransformFeedbackNV();
    glEndQuery( GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN_NV );
    glDisable( GL_RASTERIZER_DISCARD_NV );
    glPopClientAttrib();
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    ASSERT_GL;

    // Get hold of number of primitives produced (number of new particles).
    GLuint emitter_result;
    glGetQueryObjectuiv( emitter_query, GL_QUERY_RESULT, &emitter_result );
    ASSERT_GL;

    // Adjust threshold, try to keep a steady flow of newly generated particles
    float particles_per_sec = emitter_result/max(1e-5f,dt);

    if( particles_per_sec < particle_flow-100 ) {
        threshold = max(1, static_cast<int>(0.5*threshold) );
    }
    else if( particles_per_sec > particle_flow+100 ) {
        threshold = min( 100000, static_cast<int>(10.1*threshold) );
    }

    // --- animate and render particles ----------------------------------------
    // We animate the particles from the previous frame, deleting the ones that
    // is too old, and concatenate the result behind the newly created
    // particles. We also pass the particles on to the fragment shader for
    // rendering. To get more fancy effects, one would add a dedicated rendering
    // pass that expanded every particle into a billboard and do some blending.
    glUseProgram( anim_p );
    glUniform1fv( glGetUniformLocation( anim_p, "shape" ), 12, &CC[0] );
    glUniform1f( glGetUniformLocation( anim_p, "dt"), dt );
    glUniform1f( glGetUniformLocation( anim_p, "iso"), iso );

    // Output after the results of the emitter.
    glBindBufferOffsetNV( GL_TRANSFORM_FEEDBACK_BUFFER_NV,
                          0, particles_vbo[ (particles_vbo_p+1)%2 ],
                          emitter_result*(2+3+3)*sizeof(GLfloat) );

    // Render previous frame's particles

    glBindBuffer( GL_ARRAY_BUFFER, particles_vbo[ particles_vbo_p ] );
    glPushClientAttrib( GL_CLIENT_VERTEX_ARRAY_BIT );
    glInterleavedArrays( GL_T2F_N3F_V3F, 0, NULL );
    glEnable( GL_RASTERIZER_DISCARD_NV );
    glBeginQuery( GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN_NV, anim_query );
    glBeginTransformFeedbackNV( GL_POINTS );
    glDrawArrays( GL_POINTS, 0, particles_vbo_n );
    glEndTransformFeedbackNV();
    glEndQuery( GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN_NV );
    glDisable( GL_RASTERIZER_DISCARD_NV );
    glPopClientAttrib();

    // Get hold of the number of particles that didn't die of old age.
    GLuint anim_result;
    glGetQueryObjectuiv( anim_query, GL_QUERY_RESULT, &anim_result );
    ASSERT_GL;

    // Update buffer pointer and number of particles in this frame's buffer.
    particles_vbo_p = (particles_vbo_p+1)%2;
    particles_vbo_n = anim_result + emitter_result;

    // --- render all particles as billboards ----------------------------------
    glUseProgram( billboard_p );
    glDepthMask( GL_FALSE );
    glEnable( GL_BLEND );
    glBlendFunc( GL_ONE, GL_ONE );
    glBindBuffer( GL_ARRAY_BUFFER, particles_vbo[ particles_vbo_p ] );
    glPushClientAttrib( GL_CLIENT_VERTEX_ARRAY_BIT );
    glInterleavedArrays( GL_T2F_N3F_V3F, 0, NULL );
    glDrawArrays( GL_POINTS, 0, particles_vbo_n );
    glPopClientAttrib();
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glDisable( GL_BLEND );
    glDepthMask( GL_TRUE );
    ASSERT_GL;

    // --- render text string --------------------------------------------------
    static char message[512] = "";

    if( floor(5.0*(t-dt)) != floor(5.0*(t)) ) {
        snprintf( message, 512,
                  "%.1f fps, %dx%dx%d samples, %d mvps, %d triangles, %d particles",
                  fps,
                  volume_size_x,
                  volume_size_y,
                  volume_size_z,
                  (int)( ((volume_size_x-1)*(volume_size_y-1)*(volume_size_z-1)*fps)/1e6 ),
                  N/3,
                  anim_result );

    }

    glUseProgram( 0 );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();

    glDisable( GL_DEPTH_TEST );
    glColor3f( 1.0, 1.0, 1.0 );
    glRasterPos2f( -0.99, 0.95 );
    for(int i=0; i<255 && message[i] != '\0'; i++) {
        glutBitmapCharacter( GLUT_BITMAP_8_BY_13, (int)message[i] );
    }

}


// -----------------------------------------------------------------------------
int
main(int argc, char **argv)
{
    glutInit( &argc, argv );
    if( argc == 2 ) {
        volume_size_x = volume_size_y = volume_size_z = atoi( argv[1] );
    }
    else if( argc == 4 ) {
        volume_size_x = atoi( argv[1] );
        volume_size_y = atoi( argv[2] );
        volume_size_z = atoi( argv[3] );
    }
    else {
        volume_size_x = 64;
        volume_size_y = 64;
        volume_size_z = 64;
    }

    glutInitDisplayMode( GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH );
    glutInitWindowSize( 1280, 720 );
    glutCreateWindow( argv[0] );
    glewInit();
    glutReshapeFunc( reshape );
    glutDisplayFunc( display );
    glutKeyboardFunc( keyboard );
    glutIdleFunc( idle );
    init();
    glutMainLoop();
    return EXIT_SUCCESS;
}
