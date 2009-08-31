/*************************************************************************************************
 * The command line utility of Tokyo Promenade
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


#include "common.h"


/* global variables */
const char *g_progname;                  // program name


/* function prototypes */
int main(int argc, char **argv);
static void usage(void);
static void eprintf(const char *format, ...);
static void printdberr(TCTDB *tdb);
static int runcreate(int argc, char **argv);
static int runimport(int argc, char **argv);
static int runconvert(int argc, char **argv);
static int runpasswd(int argc, char **argv);
static int runversion(int argc, char **argv);
static int proccreate(const char *dbpath, int scale, bool fts);
static int procimport(const char *dbpath, TCLIST *files, TCLIST *sufs);
static int procconvert(const char *ibuf, int isiz, int fmt, const char *buri, bool page);
static int procpasswd(const char *name, const char *pass, const char *salt, const char *info);
static int procversion(void);


/* main routine */
int main(int argc, char **argv){
  g_progname = argv[0];
  if(argc < 2) usage();
  int rv = 0;
  if(!strcmp(argv[1], "create")){
    rv = runcreate(argc, argv);
  } else if(!strcmp(argv[1], "import")){
    rv = runimport(argc, argv);
  } else if(!strcmp(argv[1], "convert")){
    rv = runconvert(argc, argv);
  } else if(!strcmp(argv[1], "passwd")){
    rv = runpasswd(argc, argv);
  } else if(!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")){
    rv = runversion(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
static void usage(void){
  fprintf(stderr, "%s: the command line utility of Tokyo Promenade\n", g_progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s create [-fts] dbpath [scale]\n", g_progname);
  fprintf(stderr, "  %s import [-suf str] dbpath file ... \n", g_progname);
  fprintf(stderr, "  %s convert [-fw|-ft] [-buri str] [-page] [file]\n", g_progname);
  fprintf(stderr, "  %s passwd [-salt str] name pass\n", g_progname);
  fprintf(stderr, "  %s version\n", g_progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* print formatted error string */
static void eprintf(const char *format, ...){
  va_list ap;
  va_start(ap, format);
  fprintf(stderr, "%s: ", g_progname);
  vfprintf(stderr, format, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}


/* print error information */
static void printdberr(TCTDB *tdb){
  const char *path = tctdbpath(tdb);
  int ecode = tctdbecode(tdb);
  eprintf("%s: %d: %s\n", path ? path : "-", ecode, tctdberrmsg(ecode));
}


/* parse arguments of create command */
static int runcreate(int argc, char **argv){
  char *dbpath = NULL;
  char *sstr = NULL;
  bool fts = false;
  for(int i = 2; i < argc; i++){
    if(!dbpath && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-fts")){
        fts = true;
      } else {
        usage();
      }
    } else if(!dbpath){
      dbpath = argv[i];
    } else if(!sstr){
      sstr = argv[i];
    } else {
      usage();
    }
  }
  if(!dbpath) usage();
  int scale = sstr ? tcatoix(sstr) : -1;
  int rv = proccreate(dbpath, scale, fts);
  return rv;
}


/* parse arguments of import command */
static int runimport(int argc, char **argv){
  char *dbpath = NULL;
  TCLIST *files = tcmpoollistnew(tcmpoolglobal());
  TCLIST *sufs = tcmpoollistnew(tcmpoolglobal());
  for(int i = 2; i < argc; i++){
    if(!dbpath && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-suf")){
        if(++i >= argc) usage();
        tclistpush2(sufs, argv[i]);
      } else {
        usage();
      }
    } else if(!dbpath){
      dbpath = argv[i];
    } else {
      tclistpush2(files, argv[i]);
    }
  }
  if(!dbpath || tclistnum(files) < 1) usage();
  tclistpush2(sufs, ".tpw");
  int rv = procimport(dbpath, files, sufs);
  return rv;
}


/* parse arguments of convert command */
static int runconvert(int argc, char **argv){
  char *path = NULL;
  int fmt = FMTHTML;
  char *buri = "promenade.cgi";
  bool page = false;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-fw")){
        fmt = FMTWIKI;
      } else if(!strcmp(argv[i], "-ft")){
        fmt = FMTTEXT;
      } else if(!strcmp(argv[i], "-buri")){
        if(++i >= argc) usage();
        buri = argv[i];
      } else if(!strcmp(argv[i], "-page")){
        page = true;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else {
      usage();
    }
  }
  char *ibuf;
  int isiz;
  if(path && path[0] == '@'){
    isiz = strlen(path) - 1;
    ibuf = tcmemdup(path + 1, isiz);
  } else {
    ibuf = tcreadfile(path, IOMAXSIZ, &isiz);
  }
  if(!ibuf){
    eprintf("%s: cannot open", path ? path : "(stdin)");
    return 1;
  }
  int rv = procconvert(ibuf, isiz, fmt, buri, page);
  if(path && path[0] == '@') printf("\n");
  tcfree(ibuf);
  return rv;
}


/* parse arguments of passwd command */
static int runpasswd(int argc, char **argv){
  char *name = NULL;
  char *pass = NULL;
  char *salt = "";
  char *info = "";
  for(int i = 2; i < argc; i++){
    if(!name && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-salt")){
        if(++i >= argc) usage();
        salt = argv[i];
      } else if(!strcmp(argv[i], "-info")){
        if(++i >= argc) usage();
        info = argv[i];
      } else {
        usage();
      }
    } else if(!name){
      name = argv[i];
    } else if(!pass){
      pass = argv[i];
    } else {
      usage();
    }
  }
  if(!name || !pass) usage();
  int rv = procpasswd(name, pass, salt, info);
  return rv;
}


/* parse arguments of version command */
static int runversion(int argc, char **argv){
  int rv = procversion();
  return rv;
}


/* perform create command */
static int proccreate(const char *dbpath, int scale, bool fts){
  TCTDB *tdb = tctdbnew();
  int bnum = (scale > 0) ? scale * 2 : TUNEBNUM;
  if(!tctdbtune(tdb, bnum, TUNEAPOW, TUNEFPOW, 0)){
    printdberr(tdb);
    tctdbdel(tdb);
    return 1;
  }
  if(!tctdbopen(tdb, dbpath, TDBOWRITER | TDBOCREAT)){
    printdberr(tdb);
    tctdbdel(tdb);
    return 1;
  }
  bool err = false;
  if(!tctdbsetindex(tdb, "name", TDBITLEXICAL | TDBITKEEP) && tctdbecode(tdb) != TCEKEEP){
    printdberr(tdb);
    err = true;
  }
  if(!tctdbsetindex(tdb, "cdate", TDBITDECIMAL | TDBITKEEP) && tctdbecode(tdb) != TCEKEEP){
    printdberr(tdb);
    err = true;
  }
  if(!tctdbsetindex(tdb, "mdate", TDBITDECIMAL | TDBITKEEP) && tctdbecode(tdb) != TCEKEEP){
    printdberr(tdb);
    err = true;
  }
  if(fts && !tctdbsetindex(tdb, "text", TDBITQGRAM | TDBITKEEP) && tctdbecode(tdb) != TCEKEEP){
    printdberr(tdb);
    err = true;
  }
  if(!tctdbclose(tdb)){
    printdberr(tdb);
    err = true;
  }
  tctdbdel(tdb);
  return err ? 1 : 0;
}


/* perform import command */
static int procimport(const char *dbpath, TCLIST *files, TCLIST *sufs){
  TCTDB *tdb = tctdbnew();
  if(!tctdbtune(tdb, TUNEBNUM, TUNEAPOW, TUNEFPOW, 0)){
    printdberr(tdb);
    tctdbdel(tdb);
    return 1;
  }
  if(!tctdbopen(tdb, dbpath, TDBOWRITER | TDBOCREAT)){
    printdberr(tdb);
    tctdbdel(tdb);
    return 1;
  }
  bool err = false;
  if(!tctdbsetindex(tdb, "name", TDBITLEXICAL | TDBITKEEP) && tctdbecode(tdb) != TCEKEEP){
    printdberr(tdb);
    err = true;
  }
  if(!tctdbsetindex(tdb, "cdate", TDBITDECIMAL | TDBITKEEP) && tctdbecode(tdb) != TCEKEEP){
    printdberr(tdb);
    err = true;
  }
  if(!tctdbsetindex(tdb, "mdate", TDBITDECIMAL | TDBITKEEP) && tctdbecode(tdb) != TCEKEEP){
    printdberr(tdb);
    err = true;
  }
  tclistinvert(files);
  char *fpath;
  while((fpath = tclistpop2(files)) != NULL){
    TCLIST *cfiles = tcreaddir(fpath);
    if(cfiles){
      tclistsort(cfiles);
      for(int i = tclistnum(cfiles) - 1; i >= 0; i--){
        const char *cfile = tclistval2(cfiles, i);
        bool hit = false;
        for(int j = 0; j < tclistnum(sufs); j++){
          if(tcstribwm(cfile, tclistval2(sufs, j))){
            hit = true;
            break;
          }
        }
        if(!hit) continue;
        char *lpath = tcsprintf("%s/%s", fpath, cfile);
        tclistpush2(files, lpath);
        tcfree(lpath);
      }
      tclistdel(cfiles);
    } else {
      int isiz;
      char *ibuf = tcreadfile(fpath, IOMAXSIZ, &isiz);
      if(ibuf){
        TCMAP *cols = tcmapnew2(TINYBNUM);
        wikiload(cols, ibuf);
        const char *name = tcmapget2(cols, "name");
        if(name && *name != '\0'){
          int64_t id = tcatoi(tcmapget4(cols, "id", ""));
          if(dbputart(tdb, id, cols)){
            id = tcatoi(tcmapget4(cols, "id", ""));
            printf("%s: stored: id=%lld name=%s\n", fpath, (long long)id, name);
          } else {
            printdberr(tdb);
            err = true;
          }
        } else {
          printf("%s: ignored because there is no name\n", fpath);
        }
        tcmapdel(cols);
        tcfree(ibuf);
      }
    }
    tcfree(fpath);
  }
  if(!tctdbclose(tdb)){
    printdberr(tdb);
    err = true;
  }
  tctdbdel(tdb);
  return err ? 1 : 0;
}


/* perform convert command */
static int procconvert(const char *ibuf, int isiz, int fmt, const char *buri, bool page){
  TCMAP *cols = tcmapnew2(TINYBNUM);
  wikiload(cols, ibuf);
  if(fmt == FMTWIKI){
    TCXSTR *rbuf = tcxstrnew3(IOBUFSIZ);
    wikidump(rbuf, cols);
    fwrite(tcxstrptr(rbuf), 1, tcxstrsize(rbuf), stdout);
    tcxstrdel(rbuf);
  } else if(fmt == FMTTEXT){
    TCXSTR *rbuf = tcxstrnew3(IOBUFSIZ);
    if(page)
      tcxstrprintf(rbuf, "------------------------ Tokyo Promenade ------------------------\n");
    wikidumptext(rbuf, cols);
    if(page)
      tcxstrprintf(rbuf, "-----------------------------------------------------------------\n");
    fwrite(tcxstrptr(rbuf), 1, tcxstrsize(rbuf), stdout);
    tcxstrdel(rbuf);
  } else if(fmt == FMTHTML){
    TCXSTR *rbuf = tcxstrnew3(IOBUFSIZ);
    if(page){
      const char *name = tcmapget2(cols, "name");
      tcxstrprintf(rbuf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
      tcxstrprintf(rbuf, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\""
                   " \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n");
      tcxstrprintf(rbuf, "<html xmlns=\"http://www.w3.org/1999/xhtml\""
                   " xml:lang=\"en\" lang=\"en\">\n");
      tcxstrprintf(rbuf, "<head>\n");
      tcxstrprintf(rbuf, "<meta http-equiv=\"Content-Type\""
                   " content=\"text/html; charset=UTF-8\" />\n");
      tcxstrprintf(rbuf, "<link rel=\"contents\" href=\"%@\" />\n", buri);
      tcxstrprintf(rbuf, "<title>%@</title>\n", name ? name : "Tokyo Promenade");
      tcxstrprintf(rbuf, "</head>\n");
      tcxstrprintf(rbuf, "<body>\n");
    }
    wikidumphtml(rbuf, cols, buri, 0);
    if(page){
      tcxstrprintf(rbuf, "</body>\n");
      tcxstrprintf(rbuf, "</html>\n");
    }
    fwrite(tcxstrptr(rbuf), 1, tcxstrsize(rbuf), stdout);
    tcxstrdel(rbuf);
  }
  tcmapdel(cols);
  return 0;
}


/* perform passwd command */
static int procpasswd(const char *name, const char *pass, const char *salt, const char *info){
  if(!checkusername(name)){
    eprintf("%s: invalid user name", name);
    return 1;
  }
  char numbuf[NUMBUFSIZ];
  passwordhash(pass, salt, numbuf);
  printf("%s:%s:%s\n", name, numbuf, info);
  return 0;
}


/* perform version command */
static int procversion(void){
  printf("Tokyo Promenade version %s\n", TPVERSION);
  printf("Copyright (C) 2008-2009 Mikio Hirabayashi\n");
  return 0;
}



// END OF FILE
