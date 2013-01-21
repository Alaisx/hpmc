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
uniform float twist;
uniform vec3 centers[8];

float
HPMC_fetch( vec3 p )
{
    p = 2.0*p - 1.0;
    float rot1 = twist*p.z;
    float rot2 = 0.7*twist*p.y;
    p = mat3( cos(rot1), -sin(rot1), 0,
              sin(rot1),  cos(rot1), 0,
              0,          0, 1)*p;
    p = mat3( cos(rot2), 0, -sin(rot2),
              0, 1,          0,
              sin(rot2), 0,  cos(rot2) )*p;
    p = 0.5*p + vec3(0.5);
    float s = 0.0;
    for(int i=0; i<8; i++) {
        vec3 r = p-centers[i];
        s += 0.05/dot(r,r);
    }
    return s;
}
