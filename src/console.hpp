//
// Copyright (c) 1998-2004 Marc Rousseau
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//

#ifndef CONSOLE_HPP_
#define CONSOLE_HPP_

#if defined ( __OS2__ ) || defined ( __WIN32__ )

#if defined ( __GNUC__ ) || defined ( __INTEL_COMPILER )
#define cprintf _cprintf
#endif

#elif defined ( __GNUC__ ) || defined ( __INTEL_COMPILER )

#define stricmp strcasecmp

extern int cprintf ( const char *, ... );

#endif

void SaveConsoleSettings ();
void RestoreConsoleSettings ();

void HideCursor ();
void ShowCursor ();

int  GetKey ();
bool KeyPressed ();

UINT32 CurrentTime ();

void ClearScreen ();

extern UINT32 startX, startY;

void GetXY ( UINT32 *x, UINT32 *y );
void GotoXY ( UINT32 x, UINT32 y );
void MoveUp ( int delta );
void MoveDown ( int delta );
void PutXY ( UINT32 x, UINT32 y, char *ptr, int length );
void Put ( char *ptr, int length );

// ----- External Functions Required by ZenNode -----

void Status ( const char *message );
void GoRight ();
void GoLeft ();
void Backup ();
void ShowDone ();
void ShowProgress ();

#endif

