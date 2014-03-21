///////////////////////////////////////////////////////////////////////////////
// 
// OpenFTA - Fault Tree Analysis
// Copyright (C) 2005 FSC Limited
// 
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your 
// option) any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for 
// more details.
//
// You should have received a copy of the GNU General Public License along 
// with this program; if not, write to the Free Software Foundation, Inc., 
// 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//
// To contact FSC, please send an email to support@fsc.co.uk or write to 
// FSC Ltd., Cardiff Business Technology Centre, Senghenydd Road, Cardiff,
// CF24 4AY.
//
///////////////////////////////////////////////////////////////////////////////

/***************************************************************
 Module : Record_Io
 Author : FSC Limited
 Date   : 22/10/96

 SccsId : @(#)Record_Io.h	1.1 10/22/96

 Origin : FTA project,
          FSC Limited Private Venture.
 
 This module implements a more modular approach to reading a 
 file of records with delimited fields.

 This module only implements a few of the interfaces provided,
 and performs no 'Output' although the name implies it does.
 This is a future enhancement.
***************************************************************/
 
#ifndef record_io
#define record_io

#include <stdio.h>
#include "boolean.h"

#define DELIMITER ';'

/*---------------------------------------------------------------
 Routine : GetStringField
 Purpose : Reads the next string field from the file.
 If no field exists, or EOF is encountered, returns NULL, else
 returns a pointer to a the string.

 Leaves the file pointer at the beginning of the next field (i.e.,
 skips all delimiters).
---------------------------------------------------------------*/
extern char *
  GetStringField(
    FILE *FromFile );

/*---------------------------------------------------------------
 Routine : GetChar
 Purpose : Reads the next character from the file.
 If no character exists, or EOF is encountered, returns NULL, else
 returns a pointer to a character.
---------------------------------------------------------------*/
extern char
  GetChar(
    FILE *FromFile,
    BOOL IgnoreWhiteSpace );

extern int *
  get_int(
    FILE *fp );

/*---------------------------------------------------------------
 Routine : GetFloatField
 Purpose : Reads the next float field from the file.
 If no field exists, or EOF is encountered, then Success will be returned FALSE, else
 returns a pointer to the float and Success will be returned TRUE.

 Leaves the file pointer at the beginning of the next field (i.e.,
 skips all delimiters).
---------------------------------------------------------------*/
extern float
  GetFloatField(
    FILE *FromFile,
	BOOL *Success);

extern void
  skip_field(
    FILE *fp );

extern void
  skip_entry(
    FILE *fp );

#endif /* ndef record_io */
