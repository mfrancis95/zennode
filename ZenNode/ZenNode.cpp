//----------------------------------------------------------------------------
//
// File:        ZenNode.cpp
// Date:        26-Oct-1994
// Programmer:  Marc Rousseau
//
// Description: This module contains the logic for the NODES builder.
//
// Copyright (c) 1994-2000 Marc Rousseau, All Rights Reserved.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
//
// Revision History:
//
//   06-??-95	Added LineDef alias list to speed up the process.
//   07-07-95	Added currentAlias/Side/Flipped to speed up WhichSide.
//   07-11-95	Initialized global variables in CreateNODES.
//		Changed logic for static variable last in CreateSSector.
//   10-05-95	Added convexList & extended the use of lineUsed.
//   10-25-95	Changed from doubly linked lists to an array of SEGs.
//   10-27-95	Added header to each function describing what it does.
//   11-14-95	Fixed sideInfo so that a SEG is always to it's own right.
//   12-06-95	Added code to support selective unique sectors & don't splits
//   05-09-96	Added nodePool to reduced calls to new/delete for NODEs
//   05-15-96	Reduced memory requirements for convexList & sectorInfo
//   05-23-96	Added FACTOR_XXX to reduced memory requirements
//   05-24-96	Removed all calls to new/delete during CreateNode
//   05-31-96	Made WhichSide inline & moved the bulk of code to _WhichSide
//   10-01-96	Reordered functions & removed all function prototypes
//   07-31-00   Increased max subsector factor from 15 to 256
//
//----------------------------------------------------------------------------

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.hpp"
#include "level.hpp"
#include "ZenNode.hpp"

#if defined ( DEBUG )

    #include "logger.hpp"

    static float     worstVertex;
    static float     worstSegs;
    static float     worstNode;
    static float     worstSSector;
    void LogFactors ()
    {
        gLogger->Log ( 0, " Max Vertex: %f\n", worstVertex );
        gLogger->Log ( 0, "   Max Segs: %f\n", worstSegs );
        gLogger->Log ( 0, "   Max Node: %f\n", worstNode );
        gLogger->Log ( 0, "Max SSector: %f\n", worstSSector );
    }
    #pragma exit LogFactors

    #if defined ( DEBUG_TRACE )
        static int       depth;
        static int       noCalls;
        static int       tgtCall = -1;
        static int      *partList;
        static int      *partPtr;
        static char      traceBuffer [ 4096 ];
        static char     *tracePtr;
    #endif

#endif

// Emperical values derived from a test of all the .WAD files from id & Raven.
#define FACTOR_VERTEX		1.7		// 1.662791
#define FACTOR_SEGS		2.0		// 1.488095
#define FACTOR_NODE		2.2		// 1.030612
//define FACTOR_SSECTOR		7.6		// 7.518518
#define FACTOR_SSECTOR		50.0		// 7.518518

#define FACTOR_SUBSECTORS	256

static int       maxSegs;
static int       maxVertices;

static int       nodesLeft;
static NODE     *nodePool;
static NODE     *nodeStart = NULL;
static int       nodeCount;			// Number of NODES stored

static SEG      *tempSeg;
static SEG      *segStart = NULL;
static int       segCount;			// Number of SEGS stored

static int       ssectorsLeft;
static wSSector *ssectorPool;
static int       ssectorCount;			// Number of SSECTORS stored

static wVertex  *newVertices = NULL;
static int       noVertices;

//
// Variables used by WhichSide to speed up side calculations
//
static char     *currentSide;
static int       currentFlipped;
static sAlias   *currentAlias;

static int      *convexList;
static int      *convexPtr;
static int       sectorCount;

static int       showProgress;
static UCHAR    *usedSector;
static bool     *keepUnique;
static bool      uniqueSubsectors;
static bool     *lineUsed;
static bool     *lineChecked;
static int       noAliases;
static sAlias   *lineDefAlias;
static char    **sideInfo;
/*static*/ long      DY, DX, X, Y, H_2, ANGLE;
/*static*/ REAL      C;

static sScoreInfo *score;
		  
// metric = S ? ( L * R ) / ( X1 ? X1 * S / X2 : 1 ) - ( X3 * S + X4 ) * S : ( L * R );
static long X1 = getenv ( "ZEN_X1" ) ? atol ( getenv ( "ZEN_X1" )) : 24;
static long X2 = getenv ( "ZEN_X2" ) ? atol ( getenv ( "ZEN_X2" )) : 5;
static long X3 = getenv ( "ZEN_X3" ) ? atol ( getenv ( "ZEN_X3" )) : 1;
static long X4 = getenv ( "ZEN_X4" ) ? atol ( getenv ( "ZEN_X4" )) : 25;

static long Y1 = getenv ( "ZEN_Y1" ) ? atol ( getenv ( "ZEN_Y1" )) : 1;
static long Y2 = getenv ( "ZEN_Y2" ) ? atol ( getenv ( "ZEN_Y2" )) : 7;
static long Y3 = getenv ( "ZEN_Y3" ) ? atol ( getenv ( "ZEN_Y3" )) : 1;
static long Y4 = getenv ( "ZEN_Y4" ) ? atol ( getenv ( "ZEN_Y4" )) : 0;

static SEG *(*PartitionFunction) ( SEG *, int );

#if ! defined ( __OS2_USE_ASM__ )
    inline void fixfloat () {}
    inline void restorefloat () {}
#else
    #include <float.h>
    inline void fixfloat () { _control87 ( 0x0C00, 0x0C00 ); }
    inline void restorefloat () { _fpreset (); }
#endif

//----------------------------------------------------------------------------
//  Create a list of SEGs from the *important* sidedefs.  A sidedef is
//    considered important if:
//     It has non-zero length
//     It's linedef has different sectors on each side
//     It has at least one visible texture
//----------------------------------------------------------------------------

static SEG *CreateSegs ( DoomLevel *level, sBSPOptions *options )
{
    // Get a rough count of how many SideDefs we're starting with
    segCount = maxSegs = 0;
    const wLineDef *lineDef = level->GetLineDefs ();
    const wSideDef *sideDef = level->GetSideDefs ();
    int i;
    for ( i = 0; i < level->LineDefCount (); i++ ) {
        if ( lineDef[i].sideDef[0] != NO_SIDEDEF ) maxSegs++;
        if ( lineDef[i].sideDef[1] != NO_SIDEDEF ) maxSegs++;
    }
    tempSeg  = new SEG [ maxSegs ];
    maxSegs  = ( int ) ( maxSegs * FACTOR_SEGS );
    segStart = new SEG [ maxSegs ];
    memset ( segStart, 0, sizeof ( SEG ) * maxSegs );

    SEG *seg = segStart;
    for ( i = 0; i < level->LineDefCount (); i++, lineDef++ ) {

        wVertex *vertS = &newVertices [ lineDef->start ];
        wVertex *vertE = &newVertices [ lineDef->end ];
        long dx = vertE->x - vertS->x;
        long dy = vertE->y - vertS->y;
        if (( dx == 0 ) && ( dy == 0 )) continue;

        int rSide = lineDef->sideDef[0];
        int lSide = lineDef->sideDef[1];
        const wSideDef *sideRight = ( rSide == NO_SIDEDEF ) ? ( const wSideDef * ) NULL : &sideDef [ rSide ];
        const wSideDef *sideLeft = ( lSide == NO_SIDEDEF ) ? ( const wSideDef * ) NULL : &sideDef [ lSide ];

        // Ignore line if both sides point to the same sector & neither side has any visible texture
        if ( options->reduceLineDefs && sideRight && sideLeft && ( sideRight->sector == sideLeft->sector )) {
            if ( * ( USHORT * ) sideLeft->text3 == EMPTY_TEXTURE ) {
                sideLeft = ( const wSideDef * ) NULL;
            }
            if ( * ( USHORT * ) sideRight->text3 == EMPTY_TEXTURE ) {
                sideRight = ( const wSideDef * ) NULL;
            }
            if ( ! sideLeft && ! sideRight ) continue;
        }

        if ( options->ignoreLineDef && options->ignoreLineDef [i] ) continue;

        BAM angle = ( dy == 0 ) ? ( BAM ) (( dx < 0 ) ? BAM180 : 0 ) :
                    ( dx == 0 ) ? ( BAM ) (( dy < 0 ) ? BAM270 : BAM90 ) :
                                  ( BAM ) ( atan2 ( dy, dx ) * BAM180 / M_PI + 0.5 * sgn ( dy ));

        int split = options->dontSplit ? options->dontSplit [i] : false;

        if ( sideRight ) {
            seg->Data.start   = lineDef->start;
            seg->Data.end     = lineDef->end;
            seg->Data.angle   = angle;
            seg->Data.lineDef = ( USHORT ) i;
            seg->Data.flip    = 0;
            seg->Sector       = sideRight->sector;
            seg->noSplit      = split;
            seg++;
        }

        if ( sideLeft ) {
            seg->Data.start   = lineDef->end;
            seg->Data.end     = lineDef->start;
            seg->Data.angle   = ( BAM ) ( angle + BAM180 );
            seg->Data.lineDef = ( USHORT ) i;
            seg->Data.flip    = 1;
            seg->Sector       = sideLeft->sector;
            seg->noSplit      = split;
            seg++;
        }
    }
    segCount = seg - segStart;

    return segStart;
}

//----------------------------------------------------------------------------
//  Calculate the set of variables used frequently that are based on the
//    currently selected SEG to be used as a partition line.
//----------------------------------------------------------------------------

static void ComputeStaticVariables ( SEG *pSeg )
{
    currentAlias = &lineDefAlias [ pSeg->Data.lineDef ];
    currentSide = sideInfo ? sideInfo [ currentAlias->index ] : SIDE_SPLIT;
    currentFlipped = ( pSeg->Data.flip ^ currentAlias->flip ) ? SIDE_FLIPPED : SIDE_NORMAL;

    wVertex *vertS = &newVertices [ pSeg->Data.start ];
    wVertex *vertE = &newVertices [ pSeg->Data.end ];
    ANGLE = pSeg->Data.angle;
    X = vertS->x;
    Y = vertS->y;
    DX = vertE->x - vertS->x;
    DY = vertE->y - vertS->y;
    H_2 = ( long ) hypot ( DX, DY );
    C = ( REAL ) (( long ) vertE->y * ( long ) vertS->x ) - ( REAL ) (( long ) vertE->x * ( long ) vertS->y );
}

//----------------------------------------------------------------------------
//  Determine if the given SEG is co-linear ( ie: they lie on the same line )
//    with the currently selected partition.
//----------------------------------------------------------------------------

static bool CoLinear ( SEG *seg )
{
    // If they're not at the same angle ( �180� ), bag it
    if (( ANGLE & 0x7FFF ) != ( seg->Data.angle & 0x7FFF )) return false;

    // Do the math stuff
    wVertex *vertS = &newVertices [ seg->Data.start ];
    if ( DX == 0 ) return ( vertS->x == X ) ? true : false;
    if ( DY == 0 ) return ( vertS->y == Y ) ? true : false;

    // Rotate vertS about (X,Y) by � degrees to get y offset
    //   Y = H�sin(�)           �  1  0  0 �� cos(�)  -sin(�)  0 �
    //   X = H�cos(�)    �x y 1��  0  1  0 �� sin(�)   cos(�)  0 �
    //   H = (X�+Y�)^�         � -X -Y  1 ��   0         0    1 �

    int y = DX * ( vertS->y - Y ) - DY * ( vertS->x - X );

    return (( y == 0 ) || (( y > -H_2 ) && ( y < H_2 ))) ? true : false;
}

//----------------------------------------------------------------------------
//  Given a list of SEGs, determine the bounding rectangle.
//----------------------------------------------------------------------------

static void FindBounds ( wBound *bound, SEG *seg, int noSegs )
{
    wVertex *vert = &newVertices [ seg->Data.start ];
    bound->minx = bound->maxx = vert->x;
    bound->miny = bound->maxy = vert->y;
    for ( int i = 0; i < noSegs; i++ ) {
        wVertex *vertS = &newVertices [ seg->Data.start ];
        wVertex *vertE = &newVertices [ seg->Data.end ];

        int loX = vertS->x, hiX = vertS->x;
        if ( loX < vertE->x ) hiX = vertE->x; else loX = vertE->x;
        int loY = vertS->y, hiY = vertS->y;
        if ( loY < vertE->y ) hiY = vertE->y; else loY = vertE->y;

        if ( loX < bound->minx ) bound->minx = ( SHORT ) loX;
        if ( hiX > bound->maxx ) bound->maxx = ( SHORT ) hiX;
        if ( loY < bound->miny ) bound->miny = ( SHORT ) loY;
        if ( hiY > bound->maxy ) bound->maxy = ( SHORT ) hiY;

        seg++;
    }
}

#if ! defined ( __OS2_USE_ASM__ )

//----------------------------------------------------------------------------
//  Determine if a point lies on the line or not.  This is called when a point
//    lies close to the line segment.  Since a different algorithm is used to
//    determine which side a segment is on, points that are very close need
//    to be checked using the same algorithm that does the splits.
//----------------------------------------------------------------------------

long IsZero ( SEG *seg, wVertex *vert, long side )
{    				       		
    // IsZero is only called if an end-point is very close to the partition line
    wVertex *vertS = &newVertices [ seg->Data.start ];
    wVertex *vertE = &newVertices [ seg->Data.end ];

    REAL dx = ( REAL ) ( vertE->x - vertS->x );
    REAL dy = ( REAL ) ( vertE->y - vertS->y );

    // if det == 0 the line is parallel, assume it's also co-linear
    REAL det = ( dx * DY - dy * DX );
    if ( det == 0.0 ) return 0;

    REAL c = ( REAL ) (( long ) vertE->y * ( long ) vertS->x ) - ( REAL ) (( long ) vertE->x * ( long ) vertS->y );
    REAL x = ( C * dx - c * DX ) / det;
    REAL y = ( C * dy - c * DY ) / det;
    int newX = ( int ) ( x + 0.5 * sgn ( x ));
    int newY = ( int ) ( y + 0.5 * sgn ( y ));

//    return (( newX == vert->x ) && ( newY == vert->y )) ? 0 : side;

    if (( newX == vert->x ) && ( newY == vert->y )) return 0;

//    if (( newX == vertS->x ) && ( newY == vertS->y )) return 0;
//    if (( newX == vertE->x ) && ( newY == vertE->y )) return 0;
/*
int s1 = DX * ( vertS->y - Y ) - DY * ( vertS->x - X );
int s2 = DX * ( vertE->y - Y ) - DY * ( vertE->x - X );
int s3 = DX * ( newY     - Y ) - DY * ( newX     - X );

gLogger->Log ( 0, "IsZero: LineDef %5d - (%5d,%5d):%c - (%5d,%5d):%c @ (%5d,%5d):%c\n", seg->Data.lineDef,
vertS->x, vertS->y, s1 == 0 ? '0' : s1 <= -H_2 ? '-' : s1 >= H_2 ? '+' : '?',
vertE->x, vertE->y, s1 == 0 ? '0' : s1 <= -H_2 ? '-' : s1 >= H_2 ? '+' : '?',
newX,     newY,     s1 == 0 ? '0' : s1 <= -H_2 ? '-' : s1 >= H_2 ? '+' : '?' );
*/
//    if ((( newX == vert->x + 1 ) || ( newX == vert->x - 1 )) &&
//        (( newY == vert->y + 1 ) || ( newY == vert->y - 1 ))) return 0;

    return side;

//    return DX * ( newY - Y ) - DY * ( newX - X );		     
	   	       	    		   
}

//----------------------------------------------------------------------------
//  Determine which side of the partition line the given SEG lies.  A quick
//    check is made based on the sector containing the SEG.  If the sector
//    is split by the partition, a more detailed examination is made.  The
//    method used rotates the line segment so that the partition line lies
//    along the X-axis.  The endpoints of the line are then checked to see
//    which side of the X-axis they lie on.  This produces results that are
//    actually too accurate.  A point that is close to the axis using this
//    method may actually touch the partition line using the actual algorithm
//    used to do the split.  In this case IsZero is called to decide.
//
//    Returns:
//       -1 - SEG is on the left of the partition
//        0 - SEG is split by the partition
//       +1 - SEG is on the right of the partition
//----------------------------------------------------------------------------

int _WhichSide ( SEG *seg )
{
    wVertex *vertS = &newVertices [ seg->Data.start ];
    wVertex *vertE = &newVertices [ seg->Data.end ];
    long y1, y2;

    if ( DX == 0 ) {
        if ( DY > 0 ) {
            y1 = ( X - vertS->x ),    y2 = ( X - vertE->x );
        } else {
            y1 = ( vertS->x - X ),    y2 = ( vertE->x - X );
        }
    } else if ( DY == 0 ) {
        if ( DX > 0 ) {
            y1 = ( vertS->y - Y ),    y2 = ( vertE->y - Y );
        } else {
            y1 = ( Y - vertS->y ),    y2 = ( Y - vertE->y );
        }
    } else {

        // Rotate vertS & vertE about (X,Y) by � degrees to get y offset
        //   Y = H�sin(�)           �  1  0  0 �� cos(�)  -sin(�)  0 �
        //   X = H�cos(�)    �x y 1��  0  1  0 �� sin(�)   cos(�)  0 �
        //   H = (X�+Y�)^�         � -X -Y  1 ��   0         0    1 �

        long t1 = DX * ( vertS->y - Y ) - DY * ( vertS->x - X );
        long t2 = DX * ( vertE->y - Y ) - DY * ( vertE->x - X );

        y1 = ( t1 <= -H_2 ) ? -1 : ( t1 >= H_2 ) ? 1 : (( t1 == 0 ) || ( t2 == 0 )) ? 0 : IsZero ( seg, vertS, t1 );
        y2 = ( t2 <= -H_2 ) ? -1 : ( t2 >= H_2 ) ? 1 : (( t2 == 0 ) || ( t1 == 0 )) ? 0 : IsZero ( seg, vertE, t2 );

if ((( y1 < -1 ) || ( y1 > 1 )) && ( y2 == 0 ))
  y1 = 0;
if ((( y2 < -1 ) || ( y2 > 1 )) && ( y1 == 0 ))
  y2 = 0;
if ((( y1 < -1 ) || ( y1 > 1 )) && (( y2 < -1 ) || ( y2 > 1 ))) {
  y1 = 0;
  y2 = 0;
}
    }

    // If its co-linear, decide based on direction
    if (( y1 == 0 ) && ( y2 == 0 )) {
        return ( seg->Data.angle == ANGLE ) ? SIDE_RIGHT : SIDE_LEFT;
    }

    // Otherwise:
    //   Left   -1 : ( y1 >= 0 ) && ( y2 >= 0 )
    //   Both    0 : (( y1 < 0 ) && ( y2 > 0 )) || (( y1 > 0 ) && ( y2 < 0 ))
    //   Right   1 : ( y1 <= 0 ) && ( y2 <= 0 )

    return ( y1 <  0 ) ? (( y2 <= 0 ) ? SIDE_RIGHT : SIDE_SPLIT ) :
           ( y1 == 0 ) ? (( y2 <= 0 ) ? SIDE_RIGHT : SIDE_LEFT  ) :
                         (( y2 >= 0 ) ? SIDE_LEFT  : SIDE_SPLIT );
}

#else

extern int _WhichSide ( SEG *seg );

#endif

//inline int WhichSide ( SEG *seg )
int WhichSide ( SEG *seg )
{
    int side = currentSide [ seg->Sector ];
    // NB: side & 1 implies either SIDE_LEFT or SIDE_RIGHT
    if ( IS_LEFT_RIGHT ( side )) return FLIP ( currentFlipped, side );

    sAlias *alias = &lineDefAlias [ seg->Data.lineDef ];
    if ( alias->index == currentAlias->index ) {
        int isFlipped = ( seg->Data.flip ^ alias->flip ) ? SIDE_FLIPPED : SIDE_NORMAL;
        return ( currentFlipped == isFlipped ) ? SIDE_RIGHT : SIDE_LEFT;
    }

    return _WhichSide ( seg );
}

//----------------------------------------------------------------------------
//  Create a list of aliases vs sectors that indicates which side of a given
//    alias a sector is.  This requires:
//     Bounding rectangle information for each sector
//     List of line aliases ( unique lines )
//----------------------------------------------------------------------------

static void CreateSideInfo ( DoomLevel *level, wBound *bound, sSectorInfo *sectInfo, SEG **aliasList )
{
    SEG testSeg, partSeg;
    memset ( &partSeg, 0, sizeof ( SEG ));
    memset ( &testSeg, 0, sizeof ( SEG ));

    int v = level->VertexCount ();
    testSeg.Data.lineDef = ( USHORT ) level->LineDefCount ();
    testSeg.Data.start   = ( USHORT ) v;
    testSeg.Data.end     = ( USHORT ) ( v + 1 );

    long size = ( sizeof ( char * ) + level->SectorCount ()) * ( long ) noAliases;
    char *temp = new char [ size ];
    sideInfo = ( char ** ) temp;
    memset ( temp, 0, sizeof ( char * ) * noAliases );
    temp += sizeof ( char * ) * noAliases;
    memset ( temp, SIDE_UNKNOWN, level->SectorCount () * noAliases );
    for ( int i = 0; i < noAliases; i++ ) {

        sideInfo [i] = ( char * ) temp;
        temp += level->SectorCount ();

        SEG *alias = aliasList [i];
        partSeg = *alias;
        ComputeStaticVariables ( &partSeg );
        for ( int j = 0; j < level->SectorCount (); j++ ) {
            int s = sectInfo[j].index;
            if ( sideInfo [i][s] != SIDE_UNKNOWN ) continue;
            testSeg.Sector = s;
            // Create a bounding box around the sector & check the lower edge 1st
            newVertices [v].x = bound[s].minx;
            newVertices [v].y = bound[s].miny;
            newVertices [v+1].x = bound[s].maxx;
            newVertices [v+1].y = bound[s].miny;
            int side1 = WhichSide ( &testSeg );
            if ( side1 != SIDE_SPLIT ) {
                // Now check the upper edge
                newVertices [v].y = bound[s].maxy;
                newVertices [v+1].y = bound[s].maxy;
                int side2 = WhichSide ( &testSeg );
                if ( side2 == side1 ) {
                    sSectorInfo *sect = &sectInfo[j];
                    int x = sect->noSubSectors;
                    while ( x ) sideInfo [i][ sect->subSector [--x]] = ( char ) side1;
                } else {
                    sideInfo [i][s] = SIDE_SPLIT;
                }
            } else {
                sideInfo [i][s] = SIDE_SPLIT;
            }
        }
    }
}

//----------------------------------------------------------------------------
//  Create a SSECTOR and record the index of the 1st SEG and the total number
//  of SEGs.
//----------------------------------------------------------------------------

static USHORT CreateSSector ( int noSegs, SEG *segs )
{
    if ( ssectorsLeft-- == 0 ) {
        fprintf ( stderr, "ERROR: ssectorPool exhausted\n" );
        exit ( -1 );
    }
    wSSector *ssec = ssectorPool++;
    ssec->num = ( USHORT ) noSegs;
    ssec->first = ( USHORT ) ( segs - segStart );

    return ( USHORT ) ssectorCount++;
}

//----------------------------------------------------------------------------
//  For each sector, create a bounding rectangle.
//----------------------------------------------------------------------------

static wBound *GetSectorBounds ( DoomLevel *level )
{
    // Calculate bounding rectangles for all sectors
    wBound *bound = new wBound [ level->SectorCount () ];
    int i;
    for ( i = 0; i < level->SectorCount (); i++ ) {
        bound[i].maxx = bound[i].maxy = ( SHORT ) 0x8000;
        bound[i].minx = bound[i].miny = ( SHORT ) 0x7FFF;
    }

    int index;
    const wLineDef *lineDef = level->GetLineDefs ();
    const wVertex  *vertex = level->GetVertices ();
    for ( i = 0; i < level->LineDefCount (); i++ ) {

        const wVertex *vertS = &vertex [ lineDef->start ];
        const wVertex *vertE = &vertex [ lineDef->end ];

        int loX = vertS->x, hiX = vertS->x;
        if ( loX < vertE->x ) hiX = vertE->x; else loX = vertE->x;
        int loY = vertS->y, hiY = vertS->y;
        if ( loY < vertE->y ) hiY = vertE->y; else loY = vertE->y;

        for ( int s = 0; s < 2; s++ ) {
            if (( index = lineDef->sideDef[s] ) != NO_SIDEDEF ) {
                int sec = level->GetSideDefs ()[index].sector;
                if ( loX < bound[sec].minx ) bound[sec].minx = ( SHORT ) loX;
                if ( hiX > bound[sec].maxx ) bound[sec].maxx = ( SHORT ) hiX;
                if ( loY < bound[sec].miny ) bound[sec].miny = ( SHORT ) loY;
                if ( hiY > bound[sec].maxy ) bound[sec].maxy = ( SHORT ) hiY;
            }
        }
        lineDef++;
    }

    return bound;
}

//----------------------------------------------------------------------------
//  Sort sectors so the the largest ( sector containing the most sectors ) is
//    placed first in the list.
//----------------------------------------------------------------------------

int SectorSort ( const void *ptr1, const void *ptr2 )
{
    int dif = (( sSectorInfo * ) ptr2)->noSubSectors - (( sSectorInfo * ) ptr1)->noSubSectors;
    if ( dif ) return dif;
    dif = (( sSectorInfo * ) ptr2)->index - (( sSectorInfo * ) ptr1)->index;
    return -dif;
}

//----------------------------------------------------------------------------
//  Determine which sectors contain which other sectors, then sort the list.
//----------------------------------------------------------------------------

sSectorInfo *GetSectorInfo ( int noSectors, wBound *bound )
{
    long max = noSectors * FACTOR_SUBSECTORS;
    long size = sizeof ( sSectorInfo ) * ( long ) noSectors + sizeof ( int ) * max;
    char *temp = new char [ size ];
    memset ( temp, 0, size );

    sSectorInfo *info = ( sSectorInfo * ) temp;
    temp += sizeof ( sSectorInfo ) * noSectors;

    for ( int i = 0; i < noSectors; i++ ) {
        info[i].index = i;
        info[i].noSubSectors = 0;
        info[i].subSector = ( int * ) temp;
        for ( int j = 0; j < noSectors; j++ ) {
            if (( bound[j].minx >= bound[i].minx ) &&
                ( bound[j].maxx <= bound[i].maxx ) &&
                ( bound[j].miny >= bound[i].miny ) &&
                ( bound[j].maxy <= bound[i].maxy )) {
                int index = info[i].noSubSectors++;
                if ( index >= max ) {
                    fprintf ( stderr, "Too many contained sectors in sector %d\n", i );
                    exit ( -1 );
                }
                info[i].subSector[index] = j;
            }
        }
        temp += sizeof ( int ) * info [i].noSubSectors;
        max -= info [i].noSubSectors;
    }

    qsort ( info, noSectors, sizeof ( sSectorInfo ), SectorSort );
    return info;
}

//----------------------------------------------------------------------------
//  Create a list of aliases.  These are all the unique lines within the map.
//    Each linedef is assigned an alias.  All subsequent calculations are
//    based on the aliases rather than the linedefs, since there are usually
//    significantly fewer aliases than linedefs.
//----------------------------------------------------------------------------

SEG **GetLineDefAliases ( DoomLevel *level )
{
    noAliases = 0;
    lineDefAlias = new sAlias [ level->LineDefCount () + 1 ];
    memset ( lineDefAlias, 0, sizeof ( sAlias ) * ( level->LineDefCount () + 1 ));
    SEG **segAlias = new SEG * [ level->LineDefCount () ];

    SEG *testSeg = NULL, *refSeg = segStart;
    for ( int x, i = 0; i < level->LineDefCount (); i++ ) {

        // Skip lines that have been ignored
        if ( refSeg->Data.lineDef != i ) continue;

        ComputeStaticVariables ( refSeg );
        for ( x = noAliases - 1; x >= 0; x-- ) {
            testSeg = segAlias [x];
            if ( CoLinear ( testSeg )) break;
        }
        if ( x == -1 ) {
            lineDefAlias [i].flip = false;
            segAlias [ x = noAliases++ ] = refSeg;
        } else {
            lineDefAlias [i].flip = ( refSeg->Data.angle == testSeg->Data.angle ) ? false : true;
        }
        lineDefAlias [i].index = x;

        refSeg++;
        if ( refSeg->Data.lineDef == i ) refSeg++;
    }
    lineDefAlias [ level->LineDefCount () ].index = -1;

    return segAlias;
}

//----------------------------------------------------------------------------
//  Return an index for a vertex at (x,y).  If an existing vertex exists,
//    return it, otherwise, create a new one if room is left.
//----------------------------------------------------------------------------

static int AddVertex ( int x, int y )
{
    for ( int i = 0; i < noVertices; i++ ) {
        if (( newVertices [i].x == x ) && ( newVertices[i].y == y )) return i;
    }

    if ( noVertices >= maxVertices ) {
        fprintf ( stderr, "\nError: maximum number of vertices exceeded.\n" );
        exit ( -1 );
    }

    newVertices [ noVertices ].x = ( USHORT ) x;
    newVertices [ noVertices ].y = ( USHORT ) y;
    return noVertices++;
}

//----------------------------------------------------------------------------
//  Sort two SEGS so that the one with the lowest numbered LINEDEF is first.
//----------------------------------------------------------------------------

static int SortByLineDef ( const void *ptr1, const void *ptr2 )
{
    int dif = (( SEG * ) ptr1)->Data.lineDef - (( SEG * ) ptr2)->Data.lineDef;
    if ( dif ) return dif;
    return (( SEG * ) ptr1)->Data.flip - (( SEG * ) ptr2)->Data.flip;
}

//----------------------------------------------------------------------------
//  If the given SEGs form a proper NODE but don't all belong to the same
//    sector, artificially break up the NODE by sector.  SEGs are arranged
//    so that SEGs belonging to sectors that should be kept unique are listed
//    first, followed by those that are not - and sorted by linedef for each
//    category.
//----------------------------------------------------------------------------

static int SortBySector ( const void *ptr1, const void *ptr2 )
{
    int dif;
    int sector1 = (( SEG * ) ptr1)->Sector;
    int sector2 = (( SEG * ) ptr2)->Sector;
    dif = keepUnique [ sector2 ] - keepUnique [ sector1 ];
    if ( dif ) return dif;
    dif = sector1 - sector2;
    if ( dif ) return dif;
    return SortByLineDef ( ptr1, ptr2 );
}

static void SortSectors ( SEG *seg, int noSegs, int *noLeft, int *noRight )
{
    qsort ( seg, noSegs, sizeof ( SEG ), SortBySector );

    // Seperate the 1st keep-unique sector - leave the rest
    int sector = seg->Sector;
    int i;
    for ( i = 0; seg[i].Sector == sector; i++ ) ;

    *noRight = i;
    *noLeft = noSegs - i;
}

static void SortSegs ( SEG *pSeg, SEG *seg, int noSegs, int *noLeft, int *noRight, int *noSplits )
{
#if ! defined ( DEBUG )
    if ( pSeg == NULL ) {
        *noRight = noSegs;
        *noSplits = 0;
        *noLeft = 0;
        return;
    }

    ComputeStaticVariables ( pSeg );
#else
    ComputeStaticVariables ( pSeg ? pSeg : seg );
#endif

    int count[3];
    count [0] = count [1] = count [2] = 0;
    int i;
    for ( i = 0; i < noSegs; i++ ) {
        count [ ( seg[i].Side = WhichSide ( &seg[i] )) + 1 ]++;
    }

    *noLeft = count[0], *noSplits = count[1], *noRight = count[2];

#if ! defined ( DEBUG )
    assert (( *noLeft != 0 ) || ( *noSplits != 0 ));
#else
    if ( pSeg == NULL ) {
#if defined ( DEBUG_TRACE )
        if ( *noLeft || *noSplits ) {
            gLogger->Log ( 0, tracePtr = traceBuffer );
            gLogger->Alert ( 1, "ERROR: Something wierd is going on! (%d|%d|%d) %4d  Call: %d", *noLeft, *noSplits, *noRight, noSegs, noCalls );
            for ( int i = 0; i < noSegs; i++ ) {
                int alias = lineDefAlias [ seg[i].Data.lineDef ].index;
                gLogger->Log ( 0, "    lineDef: %5d  Alias: %5d%s\n", seg[i].Data.lineDef, alias, lineUsed [ alias ] ? "*" : "" );
            }
        }
#else
        if ( *noLeft || *noSplits ) fprintf ( stderr, "\nERROR: Something wierd is going on! (%d|%d|%d) %4d", *noLeft, *noSplits, *noRight, noSegs );
#endif
        *noRight = noSegs;
        *noSplits = 0;
        *noLeft = 0;
        return;
    }
#endif

    SEG *rSeg = seg;
    for ( i = 0; seg[i].Side == SIDE_RIGHT; i++ ) rSeg++;

    if (( i < count[2] ) || count[1] ) {
        SEG *sSeg = tempSeg;
        SEG *lSeg = sSeg + *noSplits;
        for ( ; i < noSegs; i++ ) {
            switch ( seg[i].Side ) {
                case SIDE_LEFT  : *lSeg++ = seg [i];		break;
                case SIDE_SPLIT : *sSeg++ = seg [i];		break;
                case SIDE_RIGHT : *rSeg++ = seg [i];		break;
            }
        }
        memcpy ( rSeg, tempSeg, ( noSegs - count[2] ) * sizeof ( SEG ));
    }
    return;
}

//----------------------------------------------------------------------------
//  Use the requested algorithm to select a partition for the list of SEGs.
//    After a valid partition is selected, the SEGs are re-ordered.  All SEGs
//    to the right of the partition are placed first, then those that will
//    be split, followed by those that are to the left.
//----------------------------------------------------------------------------

static bool ChoosePartition ( SEG *seg, int noSegs, int *noLeft, int *noRight, int *noSplits )
{
    memcpy ( lineChecked, lineUsed, noAliases );

    SEG *pSeg = PartitionFunction ( seg, noSegs );
#if defined ( DEBUG_TRACE )
    if ( pSeg ) tracePtr += sprintf ( tracePtr, "lineDef: %4d ", pSeg->Data.lineDef );
#endif
    SortSegs ( pSeg, seg, noSegs, noLeft, noRight, noSplits );
    return pSeg ? true : false;
}

//----------------------------------------------------------------------------
//  ALGORITHM 1: 'ZenNode Classic'
//    This is the original algorithm used by ZenNode.  It simply attempts
//    to minimize the number of SEGs that are split.  This actually yields
//    very small BSP trees, but usually results in trees that are not well
//    balanced and run deep.
//----------------------------------------------------------------------------

static SEG *Algorithm1 ( SEG *segs, int noSegs )
{
    SEG *pSeg = NULL, *testSeg = segs;
    int count [3];
    int &lCount = count[0], &sCount = count[1], &rCount = count[2];
    // Compute the maximum value maxMetric can possibly reach
    long bestMetric = ( noSegs / 2 ) * ( noSegs - noSegs / 2 );
    long maxMetric = 0x80000000, maxSplits = 0x7FFFFFFF;

    for ( int i = 0; i < noSegs; i++ ) {
        if ( showProgress && (( i & 15 ) == 0 )) ShowProgress ();
        int alias = lineDefAlias [ testSeg->Data.lineDef ].index;
        if ( ! lineChecked [ alias ]) {
            lineChecked [ alias ] = true;
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            if ( maxMetric < 0 ) for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
            } else for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
                if ( sCount > maxSplits ) goto next;
            }
            // Only consider SEG if it is not a boundary line
            if ( lCount + sCount ) {
                long temp, metric = sCount ? (( long ) lCount * ( long ) rCount ) / ( X1 ? ( temp = X1 * sCount / X2 ) != 0 ? temp : 1 : 1 ) - ( X3 * sCount + X4 ) * sCount : (( long ) lCount * ( long ) rCount );
                if ( ANGLE & 0x3FFF ) metric--;
                if ( metric == bestMetric ) return testSeg;
                if ( metric > maxMetric ) {
                    pSeg = testSeg;
                    maxSplits = sCount + 2;
                    maxMetric = metric;
                }
            } else {
                // Eliminate outer edges of the map from here & down
                *convexPtr++ = alias;
            }
        }
next:
        testSeg++;
    }
    return pSeg;
}

//----------------------------------------------------------------------------
//  ALGORITHM 2: 'ZenNode Quality'
//    This is the 2nd algorithm used by ZenNode.  It attempts to keep the
//    resulting BSP tree balanced based on the number of sectors on each side of
//    the partition line in addition to the number of SEGs.  This seems more 
//    reasonable since a given SECTOR is usually made up of one or more SSECTORS.
//----------------------------------------------------------------------------

int sortTotalMetric ( const void *ptr1, const void *ptr2 )
{
    int dif;
    dif = (( sScoreInfo * ) ptr1)->invalid - (( sScoreInfo * ) ptr2)->invalid;
    if ( dif ) return dif;
    dif = (( sScoreInfo * ) ptr1)->total - (( sScoreInfo * ) ptr2)->total;
    if ( dif ) return dif;
    dif = (( sScoreInfo * ) ptr1)->index - (( sScoreInfo * ) ptr2)->index;
    return dif;
}

int sortMetric1 ( const void *ptr1, const void *ptr2 )
{
    if ((( sScoreInfo * ) ptr2)->metric1 < (( sScoreInfo * ) ptr1)->metric1 ) return -1;
    if ((( sScoreInfo * ) ptr2)->metric1 > (( sScoreInfo * ) ptr1)->metric1 ) return  1;
    if ((( sScoreInfo * ) ptr2)->metric2 < (( sScoreInfo * ) ptr1)->metric2 ) return -1;
    if ((( sScoreInfo * ) ptr2)->metric2 > (( sScoreInfo * ) ptr1)->metric2 ) return  1;
    return (( sScoreInfo * ) ptr1)->index - (( sScoreInfo * ) ptr2)->index;
}

int sortMetric2 ( const void *ptr1, const void *ptr2 )
{
    if ((( sScoreInfo * ) ptr2)->metric2 < (( sScoreInfo * ) ptr1)->metric2 ) return -1;
    if ((( sScoreInfo * ) ptr2)->metric2 > (( sScoreInfo * ) ptr1)->metric2 ) return  1;
    if ((( sScoreInfo * ) ptr2)->metric1 < (( sScoreInfo * ) ptr1)->metric1 ) return -1;
    if ((( sScoreInfo * ) ptr2)->metric1 > (( sScoreInfo * ) ptr1)->metric1 ) return  1;
    return (( sScoreInfo * ) ptr1)->index - (( sScoreInfo * ) ptr2)->index;
}

static SEG *Algorithm2 ( SEG *segs, int noSegs )
{
    SEG *testSeg = segs;
    int count [3], noScores = 0, rank, i;
    int &lCount = count[0], &sCount = count[1], &rCount = count[2];

    memset ( score, -1, sizeof ( sScoreInfo ) * noAliases );
    score[0].index = 0;

    for ( i = 0; i < noSegs; i++ ) {
        if ( showProgress && (( i & 15 ) == 0 )) ShowProgress ();
        int alias = lineDefAlias [ testSeg->Data.lineDef ].index;
        if ( ! lineChecked [ alias ]) {
            lineChecked [ alias ] = true;
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );

            sScoreInfo *curScore = &score[noScores];
            curScore->invalid = 0;
            memset ( usedSector, 0, sizeof ( UCHAR ) * sectorCount );
            SEG *destSeg = segs;
            for ( int j = 0; j < noSegs; j++, destSeg++ ) {
                switch ( WhichSide ( destSeg )) {
                    case SIDE_LEFT  : lCount++; usedSector [ destSeg->Sector ] |= 0xF0;	break;
                    case SIDE_SPLIT : if ( destSeg->noSplit ) curScore->invalid++;
                                      sCount++; usedSector [ destSeg->Sector ] |= 0xFF;	break;
                    case SIDE_RIGHT : rCount++; usedSector [ destSeg->Sector ] |= 0x0F;	break;
                }
            }
            // Only consider SEG if it is not a boundary line
            if ( lCount + sCount ) {
                int lsCount = 0, rsCount = 0, ssCount = 0;
                for ( int j = 0; j < sectorCount; j++ ) {
                    switch ( usedSector [j] ) {
                        case 0xF0 : lsCount++;	break;
                        case 0xFF : ssCount++;	break;
                        case 0x0F : rsCount++;	break;
                    }
                }
                int temp;
                int product1 = ( long ) ( lCount + sCount ) * ( long ) ( rCount + sCount );
                int product2 = ( long ) ( lsCount + ssCount ) * ( long ) ( rsCount + ssCount );

                curScore->index = i;
                curScore->metric1 = sCount ? product1 / ( X1 ? ( temp = X1 * sCount / X2 ) != 0 ? temp : 1 : 1 ) - ( X3 * sCount + X4 ) * sCount : product1 ? product1 : 0x80000000;
                curScore->metric2 = ssCount ? product2 / ( Y1 ? ( temp = Y1 * ssCount / Y2 ) != 0 ? temp : 1 : 1 ) - ( Y3 * ssCount + Y4 ) * ssCount : product2 ? product2 : 0x80000000;
                noScores++;
            } else {
                // Eliminate outer edges of the map
                *convexPtr++ = alias;
            }
        }
        testSeg++;
    }

    if ( noScores > 1 ) {
        qsort ( score, noScores, sizeof ( sScoreInfo ), sortMetric1 );
        for ( rank = i = 0; i < noScores; i++ ) {
            score[i].total = rank;
            if ( score[i].metric1 != score[i+1].metric1 ) rank++;
        }
        qsort ( score, noScores, sizeof ( sScoreInfo ), sortMetric2 );
        for ( rank = i = 0; i < noScores; i++ ) {
            score[i].total += rank;
            if ( score[i].metric2 != score[i+1].metric2 ) rank++;
        }
        qsort ( score, noScores, sizeof ( sScoreInfo ), sortTotalMetric );
    }
#if defined ( DEBUG )
    if ( noScores && score[0].invalid ) {
        int noBad = 0;
        for ( int i = 0; i < noScores; i++ ) if ( score[i].invalid ) noBad++;
        fprintf ( stderr, "\nWarning: Non-splittable linedefs have been split! (%d/%d)", noBad, noScores );
    }
#endif

    SEG *pSeg = noScores ? &segs [ score[0].index ] : NULL;
    return pSeg;
}

//----------------------------------------------------------------------------
//  ALGORITHM 3: 'ZenNode Lite'
//    This is a modified version of the original algorithm used by ZenNode.
//    It uses the same logic for picking the partition, but only looks at the
//    first 30 segs for a valid partition.  If none is found, the search is
//    continued until one is found or all segs have been searched.
//----------------------------------------------------------------------------

static SEG *Algorithm3 ( SEG *segs, int noSegs )
{
    SEG *pSeg = NULL, *testSeg = segs;
    int count [3];
    int &lCount = count[0], &sCount = count[1], &rCount = count[2];
    // Compute the maximum value maxMetric can possibly reach
    long bestMetric = ( long ) ( noSegs / 2 ) * ( long ) ( noSegs - noSegs / 2 );
    long maxMetric = 0x80000000, maxSplits = 0x7FFFFFFF;

    int i = 0, max = ( noSegs < 30 ) ? noSegs : 30;

retry:

    for ( ; i < max; i++ ) {
        if ( showProgress && (( i & 15 ) == 0 )) ShowProgress ();
        int alias = lineDefAlias [ testSeg->Data.lineDef ].index;
        if ( ! lineChecked [ alias ]) {
            lineChecked [ alias ] = true;
            count [0] = count [1] = count [2] = 0;
            ComputeStaticVariables ( testSeg );
            if ( maxMetric < 0 ) for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
            } else for ( int j = 0; j < noSegs; j++ ) {
                count [ WhichSide ( &segs [j] ) + 1 ]++;
                if ( sCount > maxSplits ) goto next;
            }
            if ( lCount + sCount ) {
                long temp, metric = sCount ? (( long ) lCount * ( long ) rCount ) / ( X1 ? ( temp = X1 * sCount / X2 ) != 0 ? temp : 1 : 1 ) - ( X3 * sCount + X4 ) * sCount : (( long ) lCount * ( long ) rCount );
                if ( ANGLE & 0x3FFF ) metric--;
                if ( metric == bestMetric ) return testSeg;
                if ( metric > maxMetric ) {
                    pSeg = testSeg;
                    maxSplits = sCount;
                    maxMetric = metric;
                }
            } else {
                // Eliminate outer edges of the map from here & down
                *convexPtr++ = alias;
            }
        }
next:
        testSeg++;
    }
    if (( maxMetric == ( long ) 0x80000000 ) && ( max < noSegs )) {
        max += 5;
        if ( max > noSegs ) max = noSegs;
        goto retry;
    }

    return pSeg;
}

//----------------------------------------------------------------------------
//
//  Partition line:
//    DX�x - DY�y + C = 0               � DX  -DY � �-C�
//  rSeg line:                          �         �=�  �
//    dx�x - dy�y + c = 0               � dx  -dy � �-c�
//
//----------------------------------------------------------------------------

static void DivideSeg ( SEG *rSeg, SEG *lSeg )
{
    wVertex *vertS = &newVertices [ rSeg->Data.start ];
    wVertex *vertE = &newVertices [ rSeg->Data.end ];

    // Determine which sided of the partition line the start point is on
    long sideS = ( long ) (( REAL ) ( DX * ( vertS->y - Y )) -
                           ( REAL ) ( DY * ( vertS->x - X )));

    // Minimum precision required to avoid overflow/underflow:
    //   dx, dy  - 16 bits required
    //   c       - 33 bits required
    //   det     - 32 bits required
    //   x, y    - 50 bits required

    REAL dx = ( REAL ) ( vertE->x - vertS->x );
    REAL dy = ( REAL ) ( vertE->y - vertS->y );
    REAL c = ( REAL ) (( long ) vertE->y * ( long ) vertS->x ) - ( REAL ) (( long ) vertE->x * ( long ) vertS->y );

    REAL det = ( dx * DY - dy * DX );
    REAL x = ( C * dx - c * DX ) / det;
    REAL y = ( C * dy - c * DY ) / det;

    int newIndex = AddVertex (( int ) ( x + 0.5 * sgn ( x )), ( int ) ( y + 0.5 * sgn ( y )));

    if (( rSeg->Data.start == newIndex ) || ( rSeg->Data.end == newIndex )) {
        wVertex *vertN = &newVertices [ newIndex ];
        fprintf ( stderr, "\nNODES: End point duplicated in DivideSeg: LineDef #%d", rSeg->Data.lineDef );
        fprintf ( stderr, "\n       Partition: from (%d,%d) to (%d,%d)", X, Y, X + DX, Y + DY );
        fprintf ( stderr, "\n       LineDef: from (%d,%d) to (%d,%d) split at (%d,%d)", vertS->x, vertS->y, vertE->x, vertE->y, vertN->x, vertN->y );
        fprintf ( stderr, "\n" );
        exit ( -1 );
    }

    // Fill in th part of lSeg & rSeg that have changed
    if ( sideS < 0 ) {
        rSeg->Data.end    = ( USHORT ) newIndex;
        lSeg->Data.start  = ( USHORT ) newIndex;
        lSeg->Data.offset += ( USHORT ) hypot (( double ) ( x - vertS->x ), ( double ) ( y - vertS->y ));
    } else {
        rSeg->Data.start  = ( USHORT ) newIndex;
        lSeg->Data.end    = ( USHORT ) newIndex;
        rSeg->Data.offset += ( USHORT ) hypot (( double ) ( x - vertS->x ), ( double ) ( y - vertS->y ));
    }
}

//----------------------------------------------------------------------------
//  Split the list of SEGs in two and adjust each copy to the appropriate
//    values.
//----------------------------------------------------------------------------

static void SplitSegs ( SEG *segs, int noSplits )
{
    segCount += noSplits;
    if ( segCount > maxSegs ) {
        fprintf ( stderr, "\nError: Too many SEGs have been split!\n" );
        exit ( -1 );
    }
    int count = segCount - ( segs - segStart ) - noSplits;
    memmove ( segs + noSplits, segs, count * sizeof ( SEG ));

    for ( int i = 0; i < noSplits; i++ ) {
        DivideSeg ( segs, segs + noSplits );
        segs++;
    }
}

//----------------------------------------------------------------------------
//  Choose a SEG and partition the list of SEGs.  Verify that the partition
//    selected is valid and calculate the necessary data for the NODE.  If
//    no valid partition could be found, return NULL to indicate that the
//    list of SEGs forms a valid SSECTOR.
//----------------------------------------------------------------------------

static bool PartitionNode ( NODE *node, SEG *rSegs, int noSegs, int *noLeft, int *noRight )
{
    int noSplits;

    if ( ! ChoosePartition ( rSegs, noSegs, noLeft, noRight, &noSplits )) {

        if ( uniqueSubsectors ) {
            memset ( usedSector, false, sizeof ( UCHAR ) * sectorCount );
            int i;
            for ( i = 0; i < noSegs; i++ ) {
                usedSector [ rSegs[i].Sector ] = true;
            }
            int noSectors = 0;
            for ( i = 0; i < sectorCount; i++ ) {
                if ( usedSector [i] ) noSectors++;
            }
            if ( noSectors > 1 ) for ( i = 0; noSectors && ( i < sectorCount ); i++ ) {
                if ( usedSector [i] ) {
                    if ( keepUnique [i] ) goto NonUnique;
                    noSectors--;
                }
            }
        }

        // Splits may have 'upset' the lineDef ordering - some special effects
        //   assume the SEGS appear in the same order as the lineDefs
        if ( noSegs > 1 ) {
            qsort ( rSegs, noSegs, sizeof ( SEG ), SortByLineDef );
        }
        return false;

NonUnique:

#if defined ( DEBUG_TRACE )
        tracePtr += sprintf ( tracePtr, "ARBITRARY " );
#endif
        ComputeStaticVariables ( rSegs );
        SortSectors ( rSegs, noSegs, noLeft, noRight );

    } else if ( noSplits ) {

        SplitSegs ( &rSegs [ *noRight ], noSplits );
        *noLeft  += noSplits;
        *noRight += noSplits;

    }

    node->Data.x  = ( SHORT ) X;
    node->Data.y  = ( SHORT ) Y;
    node->Data.dx = ( SHORT ) DX;
    node->Data.dy = ( SHORT ) DY;

    SEG *lSegs = &rSegs [ *noRight ];
    FindBounds ( &node->Data.side[0], rSegs, *noRight );
    FindBounds ( &node->Data.side[1], lSegs, *noLeft );

    return true;
}

//----------------------------------------------------------------------------
//  Recursively create the actual NODEs.  The given list of SEGs is analyzed
//    and a partition is chosen.  If no partition can be found, a leaf NODE
//    is created.  Otherwise, the right and left SEGs are analyzed.  Features:
//     A list of 'convex' aliases is maintained.  These are lines that border
//      the list of SEGs and can never be partitions.  A line is marked as
//      convex for this and all children, and unmarked before returing.
//     Similarly, the alias chosen as the partition is marked as convex
//      since it will be convex for all children.
//----------------------------------------------------------------------------
static NODE *CreateNode ( NODE *prev, SEG *rSegs, int &noSegs, SEG *&nextSeg )
{
    if ( nodesLeft-- == 0 ) {
        fprintf ( stderr, "ERROR: nodePool exhausted\n" );
        exit ( -1 );
    }
    NODE *node = nodePool++;
    node->Next = NULL;
    if ( prev ) prev->Next = node;

    int noLeft, noRight;
    int *cptr = convexPtr;

#if defined ( DEBUG_TRACE )
    noCalls++;
    if ( noCalls == tgtCall ) tgtCall = -1;
    tracePtr = traceBuffer;
    tracePtr += sprintf ( tracePtr, "%*.*s%-4d - ", depth*2, depth*2, "", noSegs );
#endif

    if (( noSegs <= 1 ) ||
        ! PartitionNode ( node, rSegs, noSegs, &noLeft, &noRight )) {

#if defined ( DEBUG_TRACE )
    tracePtr += sprintf ( tracePtr, "LEAF" );
    if ( cptr != convexPtr ) {
        tracePtr += sprintf ( tracePtr, " Convex:" );
        for ( int *tempPtr = cptr; tempPtr != convexPtr; tempPtr++ ) {
            tracePtr += sprintf ( tracePtr, " %d%s", *tempPtr, lineUsed [ *tempPtr ] ? "*" : "" );
        }
    }
    gLogger->Log ( 0, traceBuffer );
#endif
        convexPtr = cptr;
        if ( nodeStart == NULL ) nodeStart = node;
        node->id = ( USHORT ) ( 0x8000 | CreateSSector ( noSegs, rSegs ));
        if ( showProgress ) ShowDone ();
        nextSeg = &rSegs [ noSegs ];
        return node;
    }

    int alias = currentAlias->index;
#if defined ( DEBUG_TRACE )
    tracePtr += sprintf ( tracePtr, "( %4d | %4d | %4d ) - part: %d%s", noLeft, ( noLeft + noRight - noSegs ), noRight, alias, lineUsed [ alias ] ? "*" : "" );
    if ( cptr != convexPtr ) {
        tracePtr += sprintf ( tracePtr, " Convex:" );
        for ( int *tempPtr = cptr; tempPtr != convexPtr; tempPtr++ ) {
            tracePtr += sprintf ( tracePtr, " %d%s", *tempPtr, lineUsed [ *tempPtr ] ? "*" : "" );
        }
    }
    gLogger->Log ( 0, traceBuffer );
#endif
    lineUsed [ alias ] = true;
    for ( int *tempPtr = cptr; tempPtr != convexPtr; tempPtr++ ) {
        lineUsed [ *tempPtr ] = true;
    }

#if defined ( DEBUG_TRACE )
    depth++;
    *partPtr++ = alias;
#endif

    SEG *lSegs;

    if ( showProgress ) GoRight ();
    NODE *rNode = CreateNode ( prev, rSegs, noRight, lSegs );
    node->Data.child[0] = rNode->id;

    if ( showProgress ) GoLeft ();
    NODE *lNode = CreateNode ( rNode, lSegs, noLeft, lSegs );
    node->Data.child[1] = lNode->id;

    while ( convexPtr != cptr ) lineUsed [ *--convexPtr ] = false;
    lineUsed [ alias ] = false;

#if defined ( DEBUG_TRACE )
    depth--;
    *partPtr--;
#endif

    if ( showProgress ) Backup ();

    lNode->Next = node;
    node->id = ( USHORT ) nodeCount++;

    if ( showProgress ) ShowDone ();

    noSegs = noLeft + noRight;
    nextSeg = &rSegs [ noSegs ];

    return node;
}

wVertex  *GetVertices ()
{
    wVertex *vert = new wVertex [ noVertices ];
    memcpy ( vert, newVertices, sizeof ( wVertex ) * noVertices );
    return vert;
}

wSSector *GetSSectors ( wSSector *first )
{
    wSSector *ssector = new wSSector [ ssectorCount ];
    memcpy ( ssector, first, sizeof ( wSSector ) * ssectorCount );
    return ssector;
}

wSegs *GetSegs ()
{
    wSegs *segs = new wSegs [ segCount ];
    for ( int i = 0; i < segCount; i++ ) {
        segs [i] = segStart [i].Data;
    }
    delete [] segStart;
    return segs;
}

wNode *GetNodes ()
{
    wNode *nodes = new wNode [ nodeCount ];
    for ( int i = 0; i < nodeCount; i++ ) {
        while ( nodeStart->id & 0x8000 ) {
            nodeStart = nodeStart->Next;
        }
        nodes [i] = nodeStart->Data;
        nodeStart = nodeStart->Next;
    }
    return nodes;
}

//----------------------------------------------------------------------------
//  Wrapper function that calls all the necessary functions to prepare the
//    BSP tree and insert the new data into the level.  All screen I/O is
//    done in this routine ( with the exception of progress indication ).
//----------------------------------------------------------------------------

void CreateNODES ( DoomLevel *level, sBSPOptions *options )
{
    if ( X2 == 0 ) X2 = 1;
    if ( Y2 == 0 ) Y2 = 1;

    showProgress = options->showProgress;
    uniqueSubsectors = options->keepUnique ? true : false;
    PartitionFunction = Algorithm1;
    if ( options->algorithm == 2 ) PartitionFunction = Algorithm2;
    if ( options->algorithm == 3 ) PartitionFunction = Algorithm3;
    	    				  
    fixfloat ();

    nodeStart = NULL;
    segStart = NULL;
    nodeCount = 0;
    ssectorCount = 0;

    level->NewSegs ( 0, NULL );
    level->TrimVertices ();
    level->PackVertices ();

    noVertices = level->VertexCount ();
    sectorCount = level->SectorCount ();
    usedSector = new UCHAR [ sectorCount ];
    keepUnique = new bool [ sectorCount ];
    if ( options->keepUnique ) {
        memcpy ( keepUnique, options->keepUnique, sizeof ( bool ) * sectorCount );
    } else {
        memset ( keepUnique, true, sizeof ( bool ) * sectorCount );
    }
    maxVertices = ( int ) ( noVertices * FACTOR_VERTEX );
    newVertices = new wVertex [ maxVertices ];
    memcpy ( newVertices, level->GetVertices (), sizeof ( wVertex ) * noVertices );

    Status ( "Creating SEGS ... " );
    segStart = CreateSegs ( level, options );

    if ( options->algorithm != 3 ) {

        Status ( "Getting LineDef Aliases ... " );
        SEG **aliasList = GetLineDefAliases ( level );

        lineChecked = new bool [ noAliases ];
        lineUsed = new bool [ noAliases ];
        memset ( lineUsed, false, sizeof ( bool ) * noAliases );

        Status ( "Getting Sector Bounds ... " );
        wBound *bound = GetSectorBounds ( level );
        sSectorInfo *sectInfo = GetSectorInfo ( sectorCount, bound );

        Status ( "Creating Side Info ... " );
        CreateSideInfo ( level, bound, sectInfo, aliasList );

        // Make sure every seg is on it's own right side!
        for ( int i = 0; i < segCount; i++ ) {
            ComputeStaticVariables ( &segStart[i] );
            int side = WhichSide ( &segStart[i] );
            if ( side == SIDE_LEFT ) {
                int alias = lineDefAlias [ segStart[i].Data.lineDef ].index;
                sideInfo [alias][ segStart[i].Sector ] = SIDE_SPLIT;
            }
        }

        delete [] ( char * ) sectInfo;
        delete [] bound;
        delete [] aliasList;

    } else {

        noAliases = level->LineDefCount ();
        lineDefAlias = new sAlias [ noAliases ];
        memset ( lineDefAlias, 0, sizeof ( sAlias ) * noAliases );
        int i;
        for ( i = 0; i < noAliases; i++ ) {
            lineDefAlias [i].index = i;
        }

        // Set all sideInfo entries to SIDE_SPLIT
        sideInfo = new char * [ noAliases ];
        sideInfo[0] = new char [ segCount ];
        for ( i = 1; i < noAliases; i++ ) sideInfo [i] = sideInfo[0];
        for ( i = 0; i < segCount; i++ ) sideInfo [0][i] = SIDE_SPLIT;

        lineChecked = new bool [ noAliases ];
        lineUsed = new bool [ noAliases ];
        memset ( lineUsed, false, noAliases );

    }

    score = ( options->algorithm == 2 ) ? new sScoreInfo [ noAliases ] : NULL; 
    convexList = new int [ noAliases ];	  
    convexPtr = convexList;

#if defined ( DEBUG )
    gLogger->Log ( 0, "   Algorithm: %d %s", options->algorithm, options->algorithm == 1 ? "Classic" : options->algorithm == 2 ? "Quality" : "Lite" );
    gLogger->Log ( 0, "   noSectors: %5d", level->SectorCount ());
    gLogger->Log ( 0, "  noLineDefs: %5d", level->LineDefCount ());
    gLogger->Log ( 0, "   noAliases: %5d", noAliases );
    memset ( convexList, -1, sizeof ( int ) * noAliases );
#endif

#if defined ( DEBUG_TRACE )
    depth = 0;
    noCalls = 0;
    partList = new int [ noAliases ];
    partPtr = partList;
#endif

    Status ( "Creating NODES ... " );
    int noSegs = segCount;
    SEG *endSeg;
    NODE *firstNode = nodePool = new NODE [ nodesLeft = ( int ) ( FACTOR_NODE * level->LineDefCount ()) ];
    wSSector *firstSSector = ssectorPool = new wSSector [ ssectorsLeft = ( int ) ( FACTOR_SSECTOR * level->SectorCount ()) ];
    CreateNode ( NULL, segStart, noSegs, endSeg );    		 		    

#if defined ( DEBUG_TRACE )
    delete [] partList;
#endif

#if defined ( DEBUG )
    convexPtr = convexList + noAliases - 1;
    while ( *convexPtr == -1 ) convexPtr--;
    gLogger->Log ( 0, "Convex Level: %5d", convexPtr - convexList );
#endif

    delete [] convexList;
    if ( score ) delete [] score;
    restorefloat ();

    // Clean Up temporary buffers
    Status ( "Cleaning up ... " );
    delete [] sideInfo;
    delete [] lineDefAlias;
    delete [] lineChecked;
    delete [] lineUsed;
    delete [] keepUnique;
    delete [] usedSector;

    sideInfo = NULL;

#if defined ( DEBUG )
    int oldSegs = 0;
    const wLineDef *lineDef = level->GetLineDefs ();
    for ( int i = 0; i < level->LineDefCount (); i++ ) {
        if ( lineDef[i].sideDef[0] != NO_SIDEDEF ) oldSegs++;
        if ( lineDef[i].sideDef[1] != NO_SIDEDEF ) oldSegs++;
    }

    float xVertex, xSegs, xNode, xSSector;
    gLogger->Log ( 0, "  Final SEGS: %5d ( %f )", segCount, xSegs = ( float ) segCount / oldSegs );
    gLogger->Log ( 0, "    SSECTORS: %5d ( %f )", ssectorCount, xSSector = ( float ) ssectorCount / level->SectorCount ());
    gLogger->Log ( 0, "  noVertices: %5d ( %f )", noVertices, xVertex = ( float ) noVertices / level->VertexCount ());
    gLogger->Log ( 0, "     # Nodes: %5d ( %f )", nodeCount + ssectorCount, xNode = ( float ) ( nodeCount + ssectorCount ) / level->LineDefCount ());

    if ( xVertex > worstVertex ) worstVertex = xVertex;
    if ( xSegs > worstSegs ) worstSegs = xSegs;
    if ( xNode > worstNode ) worstNode = xNode;
    if ( xSSector > worstSSector ) worstSSector = xSSector;

#endif

    level->NewVertices ( noVertices, GetVertices ());
    level->NewSegs ( segCount, GetSegs ());
    level->NewSubSectors ( ssectorCount, GetSSectors ( firstSSector ));
    level->NewNodes ( nodeCount, GetNodes ());

    delete [] newVertices;
    delete [] firstSSector;
    delete [] firstNode;
    delete [] tempSeg;
}