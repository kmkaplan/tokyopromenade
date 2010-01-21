/*************************************************************************************************
 * Scripting language extension of Tokyo Promenade
 *                                                      Copyright (C) 2008-2010 Mikio Hirabayashi
 * This file is part of Tokyo Promenade.
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either version
 * 3 of the License, or any later version.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *************************************************************************************************/


#ifndef _SCREXT_H                        // duplication check
#define _SCREXT_H



/*************************************************************************************************
 * configuration
 *************************************************************************************************/


#include "common.h"


#if defined(_MYLUA)
#define TTLUAEXT
#else
#define TTNOEXT
#endif



/*************************************************************************************************
 * pseudo API
 *************************************************************************************************/


/* Initialize the scripting language extension.
   The return value is the scripting object or `NULL' on failure. */
void *scrextnew(void);


/* Destroy the scripting language extension.
   `scr' specifies the scripting object. */
void scrextdel(void *scr);


/* Load a script file of the scripting language extension.
   `scr' specifies the scripting object.
   `path' specifies the path of the initilizing script.
   If successful, the return value is true, else, it is false. */
bool scrextload(void *scr, const char *path);


/* Check a function is implemented by the scripting language extension.
   `scr' specifies the scripting object.
   `name' specifies the name of the function to be called.
   The return value is true if the function is implemented or false if not. */
bool scrextcheckfunc(void *scr, const char *name);


/* Call a function of the scripting language extension.
   `scr' specifies the scripting object.
   `name' specifies the name of the function to be called.
   `param' specifies the paramter string.
   The return value is the string of the return value or `NULL' on error.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call when it is no longer in use. */
char *scrextcallfunc(void *scr, const char *name, const char *param);


/* Get the error message of the last operation of scripting language extension.
   `scr' specifies the scripting object.
   The return value is the error message or `NULL' if there is no error. */
const char *screxterrmsg(void *scr);


/* Set a table variable of scripting language extension.
   `scr' specifies the scripting object.
   `name' specifies the name of the variable.
   `param' specifies the records of the table. */
void scrextsetmapvar(void *scr, const char *name, const TCMAP *params);



#endif                                   // duplication check


// END OF FILE
