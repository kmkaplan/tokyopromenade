/*************************************************************************************************
 * The utility API of Tokyo Promenade
 *                                                      Copyright (C) 2008-2009 Mikio Hirabayashi
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


#ifndef _COMMON_H                        /* duplication check */
#define _COMMON_H

#if defined(__cplusplus)
#define __COMMON_CLINKAGEBEGIN extern "C" {
#define __COMMON_CLINKAGEEND }
#else
#define __COMMON_CLINKAGEBEGIN
#define __COMMON_CLINKAGEEND
#endif
__COMMON_CLINKAGEBEGIN


#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <limits.h>
#include <math.h>
#include <assert.h>
#include <tcutil.h>
#include <tchdb.h>
#include <tcbdb.h>
#include <tcfdb.h>
#include <tctdb.h>
#include <tcadb.h>



/*************************************************************************************************
 * API
 *************************************************************************************************/


#define TPVERSION      "0.9.0"

#define TUNEBNUM       131071            // bnum tuning parameter
#define TUNEAPOW       6                 // apow tuning parameter
#define TUNEFPOW       10                // fpow tuning parameter
#define IOMAXSIZ       (32<<20)          // maximum size of I/O data
#define IOBUFSIZ       65536             // size of an I/O buffer
#define LINEBUFSIZ     1024              // size of a buffer for each line
#define NUMBUFSIZ      64                // size of a buffer for number
#define TINYBNUM       31                // bucket number of a tiny map
#define HEADLVMAX      6                 // maximum level of header

enum {                                   // enumeration for external data formats
  FMTWIKI,                               // Wiki
  FMTTEXT,                               // plain text
  FMTHTML                                // HTML
};


/* Load a Wiki string.
   `cols' specifies a map object containing columns.
   `str' specifies the Wiki string. */
void wikiload(TCMAP *cols, const char *str);


/* Dump the attributes and the body text of an article into a Wiki string.
   `rbuf' specifies the result buffer.
   `cols' specifies a map object containing columns. */
void wikidump(TCXSTR *rbuf, TCMAP *cols);


/* Dump the attributes and the body text of an article into a plain text string.
   `rbuf' specifies the result buffer.
   `cols' specifies a map object containing columns. */
void wikidumptext(TCXSTR *rbuf, TCMAP *cols);


/* Convert a Wiki string into a plain text string.
   `rbuf' specifies the result buffer.
   `str' specifies the Wiki string. */
void wikitotext(TCXSTR *rbuf, const char *str);


/* Add an inline Wiki string into plain text.
   `rbuf' specifies the result buffer.
   `line' specifies the inline Wiki string. */
void wikitotextinline(TCXSTR *rbuf, const char *line);


/* Dump the attributes and the body text of an article into an HTML string.
   `rbuf' specifies the result buffer.
   `cols' specifies a map object containing columns.
   `buri' specifies the base URI.
   `bhl' specifies the base header level. */
void wikidumphtml(TCXSTR *rbuf, TCMAP *cols, const char *buri, int bhl);


/* Convert a Wiki string into an HTML string.
   `rbuf' specifies the result buffer.
   `str' specifies the Wiki string.
   `id' specifies the ID string of the article.  If it is `NULL', the ID is not expressed.
   `buri' specifies the base URI.
   `bhl' specifies the base header level. */
void wikitohtml(TCXSTR *rbuf, const char *str, const char *id, const char *buri, int bhl);


/* Add an inline Wiki string into HTML.
   `rbuf' specifies the result buffer.
   `line' specifies the inline Wiki string.
   `buri' specifies the base URI. */
void wikitohtmlinline(TCXSTR *rbuf, const char *line, const char *buri);


/* Simplify a date string.
   `str' specifies the date string.
   The return value is the date string itself. */
char *datestrsimple(char *str);


/* Get the MIME type of a file.
   `name' specifies the name of the file.
   The return value is the MIME type of the file. */
const char *mimetype(const char *name);


/* Get the human readable name of the MIME type.
   `type' specifies the MIME type.
   The return value is the name of the MIME type. */
const char *mimetypename(const char *type);


/* Store an article into the database.
   `tdb' specifies the database object.
   `id' specifies the ID number of the article.  If it is not more than 0, the auto-increment ID
   is assigned.
   `cols' specifies a map object containing columns.
   If successful, the return value is true, else, it is false. */
bool dbputart(TCTDB *tdb, int64_t id, TCMAP *cols);


/* Remove an article from the database.
   `tdb' specifies the database object.
   `id' specifies the ID number of the article.
   If successful, the return value is true, else, it is false. */
bool dboutart(TCTDB *tdb, int64_t id);


/* Retrieve an article of the database.
   `tdb' specifies the database object.
   `id' specifies the ID number.
   If successful, the return value is a map object of the columns.  `NULL' is returned if no
   article corresponds.
   Because the object of the return value is created with the function `tcmapnew', it should be
   deleted with the function `tcmapdel' when it is no longer in use. */
TCMAP *dbgetart(TCTDB *tdb, int64_t id);


/* Generate the hash value of a user password.
   `pass' specifies the password string.
   `sal' specifies the salt string.
   `buf' specifies the result buffer whose size should be equal to or more than 48 bytes. */
void passwordhash(const char *pass, const char *salt, char *buf);


/* Check whether the name of a user is valid.
   `name' specifies the name of the user.
   The return value is true if the name is valid, else, it is false. */
bool checkusername(const char *name);


/* Check whether an article is frozen.
   `cols' specifies a map object containing columns.
   The return value is true if the article is frozen, else, it is false. */
bool checkfrozen(TCMAP *cols);



#endif                                   /* duplication check */


/* END OF FILE */
