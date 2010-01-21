/*************************************************************************************************
 * The Web interface of Tokyo Promenade
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


#include "common.h"
#include "scrext.h"
#if defined(MYFCGI)
#include <fcgi_stdio.h>
#endif

#define SALTNAME       "[salt]"          // dummy user name of the salt
#define RIDDLENAME     "[riddle]"        // dummy user name of the riddle
#define ADMINNAME      "admin"           // user name of the administrator

typedef struct {                         // type of structure for a record
  int64_t id;                            // ID of the article
  int64_t date;                          // date
  const char *owner;                     // owner
  const char *text;                      // text
} COMMENT;


/* global variables */
time_t g_starttime = 0;                  // start time of the process
TCMPOOL *g_mpool = NULL;                 // global memory pool
TCTMPL *g_tmpl = NULL;                   // template serializer
TCMAP *g_users = NULL;                   // user list
void *g_scrextproc = NULL;               // processor of the script extension
unsigned long g_eventcount = 0;          // event counter
const char *g_scriptname;                // script name
const char *g_scriptprefix;              // script prefix
const char *g_scriptpath;                // script path
const char *g_docroot;                   // document root
const char *g_database;                  // path of the database file
const char *g_password;                  // path of the password file
const char *g_upload;                    // path of the upload directory
const char *g_uploadpub;                 // public path of the upload directory
const char *g_scrext;                    // path of the script extension file
int64_t g_recvmax;                       // maximum size of received data
const char *g_title;                     // site title
int g_searchnum;                         // number of articles in a search page
int g_listnum;                           // number of articles in a list page
int g_feedlistnum;                       // number of articles in a RSS feed
int g_filenum;                           // number of files in a file list page
int g_sidebarnum;                        // number of items in the side bar
const char *g_commentmode;               // comment mode
const char *g_updatecmd;                 // path of the update command
int g_sessionlife;                       // lifetime of each session
const char *g_frontpage;                 // name of the front page


/* function prototypes */
int main(int argc, char **argv);
static int realmain(int argc, char **argv);
static void showerror(int code, const char *msg);
static void showcache(void);
static void readpasswd(void);
static bool writepasswd(void);
static void dosession(TCMPOOL *mpool);
static void setdberrmsg(TCLIST *emsgs, TCTDB *tdb, const char *msg);
static void setarthtml(TCMPOOL *mpool, TCMAP *cols, int64_t id, int bhl, bool tiny);
static TCLIST *searcharts(TCMPOOL *mpool, TCTDB *tdb, const char *cond, const char *expr,
                          const char *order, int max, int skip, bool ls);
static void getdaterange(const char *expr, int64_t *lowerp, int64_t *upper);
static bool putfile(TCMPOOL *mpool, const char *path, const char *name,
                    const char *ptr, int size);
static bool outfile(TCMPOOL *mpool, const char *path);
static TCLIST *searchfiles(TCMPOOL *mpool, const char *expr, const char *order,
                           int max, int skip, bool thum);
static int comparecomments(const TCLISTDATUM *a, const TCLISTDATUM *b);
static bool doupdatecmd(TCMPOOL *mpool, const char *mode, const char *baseurl, const char *user,
                        double now, int64_t id, TCMAP *ncols, TCMAP *ocols);


/* main routine */
int main(int argc, char **argv){
#if defined(MYFCGI)
  g_starttime = time(NULL);
  g_mpool = tcmpoolnew();
  int rv = 0;
  while(FCGI_Accept() >= 0){
    g_eventcount++;
    if(realmain(argc, argv) != 0) rv = 1;
  }
  tcmpooldel(g_mpool);
  return rv;
#else
  g_starttime = time(NULL);
  g_mpool = tcmpoolnew();
  int rv = realmain(argc, argv);
  tcmpooldel(g_mpool);
  return rv;
#endif
}


/* real main routine */
static int realmain(int argc, char **argv){
  TCMPOOL *mpool = tcmpoolnew();
  g_scriptname = getenv("SCRIPT_NAME");
  if(!g_scriptname) g_scriptname = argv[0];
  char *prefix = tcmpoolpushptr(mpool, tcregexreplace(g_scriptname, "\\.[a-zA-Z0-9]*$", ""));
  g_scriptprefix = prefix;
  g_scriptpath = getenv("SCRIPT_FILENAME");
  if(!g_scriptpath) g_scriptpath = argv[0];
  g_docroot = getenv("DOCUMENT_ROOT");
  if(!g_docroot){
    g_docroot = "/";
    if(*g_scriptpath == '/'){
      int diff = strlen(g_scriptpath) - strlen(g_scriptname);
      if(diff > 0 && !strcmp(g_scriptpath + diff, g_scriptname))
        g_docroot = tcmpoolpushptr(mpool, tcmemdup(g_scriptpath, diff));
    }
  }
  char *barepath = tcmpoolpushptr(mpool, tcregexreplace(g_scriptpath, "\\.[a-zA-Z0-9]*$", ""));
  char *tmplpath = tcmpoolpushptr(mpool, tcsprintf("%s.tmpl", barepath));
  const char *rp = getenv("REQUEST_URI");
  if(rp && *rp == '/'){
    char *buf = tcmpoolpushptr(mpool, strdup(rp));
    char *pv = strchr(buf, '#');
    if(pv) *pv = '\0';
    pv = strchr(buf, '?');
    if(pv) *pv = '\0';
    if(*buf != '\0') g_scriptname = buf;
  }
  if(g_tmpl){
    if(g_users){
      int64_t mtime;
      if(tcstatfile(g_password, NULL, NULL, &mtime) && mtime >= g_starttime){
        tcmapclear(g_users);
        readpasswd();
      }
    }
    dosession(mpool);
  } else {
    g_tmpl = tcmpoolpush(g_mpool, tctmplnew(), (void (*)(void *))tctmpldel);
    if(tctmplload2(g_tmpl, tmplpath)){
      g_database = tctmplconf(g_tmpl, "database");
      if(!g_database) g_database = "promenade.tct";
      g_password = tctmplconf(g_tmpl, "password");
      if(g_password){
        g_users = tcmpoolpushmap(g_mpool, tcmapnew2(TINYBNUM));
        readpasswd();
      }
      g_upload = tctmplconf(g_tmpl, "upload");
      g_uploadpub = NULL;
      if(g_upload && *g_upload != '\0'){
        if(!strchr(g_upload, '/')){
          g_uploadpub = g_upload;
        } else {
          char *rpath = tcmpoolpushptr(g_mpool, tcrealpath(g_upload));
          if(rpath){
            if(tcstrfwm(rpath, g_docroot)){
              int plen = strlen(g_docroot);
              if(rpath[plen] == '/') g_uploadpub = rpath + plen;
            }
          }
        }
      }
      g_scrext = tctmplconf(g_tmpl, "scrext");
      if(g_scrext && *g_scrext != '\0')
        g_scrextproc = tcmpoolpush(g_mpool, scrextnew(), scrextdel);
      const char *rp = tctmplconf(g_tmpl, "recvmax");
      g_recvmax = rp ? tcatoix(rp) : INT_MAX;
      g_title = tctmplconf(g_tmpl, "title");
      if(!g_title) g_title = "Tokyo Promenade";
      rp = tctmplconf(g_tmpl, "searchnum");
      g_searchnum = tclmax(rp ? tcatoi(rp) : 10, 1);
      rp = tctmplconf(g_tmpl, "listnum");
      g_listnum = tclmax(rp ? tcatoi(rp) : 10, 1);
      rp = tctmplconf(g_tmpl, "feedlistnum");
      g_feedlistnum = tclmax(rp ? tcatoi(rp) : 10, 1);
      rp = tctmplconf(g_tmpl, "filenum");
      g_filenum = tclmax(rp ? tcatoi(rp) : 10, 1);
      rp = tctmplconf(g_tmpl, "sidebarnum");
      g_sidebarnum = tclmax(rp ? tcatoi(rp) : 0, 0);
      g_commentmode = tctmplconf(g_tmpl, "commentmode");
      if(!g_commentmode) g_commentmode = "";
      g_updatecmd = tctmplconf(g_tmpl, "updatecmd");
      if(!g_updatecmd) g_updatecmd = "";
      rp = tctmplconf(g_tmpl, "sessionlife");
      g_sessionlife = tclmax(rp ? tcatoi(rp) : 0, 0);
      g_frontpage = tctmplconf(g_tmpl, "frontpage");
      if(!g_frontpage) g_frontpage = "";
      TCMAP *conf = g_tmpl->conf;
      tcmapiterinit(conf);
      while((rp = tcmapiternext2(conf)) != NULL){
        const char *pv = tcmapiterval2(rp);
        char *name = tcmpoolpushptr(g_mpool, tcsprintf("TP_%s", rp));
        tcstrtoupper(name);
        setenv(name, pv, 1);
      }
      if(g_scrextproc){
        scrextsetmapvar(g_scrextproc, "_conf", conf);
        scrextload(g_scrextproc, g_scrext);
      }
      dosession(mpool);
    } else {
      showerror(500, "The template file is missing.");
    }
  }
  tcmpooldel(mpool);
  return 0;
}


/* show the error page */
static void showerror(int code, const char *msg){
  switch(code){
    case 400:
      printf("Status: %d Bad Request\r\n", code);
      break;
    case 404:
      printf("Status: %d File Not Found\r\n", code);
      break;
    case 413:
      printf("Status: %d Request Entity Too Large\r\n", code);
      break;
    case 500:
      printf("Status: %d Internal Server Error\r\n", code);
      break;
    default:
      printf("Status: %d Error\r\n", code);
      break;
  }
  printf("Content-Type: text/plain; charset=UTF-8\r\n");
  printf("\r\n");
  printf("%s\n", msg);
}


/* show the not-modified page */
static void showcache(void){
  printf("Status: 304 Not Modified\r\n");
  printf("\r\n");
}


/* read the password file */
static void readpasswd(void){
  if(!g_password) return;
  TCLIST *lines = tcreadfilelines(g_password);
  if(!lines) return;
  int lnum = tclistnum(lines);
  for(int i = 0; i < lnum; i++){
    const char *line = tclistval2(lines, i);
    const char *pv = strchr(line, ':');
    if(!pv) continue;
    tcmapputkeep(g_users, line, pv - line, pv + 1, strlen(pv + 1));
  }
  tclistdel(lines);
}


/* write the password file */
static bool writepasswd(void){
  if(!g_password) return false;
  bool err = false;
  TCXSTR *xstr = tcxstrnew();
  tcmapiterinit(g_users);
  const char *name;
  while((name = tcmapiternext2(g_users)) != NULL){
    const char *value = tcmapiterval2(name);
    tcxstrprintf(xstr, "%s:%s\n", name, value);
  }
  if(!tcwritefile(g_password, tcxstrptr(xstr), tcxstrsize(xstr))) err = true;
  tcxstrdel(xstr);
  return !err;
}


/* process each session */
static void dosession(TCMPOOL *mpool){
  // download a file
  const char *rp = getenv("HTTP_IF_MODIFIED_SINCE");
  int64_t p_ifmod = rp ? tcstrmktime(rp) : 0;
  rp = getenv("PATH_INFO");
  if(rp && *rp == '/'){
    rp++;
    if(!g_upload){
      showerror(404, "The upload directory is missing.");
      return;
    }
    if(*rp == '\0' || strchr(rp, '/')){
      showerror(404, "The request path is invalid.");
      return;
    }
    const char *path = tcmpoolpushptr(mpool, tcsprintf("%s/%s", g_upload, rp));
    int64_t mtime;
    if(!tcstatfile(path, NULL, NULL, &mtime)){
      showerror(404, "The requested file is missing.");
      return;
    }
    if(mtime <= p_ifmod){
      showcache();
      return;
    }
    FILE *ifp = tcmpoolpush(mpool, fopen(path, "rb"), (void (*)(void *))fclose);
    if(!ifp){
      showerror(404, "The requested file is missing.");
      return;
    }
    printf("Content-Type: %s\r\n", mimetype(rp));
    printf("Cache-Control: no-cache\r\n");
    char numbuf[NUMBUFSIZ];
    tcdatestrhttp(mtime, 0, numbuf);
    printf("Last-Modified: %s\r\n", numbuf);
    printf("\r\n");
    int c;
    while((c = fgetc(ifp)) != EOF){
      putchar(c);
    }
    return;
  }
  // prepare session-scope variables
  TCMAP *vars = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
  TCLIST *emsgs = tcmpoollistnew(mpool);
  TCMAP *params = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
  double now = tctime();
  // read query parameters
  rp = getenv("CONTENT_LENGTH");
  bool post = false;
  if(rp && *rp != '\0'){
    int clen = tcatoi(rp);
    if(clen > g_recvmax){
      showerror(413, "The entity body was too long.");
      return;
    }
    char *cbuf = tcmpoolmalloc(mpool, clen + 1);
    if(fread(cbuf, 1, clen, stdin) != clen){
      showerror(500, "Reading the entity body was failed.");
      return;
    }
    tcwwwformdecode2(cbuf, clen, getenv("CONTENT_TYPE"), params);
    post = true;
  }
  rp = getenv("QUERY_STRING");
  if(rp) tcwwwformdecode(rp, params);
  rp = getenv("HTTP_COOKIE");
  if(rp) tcwwwformdecode(rp, params);
  const char *p_user = tcstrskipspc(tcmapget4(params, "user", ""));
  const char *p_pass = tcstrskipspc(tcmapget4(params, "pass", ""));
  const char *p_format = tcstrskipspc(tcmapget4(params, "format", ""));
  const char *p_act = tcstrskipspc(tcmapget4(params, "act", ""));
  int64_t p_id = tcatoi(tcmapget4(params, "id", ""));
  const char *p_name = tcstrskipspc(tcmapget4(params, "name", ""));
  const char *p_order = tcstrskipspc(tcmapget4(params, "order", ""));
  const char *p_adjust = tcstrskipspc(tcmapget4(params, "adjust", ""));
  const char *p_expr = tcstrskipspc(tcmapget4(params, "expr", ""));
  const char *p_cond = tcstrskipspc(tcmapget4(params, "cond", ""));
  int p_page = tclmax(tcatoi(tcmapget4(params, "page", "")), 1);
  const char *p_wiki = tcstrskipspc(tcmapget4(params, "wiki", ""));
  bool p_mts = *tcmapget4(params, "mts", "") != '\0';
  const char *p_hash = tcstrskipspc(tcmapget4(params, "hash", ""));
  uint32_t p_seskey = tcatoi(tcmapget4(params, "seskey", ""));
  const char *p_comowner = tcstrskipspc(tcmapget4(params, "comowner", ""));
  const char *p_comtext = tcstrskipspc(tcmapget4(params, "comtext", ""));
  const char *p_ummode = tcstrskipspc(tcmapget4(params, "ummode", ""));
  const char *p_umname = tcstrskipspc(tcmapget4(params, "umname", ""));
  const char *p_uminfo = tcstrskipspc(tcmapget4(params, "uminfo", ""));
  const char *p_umpassone = tcstrskipspc(tcmapget4(params, "umpassone", ""));
  const char *p_umpasstwo = tcstrskipspc(tcmapget4(params, "umpasstwo", ""));
  const char *p_umridque = tcstrskipspc(tcmapget4(params, "umridque", ""));
  const char *p_umridans = tcstrskipspc(tcmapget4(params, "umridans", ""));
  const char *p_fmmode = tcstrskipspc(tcmapget4(params, "fmmode", ""));
  const char *p_fmpath = tcstrskipspc(tcmapget4(params, "fmpath", ""));
  const char *p_fmname = tcstrskipspc(tcmapget4(params, "fmname", ""));
  int p_fmfilesiz;
  const char *p_fmfilebuf = tcmapget(params, "fmfile", 6, &p_fmfilesiz);
  const char *p_fmfilename = tcstrskipspc(tcmapget4(params, "fmfile_filename", ""));
  bool p_fmthum = *tcmapget4(params, "fmthum", "") != '\0';
  bool p_confirm = *tcmapget4(params, "confirm", "") != '\0';
  rp = getenv("REMOTE_HOST");
  const char *p_remotehost = rp ? rp : "";
  rp = getenv("HTTP_HOST");
  const char *p_hostname = rp ? rp : "";
  if(*p_hostname == '\0'){
    rp = getenv("SERVER_NAME");
    p_hostname = rp ? rp : "";
  }
  rp = getenv("SSL_PROTOCOL_VERSION");
  const char *p_scheme = (rp && *rp != '\0') ? "https" : "http";
  const char *p_scripturl = tcmpoolpushptr(mpool, tcsprintf("%s://%s%s",
                                                            p_scheme, p_hostname, g_scriptname));
  rp = getenv("HTTP_REFERER");
  const char *p_referrer = rp ? rp : "";
  rp = getenv("HTTP_USER_AGENT");
  const char *p_useragent = rp ? rp : "";
  const char *p_userlang = "";
  rp = getenv("HTTP_ACCEPT_LANGUAGE");
  if(rp){
    char *lang = tcmpoolpushptr(mpool, tcstrdup(rp));
    char *pv = strchr(lang, ',');
    if(pv) *pv = '\0';
    pv = strchr(lang, ';');
    if(pv) *pv = '\0';
    pv = strchr(lang, '-');
    if(pv) *pv = '\0';
    tcstrtrim(lang);
    p_userlang = lang;
  }
  if(*p_format == '\0'){
    rp = getenv("HTTP_ACCEPT");
    if(rp && strstr(rp, "application/xhtml+xml")) p_format = "xhtml";
  }
  // perform authentication
  bool auth = true;
  const char *userinfo = NULL;
  uint32_t seskey = 0;
  const char *authcookie = NULL;
  const char *ridque = "";
  const char *ridans = "";
  const char *ridcookie = NULL;
  rp = getenv("AUTH_TYPE");
  if(rp && (!tcstricmp(rp, "Basic") || !tcstricmp(rp, "Digest")) &&
     (rp = getenv("REMOTE_USER")) != NULL){
    p_user = rp;
    userinfo = "";
    tcmapput2(vars, "basicauth", "true");
  } else if(g_users){
    auth = false;
    const char *salt = tcmapget4(g_users, SALTNAME, "");
    int saltsiz = strlen(salt);
    bool cont = false;
    if(*p_user == '\0'){
      int authsiz;
      const char *authbuf = tcmapget(params, "auth", 4, &authsiz);
      if(authbuf && authsiz > 0){
        char *token = tcmpoolmalloc(mpool, authsiz + 1);
        tcarccipher(authbuf, authsiz, salt, saltsiz, token);
        token[authsiz] = '\0';
        TCLIST *elems = tcmpoolpushlist(mpool, tcstrsplit(token, ":"));
        if(tclistnum(elems) >= 4 && !strcmp(tclistval2(elems, 0), salt)){
          seskey = tcatoi(tclistval2(elems, 3));
          if(seskey > 0){
            p_user = tclistval2(elems, 1);
            p_pass = tclistval2(elems, 2);
            cont = true;
          }
        }
      }
    }
    if(*p_user != '\0'){
      rp = tcmapget2(g_users, p_user);
      if(rp){
        char *hash = tcmpoolpushptr(mpool, tcstrdup(rp));
        char *pv = strchr(hash, ':');
        if(pv) *(pv++) = '\0';
        char numbuf[NUMBUFSIZ];
        passwordhash(p_pass, salt, numbuf);
        if(!strcmp(hash, numbuf)){
          auth = true;
          userinfo = pv ? pv : "";
          if(seskey < 1){
            uint32_t seed = 19780211;
            for(rp = p_pass; *rp != '\0'; rp++){
              seed = seed * 31 + *(unsigned char *)rp;
            }
            double integ;
            double fract = modf(now, &integ) * (1ULL << 31);
            seskey = (((uint32_t)integ + (uint32_t)fract) ^ (seed << 8)) & INT32_MAX;
            if(seskey < 1) seskey = INT32_MAX;
          }
          int tsiz = strlen(p_user) + strlen(p_pass) + saltsiz + NUMBUFSIZ * 2;
          char token[tsiz];
          tsiz = sprintf(token, "%s:%s:%s:%u:%lld",
                         salt, p_user, p_pass, (unsigned int)seskey, (long long)now);
          tcmd5hash(token, tsiz, numbuf);
          sprintf(token + tsiz, ":%s", numbuf);
          tcarccipher(token, tsiz, salt, saltsiz, token);
          if(!cont){
            authcookie = tcmpoolpushptr(mpool, tcurlencode(token, tsiz));
            p_seskey = seskey;
          }
        }
      }
    }
    rp = tcmapget2(g_users, RIDDLENAME);
    if(rp){
      const char *pv = strstr(rp, ":");
      if(pv){
        ridque = tcmpoolpushptr(mpool, tcstrdup(pv + 1));
        ridans = tcmpoolpushptr(mpool, tcmemdup(rp, pv - rp));
      }
    }
    if(*ridans != '\0'){
      const char *ridbuf = tcmapget2(params, "riddle");
      if((ridbuf && !tcstricmp(ridbuf, ridans)) || !tcstricmp(p_umridans, ridans))
        ridcookie = ridans;
    }
  }
  if(!strcmp(p_act, "logout")){
    p_user = "";
    p_pass = "";
    auth = false;
    seskey = 0;
    authcookie = "";
  }
  bool admin = auth && !strcmp(p_user, ADMINNAME);
  bool cancom = false;
  if(!strcmp(g_commentmode, "all") || (!strcmp(g_commentmode, "login") && auth) ||
     (!strcmp(g_commentmode, "riddle") && (ridcookie || auth))) cancom = true;
  // execute the beginning script
  if(g_scrextproc){
    const char *emsg = screxterrmsg(g_scrextproc);
    if(emsg) tclistprintf(emsgs, "Loading scripting extension was failed (%s).", emsg);
    scrextsetmapvar(g_scrextproc, "_params", params);
    if(*p_user != '\0'){
      TCMAP *user = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
      tcmapput2(user, "name", p_user);
      tcmapput2(user, "pass", p_pass);
      if(userinfo) tcmapput2(user, "info", userinfo);
      scrextsetmapvar(g_scrextproc, "_user", user);
    }
  }
  if(g_scrextproc && scrextcheckfunc(g_scrextproc, "_begin")){
    char *obuf = tcmpoolpushptr(mpool, scrextcallfunc(g_scrextproc, "_begin", ""));
    if(obuf){
      tcmapput2(vars, "beginmsg", obuf);
    } else {
      const char *emsg = screxterrmsg(g_scrextproc);
      tclistprintf(emsgs, "Scripting extension was failed (%s).", emsg ? emsg : "(unknown)");
    }
  }
  // open the database
  TCTDB *tdb = tcmpoolpush(mpool, tctdbnew(), (void (*)(void *))tctdbdel);
  int omode = TDBOREADER;
  if(!strcmp(p_act, "update") && auth && post) omode = TDBOWRITER;
  if(!strcmp(p_act, "comment") && cancom && post) omode = TDBOWRITER;
  if(post && auth && *p_referrer != '\0'){
    char *src = tcmpoolpushptr(mpool, tcstrdup(p_referrer));
    char *wp = strchr(src, '?');
    if(wp) *wp = '\0';
    if(strcmp(src, p_scripturl)){
      tclistprintf(emsgs, "Referrer is invalid (%s).", src);
      admin = false;
      post = false;
      omode = TDBOREADER;
    }
  }
  if(!tctdbopen(tdb, g_database, omode))
    setdberrmsg(emsgs, tdb, "Opening the database was failed.");
  int64_t mtime = tctdbmtime(tdb);
  if(mtime < 1) mtime = now;
  // prepare the common query
  TCXSTR *comquery = tcmpoolxstrnew(mpool);
  if(*p_act != '\0') tcxstrprintf(comquery, "&act=%?", p_act);
  if(p_id > 0) tcxstrprintf(comquery, "&id=%lld", (long long)p_id);
  if(*p_name != '\0') tcxstrprintf(comquery, "&name=%?", p_name);
  if(*p_order != '\0') tcxstrprintf(comquery, "&order=%?", p_order);
  if(*p_expr != '\0') tcxstrprintf(comquery, "&expr=%?", p_expr);
  if(*p_cond != '\0') tcxstrprintf(comquery, "&cond=%?", p_cond);
  if(p_fmthum) tcxstrprintf(comquery, "&fmthum=on");
  // save a comment
  if(!strcmp(p_act, "comment") && p_id > 0 && *p_comowner != '\0' && *p_comtext != '\0'){
    char *owner = tcmpoolpushptr(mpool, tcstrdup(p_comowner));
    tcstrsqzspc(owner);
    char *text = tcmpoolpushptr(mpool, tcstrdup(p_comtext));
    tcstrsqzspc(text);
    if(*owner != '\0' && *text != '\0'){
      if(checkusername(p_comowner)){
        TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
        if(cols){
          if(checkfrozen(cols) && !admin){
            tclistprintf(emsgs, "Frozen articles are not editable by normal users.");
          } else {
            TCMAP *ocols = *g_updatecmd != '\0' ? tcmpoolpushmap(mpool, tcmapdup(cols)) : NULL;
            TCXSTR *wiki = tcmpoolxstrnew(mpool);
            wikidump(wiki, cols);
            TCXSTR *line = tcmpoolxstrnew(mpool);
            tcxstrprintf(line, "%lld|%s|%s\n", (long long)now, owner, text);
            tcmapputcat(cols, "comments", 8, tcxstrptr(line), tcxstrsize(line));
            if(dbputart(tdb, p_id, cols)){
              if(*g_updatecmd != '\0' &&
                 !doupdatecmd(mpool, "comment", p_scripturl, p_user, now, p_id, cols, ocols))
                tclistprintf(emsgs, "The update command was failed.");
            } else {
              setdberrmsg(emsgs, tdb, "Storing the article was failed.");
            }
          }
        }
      } else {
        tclistprintf(emsgs, "An invalid user name was specified.");
      }
    }
  }
  // perform each view
  if(!strcmp(p_act, "login")){
    // login view
    tcmapprintf(vars, "titletip", "[login]");
    tcmapput2(vars, "view", "login");
  } else if(!strcmp(p_act, "logincheck") && !auth){
    // login view
    tcmapprintf(vars, "titletip", "[login]");
    tcmapput2(vars, "view", "login");
  } else if(!strcmp(p_act, "edit")){
    // edit view
    if(p_id > 0){
      TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
      if(cols){
        if(checkfrozen(cols) && !admin){
          tclistprintf(emsgs, "Frozen articles are not editable by normal users.");
        } else {
          TCXSTR *wiki = tcmpoolxstrnew(mpool);
          wikidump(wiki, cols);
          tcmapprintf(cols, "id", "%lld", (long long)p_id);
          char numbuf[NUMBUFSIZ];
          tcmd5hash(tcxstrptr(wiki), tcxstrsize(wiki), numbuf);
          tcmapput2(cols, "hash", numbuf);
          tcmapput2(vars, "view", "edit");
          tcmapputmap(vars, "art", cols);
          tcmapput(vars, "wiki", 4, tcxstrptr(wiki), tcxstrsize(wiki));
        }
      } else {
        tcmapput2(vars, "view", "empty");
      }
      tcmapprintf(vars, "cond", "id:%lld", (long long)p_id);
    } else {
      tcmapprintf(vars, "titletip", "[edit]");
      tcmapput2(vars, "view", "edit");
      if(*p_name != '\0') tcmapput2(vars, "name", p_name);
      if(*p_user != '\0') tcmapput2(vars, "user", p_user);
      if(!strcmp(p_adjust, "front")) tcmapput2(vars, "tags", "*,?");
    }
  } else if(!strcmp(p_act, "preview")){
    // preview view
    if(p_id > 0){
      TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
      if(cols){
        if(checkfrozen(cols) && !admin){
          tclistprintf(emsgs, "Frozen articles are not editable by normal users.");
        } else {
          TCXSTR *wiki = tcmpoolxstrnew(mpool);
          wikidump(wiki, cols);
          char numbuf[NUMBUFSIZ];
          tcmd5hash(tcxstrptr(wiki), tcxstrsize(wiki), numbuf);
          if(!strcmp(numbuf, p_hash)){
            if(*p_wiki != '\0'){
              tcmapclear(cols);
              wikiload(cols, p_wiki);
              if(p_mts) tcmapprintf(cols, "mdate", "%lld", (long long)now);
              const char *name = tcmapget2(cols, "name");
              if(!name || *name == '\0'){
                tclistprintf(emsgs, "The name can not be empty.");
              } else if(checkfrozen(cols) && !admin){
                tclistprintf(emsgs, "The frozen tag is not available by normal users.");
              } else {
                setarthtml(mpool, cols, p_id, 0, false);
                tcmapprintf(vars, "titletip", "[preview]");
                tcmapput2(vars, "view", "preview");
                tcmapputmap(vars, "art", cols);
                tcmapput2(vars, "wiki", p_wiki);
                if(p_mts) tcmapput2(vars, "mts", "on");
                tcmapprintf(vars, "id", "%lld", (long long)p_id);
                tcmapput2(vars, "hash", p_hash);
              }
            } else {
              tcmapput2(vars, "view", "removecheck");
              tcmapputmap(vars, "art", cols);
              tcmapprintf(vars, "id", "%lld", (long long)p_id);
              tcmapput2(vars, "hash", p_hash);
            }
          } else {
            tcmapput2(vars, "view", "collision");
            tcmapput(vars, "wiki", 4, tcxstrptr(wiki), tcxstrsize(wiki));
            tcmapput2(vars, "yourwiki", p_wiki);
          }
        }
      } else {
        tcmapput2(vars, "view", "empty");
      }
      tcmapprintf(vars, "cond", "id:%lld", (long long)p_id);
    } else {
      TCMAP *cols = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
      wikiload(cols, p_wiki);
      if(p_mts){
        tcmapprintf(cols, "cdate", "%lld", (long long)now);
        tcmapprintf(cols, "mdate", "%lld", (long long)now);
      }
      const char *name = tcmapget2(cols, "name");
      if(!name || *name == '\0'){
        tclistprintf(emsgs, "The name can not be empty.");
        tcmapput2(vars, "view", "edit");
        tcmapput2(vars, "wiki", p_wiki);
      } else if(checkfrozen(cols)  && !admin){
        tclistprintf(emsgs, "The frozen tag is not available by normal users.");
        tcmapput2(vars, "view", "edit");
        tcmapput2(vars, "wiki", p_wiki);
      } else {
        setarthtml(mpool, cols, 0, 0, false);
        tcmapprintf(vars, "titletip", "[preview]");
        tcmapput2(vars, "view", "preview");
        tcmapputmap(vars, "art", cols);
        tcmapput2(vars, "wiki", p_wiki);
        if(p_mts) tcmapput2(vars, "mts", "on");
      }
    }
  } else if(!strcmp(p_act, "update")){
    // update view
    if(seskey > 0 && p_seskey != seskey){
      tclistprintf(emsgs, "The session key is invalid (%u).", (unsigned int)p_seskey);
    } else if(p_id > 0){
      TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
      if(cols){
        if(checkfrozen(cols) && !admin){
          tclistprintf(emsgs, "Frozen articles are not editable by normal users.");
        } else {
          TCXSTR *wiki = tcmpoolxstrnew(mpool);
          wikidump(wiki, cols);
          char numbuf[NUMBUFSIZ];
          tcmd5hash(tcxstrptr(wiki), tcxstrsize(wiki), numbuf);
          if(!strcmp(numbuf, p_hash)){
            TCMAP *ocols = *g_updatecmd != '\0' ? tcmpoolpushmap(mpool, tcmapdup(cols)) : NULL;
            if(*p_wiki != '\0'){
              tcmapclear(cols);
              wikiload(cols, p_wiki);
              if(p_mts) tcmapprintf(cols, "mdate", "%lld", (long long)now);
              const char *name = tcmapget2(cols, "name");
              if(!name || *name == '\0'){
                tclistprintf(emsgs, "The name can not be empty.");
              } else if(checkfrozen(cols) && !admin){
                tclistprintf(emsgs, "The frozen tag is not available by normal users.");
              } else if(dbputart(tdb, p_id, cols)){
                if(*g_updatecmd != '\0' &&
                   !doupdatecmd(mpool, "update", p_scripturl, p_user, now, p_id, cols, ocols))
                  tclistprintf(emsgs, "The update command was failed.");
                tcmapput2(vars, "view", "store");
                tcmapputmap(vars, "art", cols);
              } else {
                setdberrmsg(emsgs, tdb, "Storing the article was failed.");
              }
            } else {
              if(dboutart(tdb, p_id)){
                if(*g_updatecmd != '\0' &&
                   !doupdatecmd(mpool, "remove", p_scripturl, p_user, now, p_id, NULL, ocols))
                  tclistprintf(emsgs, "The update command was failed.");
                tcmapprintf(cols, "id", "%lld", (long long)p_id);
                tcmapput2(vars, "view", "remove");
                tcmapputmap(vars, "art", cols);
              } else {
                setdberrmsg(emsgs, tdb, "Removing the article was failed.");
              }
            }
          } else {
            tcmapput2(vars, "view", "collision");
            tcmapput(vars, "wiki", 4, tcxstrptr(wiki), tcxstrsize(wiki));
            tcmapput2(vars, "yourwiki", p_wiki);
          }
        }
      } else {
        tcmapput2(vars, "view", "empty");
      }
      tcmapprintf(vars, "cond", "id:%lld", (long long)p_id);
    } else {
      TCMAP *cols = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
      wikiload(cols, p_wiki);
      if(p_mts){
        tcmapprintf(cols, "cdate", "%lld", (long long)now);
        tcmapprintf(cols, "mdate", "%lld", (long long)now);
      }
      const char *name = tcmapget2(cols, "name");
      if(!name || *name == '\0'){
        tclistprintf(emsgs, "The name can not be empty.");
        tcmapput2(vars, "view", "edit");
        tcmapput2(vars, "wiki", p_wiki);
      } else if(dbputart(tdb, 0, cols)){
        rp = tcmapget2(cols, "id");
        int64_t nid = rp ? tcatoi(rp) : 0;
        if(*g_updatecmd != '\0' &&
           !doupdatecmd(mpool, "new", p_scripturl, p_user, now, nid, cols, NULL))
          tclistprintf(emsgs, "The update command was failed.");
        tcmapput2(vars, "view", "store");
        tcmapputmap(vars, "art", cols);
      } else {
        setdberrmsg(emsgs, tdb, "Storing the article was failed.");
      }
    }
  } else if(!strcmp(p_act, "users")){
    // users view
    if(g_users){
      if(admin){
        if(post && p_umname != '\0'){
          if(seskey > 0 && p_seskey != seskey){
            tclistprintf(emsgs, "The session key is invalid (%u).", (unsigned int)p_seskey);
          } else if(!strcmp(p_ummode, "new")){
            if(tcmapget2(g_users, p_umname)){
              tclistprintf(emsgs, "The user already exists.");
            } else if(!checkusername(p_umname)){
              tclistprintf(emsgs, "The user name is invalid.");
            } else if(strcmp(p_umpassone, p_umpasstwo)){
              tclistprintf(emsgs, "The two passwords are different.");
            } else {
              const char *salt = tcmapget4(g_users, SALTNAME, "");
              char numbuf[NUMBUFSIZ];
              passwordhash(p_umpassone, salt, numbuf);
              tcmapprintf(g_users, p_umname, "%s:%s", numbuf, p_uminfo);
              if(writepasswd()){
                tcmapput2(vars, "newuser", p_umname);
              } else {
                tclistprintf(emsgs, "Storing the password file was failed.");
              }
            }
          } else if(!strcmp(p_ummode, "chpw")){
            const char *pass = tcmapget2(g_users, p_umname);
            if(!pass){
              tclistprintf(emsgs, "The user does not exist.");
            } else if(strcmp(p_umpassone, p_umpasstwo)){
              tclistprintf(emsgs, "The two passwords are different.");
            } else {
              char *str = tcmpoolpushptr(mpool, tcstrdup(pass));
              char *pv = strchr(str, ':');
              if(pv){
                *(pv++) = '\0';
              } else {
                pv = "";
              }
              const char *salt = tcmapget4(g_users, SALTNAME, "");
              char numbuf[NUMBUFSIZ];
              passwordhash(p_umpassone, salt, numbuf);
              tcmapprintf(g_users, p_umname, "%s:%s", numbuf, pv);
              if(writepasswd()){
                tcmapput2(vars, "chpwuser", p_umname);
                if(!strcmp(p_umname, p_user)){
                  p_user = "";
                  p_pass = "";
                  auth = false;
                  authcookie = "";
                  tcmapput2(vars, "tologin", p_umname);
                }
              } else {
                tclistprintf(emsgs, "Storing the password file was failed.");
              }
            }
          } else if(!strcmp(p_ummode, "del") && p_confirm){
            if(!tcmapget2(g_users, p_umname)){
              tclistprintf(emsgs, "The user does not exist.");
            } else {
              tcmapout2(g_users, p_umname);
              if(writepasswd()){
                tcmapput2(vars, "deluser", p_umname);
                if(!strcmp(p_umname, p_user)){
                  p_user = "";
                  p_pass = "";
                  auth = false;
                  authcookie = "";
                  tcmapput2(vars, "tologin", p_umname);
                }
              } else {
                tclistprintf(emsgs, "Storing the password file was failed.");
              }
            }
          } else if(!strcmp(p_ummode, "rid")){
            if(!checkusername(p_umridans)){
              tclistprintf(emsgs, "The answer is invalid.");
            } else {
              tcmapprintf(g_users, RIDDLENAME, "%s:%s", p_umridans, p_umridque);
              if(writepasswd()){
                tcmapput2(vars, "chrid", p_umname);
                ridque = p_umridque;
                ridans = p_umridans;
              } else {
                tclistprintf(emsgs, "Storing the password file was failed.");
              }
            }
          }
        }
        TCLIST *ulist = tcmpoollistnew(mpool);
        tcmapiterinit(g_users);
        const char *salt = NULL;
        const char *name;
        while((name = tcmapiternext2(g_users)) != NULL){
          const char *pass = tcmapiterval2(name);
          if(!strcmp(name, SALTNAME)){
            salt = pass;
          } else if(*name != SALTNAME[0]){
            TCMAP *user = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
            char *str = tcmpoolpushptr(mpool, tcstrdup(pass));
            char *pv = strchr(str, ':');
            if(pv){
              *(pv++) = '\0';
            } else {
              pv = "";
            }
            tcmapput2(user, "name", name);
            tcmapput2(user, "pass", str);
            tcmapput2(user, "info", pv);
            if(!strcmp(name, ADMINNAME)) tcmapput2(user, "admin", "true");
            tclistpushmap(ulist, user);
          }
        }
        tcmapprintf(vars, "titletip", "[user management]");
        tcmapput2(vars, "view", "users");
        if(tclistnum(ulist) > 0) tcmapputlist(vars, "userlist", ulist);
        if(salt) tcmapput2(vars, "salt", salt);
        tcmapput2(vars, "ridque", ridque);
        tcmapput2(vars, "ridans", ridans);
      } else {
        tclistprintf(emsgs, "The user management function is not available by normal users.");
      }
    } else {
      tclistprintf(emsgs, "The password file is missing.");
    }
  } else if(!strcmp(p_act, "files")){
    // files view
    bool isdir;
    if(g_upload && tcstatfile(g_upload, &isdir, NULL, &mtime) && isdir){
      if(auth){
        if(post && (p_fmfilebuf || *p_fmpath != '\0')){
          if(seskey > 0 && p_seskey != seskey){
            tclistprintf(emsgs, "The session key is invalid (%u).", (unsigned int)p_seskey);
          } else if(!strcmp(p_fmmode, "new")){
            if(p_fmfilebuf && p_fmfilesiz > 0){
              const char *name = p_fmname;
              if(*name == '\0') name = p_fmfilename;
              if(*name == '\0') name = "_noname_";
              const char *ext = strrchr(name, '.');
              if(!ext && (ext = strrchr(p_fmfilename, '.')) != NULL)
                name = tcmpoolpushptr(mpool, tcsprintf("%s%s", name, ext));
              if(putfile(mpool, p_fmpath, name, p_fmfilebuf, p_fmfilesiz)){
                tcmapput2(vars, "newfile", name);
              } else {
                tclistprintf(emsgs, "Storing the file was failed.");
              }
            } else {
              tclistprintf(emsgs, "There is no data.");
            }
          } else if(!strcmp(p_fmmode, "repl")){
            if(p_fmfilebuf && p_fmfilesiz > 0){
              if(putfile(mpool, p_fmpath, "", p_fmfilebuf, p_fmfilesiz)){
                tcmapput2(vars, "replfile", p_fmname);
              } else {
                tclistprintf(emsgs, "Storing the file was failed.");
              }
            } else {
              tclistprintf(emsgs, "There is no data.");
            }
          } else if(!strcmp(p_fmmode, "del") && p_confirm){
            if(outfile(mpool, p_fmpath)){
              tcmapput2(vars, "delfile", p_fmname);
            } else {
              tclistprintf(emsgs, "Removing the file was failed.");
            }
          }
        } else {
          if(mtime <= p_ifmod){
            showcache();
            return;
          }
          char numbuf[NUMBUFSIZ];
          tcdatestrhttp(mtime, 0, numbuf);
          tcmapput2(vars, "lastmod", numbuf);
        }
        int max = g_filenum;
        int skip = max * (p_page - 1);
        TCLIST *files = searchfiles(mpool, p_expr, p_order, max + 1, skip, p_fmthum);
        bool over = false;
        if(tclistnum(files) > max){
          tcfree(tclistpop2(files));
          over = true;
        }
        tcmapprintf(vars, "titletip", "[file management]");
        tcmapput2(vars, "view", "files");
        if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
        if(over) tcmapprintf(vars, "next", "%d", p_page + 1);
        if(tclistnum(files) > 0) tcmapputlist(vars, "files", files);
      } else {
        tclistprintf(emsgs, "The file management function is not available by outer users.");
      }
    } else {
      tclistprintf(emsgs, "The upload directory is missing.");
    }
  } else if(p_id > 0){
    // single view
    if(!auth && !ridcookie){
      if(mtime <= p_ifmod){
        showcache();
        return;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
    if(cols){
      setarthtml(mpool, cols, p_id, 0, false);
      if(checkfrozen(cols) && !admin){
        tcmapput2(cols, "frozen", "true");
      } else if(cancom){
        tcmapputkeep2(cols, "comnum", "0");
        tcmapputkeep2(cols, "cancom", "true");
      } else if(!strcmp(g_commentmode, "riddle")){
        tcmapputkeep2(cols, "comnum", "0");
        tcmapput2(vars, "ridque", ridque);
        tcmapput2(vars, "ridans", ridans);
      }
      const char *name = tcmapget2(cols, "name");
      if(name) tcmapput2(vars, "titletip", name);
      if(!strcmp(p_adjust, "front")){
        tcmapput2(vars, "view", "front");
      } else {
        tcmapput2(vars, "view", "single");
      }
      tcmapput2(vars, "robots", "index,follow");
      tcmapputmap(vars, "art", cols);
    } else {
      tcmapput2(vars, "view", "empty");
    }
    tcmapprintf(vars, "cond", "id:%lld", (long long)p_id);
  } else if(*p_name != '\0'){
    // single view or search view
    if(!auth){
      if(mtime <= p_ifmod){
        showcache();
        return;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    int max = g_searchnum;
    int skip = max * (p_page - 1);
    const char *order = (*p_order == '\0') ? "_cdate" : p_order;
    TCLIST *res = searcharts(mpool, tdb, "name", p_name, order, max + 1, skip, false);
    int rnum = tclistnum(res);
    if(rnum < 1){
      tcmapput2(vars, "view", "empty");
      if(auth) tcmapput2(vars, "missname", p_name);
    } else if(rnum < 2 || p_confirm){
      int64_t id = tcatoi(tclistval2(res, 0));
      TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
      if(cols){
        setarthtml(mpool, cols, id, 0, false);
        if(checkfrozen(cols) && !admin){
          tcmapput2(cols, "frozen", "true");
        } else if(cancom){
          tcmapputkeep2(cols, "comnum", "0");
          tcmapputkeep2(cols, "cancom", "true");
        }
        const char *name = tcmapget2(cols, "name");
        if(name) tcmapput2(vars, "titletip", name);
        if(!strcmp(p_adjust, "front")){
          tcmapput2(vars, "view", "front");
        } else {
          tcmapput2(vars, "view", "single");
        }
        tcmapput2(vars, "robots", "index,follow");
        tcmapputmap(vars, "art", cols);
      } else {
        tcmapput2(vars, "view", "empty");
      }
    } else {
      tcmapprintf(vars, "hitnum", "%d", rnum);
      TCLIST *arts = tcmpoollistnew(mpool);
      for(int i = 0; i < rnum && i < max; i++){
        int64_t id = tcatoi(tclistval2(res, i));
        TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
        if(cols){
          setarthtml(mpool, cols, id, 1, true);
          tclistpushmap(arts, cols);
        }
      }
      if(tclistnum(arts) > 0){
        tcmapprintf(vars, "titletip", "[name:%s]", p_name);
        tcmapput2(vars, "view", "search");
        tcmapput2(vars, "robots", "noindex,follow");
        if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
        if(rnum > max) tcmapprintf(vars, "next", "%d", p_page + 1);
        tcmapputlist(vars, "arts", arts);
      } else {
        tcmapput2(vars, "view", "empty");
      }
    }
    tcmapprintf(vars, "cond", "name:%s", p_name);
  } else if(!strcmp(p_act, "search")){
    // search view
    if(!auth){
      if(mtime <= p_ifmod){
        showcache();
        return;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    tcmapprintf(vars, "titletip", "[search]");
    tcmapput2(vars, "view", "search");
    tcmapput2(vars, "robots", "noindex,follow");
    int max = g_searchnum;
    int skip = max * (p_page - 1);
    TCLIST *res = searcharts(mpool, tdb, p_cond, p_expr, p_order, max + 1, skip, true);
    int rnum = tclistnum(res);
    TCLIST *arts = tcmpoollistnew(mpool);
    for(int i = 0; i < rnum && i < max; i++){
      int64_t id = tcatoi(tclistval2(res, i));
      TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
      if(cols){
        setarthtml(mpool, cols, id, 1, true);
        tclistpushmap(arts, cols);
      }
    }
    if(tclistnum(arts) > 0){
      if(*p_expr != '\0') tcmapprintf(vars, "titletip", "[search:%s]", p_expr);
      if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
      if(rnum > max) tcmapprintf(vars, "next", "%d", p_page + 1);
      if(tcmapget2(vars, "prev") || tcmapget2(vars, "next")) tcmapput2(vars, "page", "true");
      tcmapputlist(vars, "arts", arts);
    }
    if(*p_cond != '\0'){
      tcmapprintf(vars, "hitnum", "%d", rnum);
    } else {
      res = tcmpoollistnew(mpool);
      char numbuf[NUMBUFSIZ];
      tcdatestrwww(now, INT_MAX, numbuf);
      int year = tcatoi(numbuf);
      int minyear = year;
      res = searcharts(mpool, tdb, "cdate", "x", "_cdate", 1, 0, true);
      if(tclistnum(res) > 0){
        int64_t id = tcatoi(tclistval2(res, 0));
        TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
        const char *value = cols ? tcmapget2(cols, "cdate") : NULL;
        if(value){
          int64_t cdate = tcstrmktime(value);
          tcdatestrwww(cdate, INT_MAX, numbuf);
          minyear = tcatoi(numbuf);
        }
      }
      TCLIST *arcyears = tcmpoollistnew(mpool);
      for(int i = 0; i < 100 && year >= minyear; i++){
        sprintf(numbuf, "%04d", year);
        res = searcharts(mpool, tdb, "cdate", numbuf, "cdate", 1, 0, true);
        if(tclistnum(res) > 0){
          TCMAP *arcmonths = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
          for(int month = 0; month <= 12; month++){
            sprintf(numbuf, "%04d-%02d", year, month);
            res = searcharts(mpool, tdb, "cdate", numbuf, "cdate", 1, 0, true);
            rnum = tclistnum(res);
            sprintf(numbuf, "%02d", month);
            if(rnum > 0) tcmapprintf(arcmonths, numbuf, "%d", rnum);
          }
          if(tcmaprnum(arcmonths) > 0){
            tcmapprintf(arcmonths, "year", "%d", year);
            tclistpushmap(arcyears, arcmonths);
          }
        }
        year--;
      }
      if(tclistnum(arcyears) > 0) tcmapputlist(vars, "arcyears", arcyears);
    }
  } else if(*g_frontpage != '\0' && strcmp(p_act, "timeline")){
    // front view
    if(!auth){
      if(mtime <= p_ifmod){
        showcache();
        return;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    int64_t id = 0;
    const char *name = "";
    if(tcstrfwm(g_frontpage, "id:")){
      id = tcatoi(strchr(g_frontpage, ':') + 1);
    } else if(tcstrfwm(g_frontpage, "name:")){
      name = strchr(g_frontpage, ':') + 1;
    } else {
      name = g_frontpage;
    }
    if(id < 1 && *name != '\0'){
      TCLIST *res = searcharts(mpool, tdb, "name", name, "_cdate", 1, 0, false);
      if(tclistnum(res) > 0) id = tcatoi(tclistval2(res, 0));
    }
    tcmapput2(vars, "view", "front");
    tcmapput2(vars, "robots", "index,follow");
    if(id > 0){
      TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, id));
      if(cols){
        setarthtml(mpool, cols, id, 0, false);
        if(checkfrozen(cols) && !admin) tcmapput2(cols, "frozen", "true");
        tcmapputmap(vars, "art", cols);
      }
    }
  } else {
    // timeline view
    if(!auth){
      if(mtime <= p_ifmod){
        showcache();
        return;
      }
      char numbuf[NUMBUFSIZ];
      tcdatestrhttp(mtime, 0, numbuf);
      tcmapput2(vars, "lastmod", numbuf);
    }
    int max = !strcmp(p_format, "atom") ? g_feedlistnum : g_listnum;
    int skip = max * (p_page - 1);
    TCLIST *res = searcharts(mpool, tdb, NULL, NULL, p_order, max + 1, skip, true);
    int rnum = tclistnum(res);
    if(rnum < 1){
      tcmapput2(vars, "view", "empty");
    } else {
      TCLIST *arts = tcmpoollistnew(mpool);
      for(int i = 0; i < rnum && i < max ; i++){
        int64_t id = tcatoi(tclistval2(res, i));
        TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
        if(cols){
          setarthtml(mpool, cols, id, 1, false);
          tclistpushmap(arts, cols);
        }
      }
      if(tclistnum(arts) > 0){
        if(*p_act != '\0'){
          if(!strcmp(p_order, "_cdate")){
            tcmapput2(vars, "titletip", "[old creation]");
          } else if(!strcmp(p_order, "mdate")){
            tcmapput2(vars, "titletip", "[recent modification]");
          } else if(!strcmp(p_order, "_mdate")){
            tcmapput2(vars, "titletip", "[old modification]");
          } else if(!strcmp(p_order, "xdate")){
            tcmapput2(vars, "titletip", "[recent comment]");
          } else if(!strcmp(p_order, "_xdate")){
            tcmapput2(vars, "titletip", "[old comment]");
          } else {
            tcmapput2(vars, "titletip", "[newcome articles]");
          }
        }
        tcmapput2(vars, "view", "timeline");
        tcmapput2(vars, "robots", "index,follow");
        if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
        if(rnum > max) tcmapprintf(vars, "next", "%d", p_page + 1);
        if(tcmapget2(vars, "prev") || tcmapget2(vars, "next")) tcmapput2(vars, "page", "true");
        tcmapputlist(vars, "arts", arts);
      } else {
        tcmapput2(vars, "view", "empty");
      }
    }
  }
  if(g_sidebarnum > 0 && strcmp(p_format, "atom")){
    // side bar
    TCLIST *res = searcharts(mpool, tdb, NULL, NULL, "cdate", g_sidebarnum, 0, true);
    int rnum = tclistnum(res);
    TCLIST *arts = tcmpoollistnew(mpool);
    for(int i = 0; i < rnum; i++){
      int64_t id = tcatoi(tclistval2(res, i));
      TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
      if(cols){
        setarthtml(mpool, cols, id, 1, true);
        tclistpushmap(arts, cols);
      }
    }
    if(tclistnum(arts) > 0) tcmapputlist(vars, "sidearts", arts);
    res = searcharts(mpool, tdb, NULL, NULL, "xdate", g_sidebarnum, 0, true);
    rnum = tclistnum(res);
    TCLIST *coms = tcmpoollistnew(mpool);
    for(int i = 0; i < rnum; i++){
      int64_t id = tcatoi(tclistval2(res, i));
      TCMAP *cols = tcmpoolpushmap(mpool, id > 0 ? dbgetart(tdb, id) : NULL);
      if(cols){
        rp = tcmapget2(cols, "comments");
        if(rp && *rp != '\0'){
          TCLIST *lines = tcmpoolpushlist(mpool, tcstrsplit(rp, "\n"));
          int left = g_sidebarnum;
          for(int i = tclistnum(lines) - 1; i >= 0 && left > 0; i--, left--){
            rp = tclistval2(lines, i);
            char *co = strchr(rp, '|');
            if(co){
              *(co++) = '\0';
              char *ct = strchr(co, '|');
              if(ct){
                *(ct++) = '\0';
                COMMENT com;
                memset(&com, 0, sizeof(com));
                com.id = id;
                com.date = tcatoi(rp);
                com.owner = co;
                com.text = ct;
                tclistpush(coms, &com, sizeof(com));
              }
            }
          }
        }
      }
    }
    int cnum = tclistnum(coms);
    if(cnum > 0){
      tclistsortex(coms, comparecomments);
      TCLIST *comments = tcmpoolpushlist(mpool, tclistnew2(cnum));
      for(int i = 0; i < cnum && i < g_sidebarnum; i++){
        COMMENT *com = (COMMENT *)tclistval2(coms, i);
        TCMAP *comment = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
        tcmapprintf(comment, "id", "%lld", (long long)com->id);
        char numbuf[NUMBUFSIZ];
        tcdatestrwww(com->date, INT_MAX, numbuf);
        tcmapput2(comment, "date", numbuf);
        tcmapput2(comment, "datesimple", datestrsimple(numbuf));
        tcmapput2(comment, "owner", com->owner);
        tcmapput2(comment, "text", com->text);
        TCXSTR *xstr = tcmpoolxstrnew(mpool);
        wikitohtmlinline(xstr, com->text, g_scriptname, g_uploadpub);
        tcmapput(comment, "texthtml", 8, tcxstrptr(xstr), tcxstrsize(xstr));
        tclistpushmap(comments, comment);
      }
      tcmapputlist(vars, "sidecoms", comments);
    }
    tcmapput2(vars, "sidebar", "true");
  }
  // close the database
  if(!tctdbclose(tdb)) setdberrmsg(emsgs, tdb, "Closing the database was failed.");
  // execute the ending script
  if(g_scrextproc && scrextcheckfunc(g_scrextproc, "_end")){
    char *obuf = tcmpoolpushptr(mpool, scrextcallfunc(g_scrextproc, "_end", ""));
    if(obuf){
      tcmapput2(vars, "endmsg", obuf);
    } else {
      const char *emsg = screxterrmsg(g_scrextproc);
      tclistprintf(emsgs, "Scripting extension was failed (%s).", emsg ? emsg : "(unknown)");
    }
  }
  // generete the output
  if(tclistnum(emsgs) > 0) tcmapputlist(vars, "emsgs", emsgs);
  tcmapputmap(vars, "params", params);
  if(tcxstrsize(comquery) > 0) tcmapput2(vars, "comquery", tcxstrptr(comquery));
  if(!strcmp(p_act, "logout")) tcmapout2(vars, "lastmod");
  tcmapput2(vars, "scriptname", g_scriptname);
  tcmapput2(vars, "scriptprefix", g_scriptprefix);
  tcmapput2(vars, "scriptpath", g_scriptpath);
  tcmapput2(vars, "scripturl", p_scripturl);
  tcmapput2(vars, "documentroot", g_docroot);
  if(g_users) tcmapputmap(vars, "users", g_users);
  if(auth && p_user != '\0'){
    tcmapput2(vars, "username", p_user);
    if(userinfo && *userinfo != '\0') tcmapput2(vars, "userinfo", userinfo);
    if(seskey > 0) tcmapprintf(vars, "seskey", "%llu", (unsigned long long)seskey);
  }
  if(auth) tcmapput2(vars, "auth", "true");
  if(admin) tcmapput2(vars, "admin", "true");
  if(cancom) tcmapput2(vars, "cancom", "true");
  if(g_uploadpub) tcmapput2(vars, "uploadpub", g_uploadpub);
  tcmapput2(vars, "tpversion", TPVERSION);
  char numbuf[NUMBUFSIZ];
  tcdatestrwww(now, INT_MAX, numbuf);
  tcmapput2(vars, "now", numbuf);
  tcdatestrwww(mtime, INT_MAX, numbuf);
  tcmapput2(vars, "mtime", numbuf);
  tcmapput2(vars, "mtimesimple", datestrsimple(numbuf));
  if(tcmapget2(vars, "prev") || tcmapget2(vars, "next")) tcmapput2(vars, "page", "true");
  tcmapputkeep2(vars, "view", "error");
  tcmapput2(vars, "format", !strcmp(p_format, "atom") ? "atom" : "html");
  tcmapput2(vars, "scheme", p_scheme);
  tcmapput2(vars, "remotehost", p_remotehost);
  tcmapput2(vars, "hostname", p_hostname);
  tcmapput2(vars, "referrer", p_referrer);
  tcmapput2(vars, "useragent", p_useragent);
  tcmapput2(vars, "userlang", p_userlang);
  const char *mtype = "text/html; charset=UTF-8";
  if(!strcmp(p_format, "atom")){
    mtype = "application/atom+xml";
  } else if(!strcmp(p_format, "xhtml")){
    mtype = "application/xhtml+xml";
  } else if(!strcmp(p_format, "xml")){
    mtype = "application/xml";
  }
  tcmapput2(vars, "mimetype", mtype);
  const char *tmplstr = tcmpoolpushptr(mpool, tctmpldump(g_tmpl, vars));
  while(*tmplstr > '\0' && *tmplstr <= ' '){
    tmplstr++;
  }
  if(g_scrextproc && scrextcheckfunc(g_scrextproc, "_procpage")){
    char *obuf = tcmpoolpushptr(mpool, scrextcallfunc(g_scrextproc, "_procpage", tmplstr));
    if(obuf) tmplstr = obuf;
  }
  printf("Content-Type: %s\r\n", mtype);
  rp = tcmapget2(vars, "lastmod");
  if(rp) printf("Last-Modified: %s\r\n", rp);
  printf("Cache-Control: no-cache\r\n");
  if(authcookie){
    printf("Set-Cookie: auth=%s;", authcookie);
    if(g_sessionlife > 0) printf(" max-age=%d;", g_sessionlife);
    printf("\r\n");
  }
  if(ridcookie){
    printf("Set-Cookie: riddle=%s;", ridcookie);
    if(g_sessionlife > 0) printf(" max-age=%d;", g_sessionlife);
    printf("\r\n");
  }
  if(*p_comowner != '\0' && checkusername(p_comowner)){
    printf("Set-Cookie: comowner=%s;", p_comowner);
    if(g_sessionlife > 0) printf(" max-age=%d;", g_sessionlife);
    printf("\r\n");
  }
  if(g_eventcount > 0) printf("X-Event-Count: %lu\r\n", g_eventcount);
  printf("\r\n");
  fwrite(tmplstr, 1, strlen(tmplstr), stdout);
  fflush(stdout);
}


/* set a database error message */
static void setdberrmsg(TCLIST *emsgs, TCTDB *tdb, const char *msg){
  tclistprintf(emsgs, "[database error: %s] %s", tctdberrmsg(tctdbecode(tdb)), msg);
}


/* set the HTML data of an article */
static void setarthtml(TCMPOOL *mpool, TCMAP *cols, int64_t id, int bhl, bool tiny){
  if(g_scrextproc && scrextcheckfunc(g_scrextproc, "_procart")){
    TCXSTR *wiki = tcmpoolxstrnew(mpool);
    wikidump(wiki, cols);
    char *obuf = tcmpoolpushptr(mpool,
                                scrextcallfunc(g_scrextproc, "_procart", tcxstrptr(wiki)));
    if(obuf){
      tcmapclear(cols);
      wikiload(cols, obuf);
    }
  }
  tcmapprintf(cols, "id", "%lld", (long long)id);
  char idbuf[NUMBUFSIZ];
  sprintf(idbuf, "article%lld", (long long)id);
  char numbuf[NUMBUFSIZ];
  const char *rp = tcmapget2(cols, "cdate");
  if(rp){
    tcdatestrwww(tcstrmktime(rp), INT_MAX, numbuf);
    tcmapput2(cols, "cdate", numbuf);
    tcmapput2(cols, "cdatesimple", datestrsimple(numbuf));
  }
  rp = tcmapget2(cols, "mdate");
  if(rp){
    tcdatestrwww(tcstrmktime(rp), INT_MAX, numbuf);
    tcmapput2(cols, "mdate", numbuf);
    tcmapput2(cols, "mdatesimple", datestrsimple(numbuf));
  }
  rp = tcmapget2(cols, "xdate");
  if(rp){
    tcdatestrwww(tcstrmktime(rp), INT_MAX, numbuf);
    tcmapput2(cols, "xdate", numbuf);
    tcmapput2(cols, "xdatesimple", datestrsimple(numbuf));
  }
  rp = tcmapget2(cols, "tags");
  if(rp){
    TCLIST *tags = tcmpoolpushlist(mpool, tcstrsplit(rp, " ,"));
    int idx = 0;
    while(idx < tclistnum(tags)){
      rp = tclistval2(tags, idx);
      if(*rp != '\0'){
        idx++;
      } else {
        tcfree(tclistremove2(tags, idx));
      }
    }
    tcmapputlist(cols, "taglist", tags);
  }
  rp = tcmapget2(cols, "text");
  if(rp && *rp != '\0'){
    if(tiny){
      TCXSTR *xstr = tcmpoolxstrnew(mpool);
      wikitotext(xstr, rp);
      char *str = tcmpoolpushptr(mpool, tcmemdup(tcxstrptr(xstr), tcxstrsize(xstr)));
      tcstrutfnorm(str, TCUNSPACE);
      tcstrcututf(str, 256);
      tcmapput2(cols, "texttiny", str);
    } else {
      TCXSTR *xstr = tcmpoolxstrnew(mpool);
      wikitohtml(xstr, rp, idbuf, g_scriptname, bhl + 1, g_uploadpub);
      if(tcxstrsize(xstr) > 0) tcmapput(cols, "texthtml", 8, tcxstrptr(xstr), tcxstrsize(xstr));
    }
  }
  rp = tcmapget2(cols, "comments");
  if(rp && *rp != '\0'){
    TCLIST *lines = tcmpoolpushlist(mpool, tcstrsplit(rp, "\n"));
    int cnum = tclistnum(lines);
    tcmapprintf(cols, "comnum", "%d", cnum);
    TCLIST *comments = tcmpoolpushlist(mpool, tclistnew2(cnum));
    int cnt = 0;
    for(int i = 0; i < cnum; i++){
      const char *rp = tclistval2(lines, i);
      char *co = strchr(rp, '|');
      if(co){
        *(co++) = '\0';
        char *ct = strchr(co, '|');
        if(ct){
          *(ct++) = '\0';
          TCMAP *comment = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
          char numbuf[NUMBUFSIZ];
          tcdatestrwww(tcatoi(rp), INT_MAX, numbuf);
          cnt++;
          tcmapprintf(comment, "cnt", "%d", cnt);
          tcmapput2(comment, "date", numbuf);
          tcmapput2(comment, "datesimple", datestrsimple(numbuf));
          tcmapput2(comment, "owner", co);
          tcmapput2(comment, "text", ct);
          TCXSTR *xstr = tcmpoolxstrnew(mpool);
          wikitohtmlinline(xstr, ct, g_scriptname, g_uploadpub);
          tcmapput(comment, "texthtml", 8, tcxstrptr(xstr), tcxstrsize(xstr));
          tclistpushmap(comments, comment);
        }
      }
    }
    if(tclistnum(comments) > 0) tcmapputlist(cols, "comments", comments);
    tcmapprintf(cols, "comnum", "%d", tclistnum(comments));
  }
}


/* search for articles */
static TCLIST *searcharts(TCMPOOL *mpool, TCTDB *tdb, const char *cond, const char *expr,
                          const char *order, int max, int skip, bool ls){
  TDBQRY *qrys[8];
  int qnum = 0;
  if(!cond) cond = "";
  if(!expr) expr = "";
  if(*expr == '\0'){
    qrys[qnum++] = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
  } else if(!strcmp(cond, "main")){
    const char *names[] = { "name", "text", NULL };
    for(int i = 0; names[i] != NULL; i++){
      TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
      tctdbqryaddcond(qry, names[i], TDBQCFTSEX, expr);
      qrys[qnum++] = qry;
    }
  } else if(!strcmp(cond, "name")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "name", TDBQCSTREQ, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "namebw")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "name", TDBQCSTRBW, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "namefts")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "name", TDBQCFTSEX, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "cdate")){
    int64_t lower, upper;
    getdaterange(expr, &lower, &upper);
    char numbuf[NUMBUFSIZ*2];
    sprintf(numbuf, "%lld,%lld", (long long)lower, (long long)upper);
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "cdate", TDBQCNUMBT, numbuf);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "mdate")){
    int64_t lower, upper;
    getdaterange(expr, &lower, &upper);
    char numbuf[NUMBUFSIZ*2];
    sprintf(numbuf, "%lld,%lld", (long long)lower, (long long)upper);
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "mdate", TDBQCNUMBT, numbuf);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "xdate")){
    int64_t lower, upper;
    getdaterange(expr, &lower, &upper);
    char numbuf[NUMBUFSIZ*2];
    sprintf(numbuf, "%lld,%lld", (long long)lower, (long long)upper);
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "xdate", TDBQCNUMBT, numbuf);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "owner")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "owner", TDBQCSTREQ, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "ownerbw")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "", TDBQCSTRBW, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "ownerfts")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "", TDBQCFTSEX, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "tags")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "tags", TDBQCSTRAND, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "tagsor")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "tags", TDBQCSTROR, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "tagsfts")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "tags", TDBQCFTSEX, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "text")){
    TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
    tctdbqryaddcond(qry, "text", TDBQCFTSEX, expr);
    qrys[qnum++] = qry;
  } else if(!strcmp(cond, "any")){
    const char *names[] = { "name", "owner", "tags", "text", NULL };
    for(int i = 0; names[i] != NULL; i++){
      TDBQRY *qry = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
      tctdbqryaddcond(qry, names[i], TDBQCFTSEX, expr);
      qrys[qnum++] = qry;
    }
  } else {
    qrys[qnum++] = tcmpoolpush(mpool, tctdbqrynew(tdb), (void (*)(void *))tctdbqrydel);
  }
  if(!order) order = "";
  const char *oname = "cdate";
  int otype = TDBQONUMDESC;
  if(!strcmp(order, "_cdate")){
    oname = "cdate";
    otype = TDBQONUMASC;
  } else if(!strcmp(order, "mdate")){
    oname = "mdate";
    otype = TDBQONUMDESC;
  } else if(!strcmp(order, "_mdate")){
    oname = "mdate";
    otype = TDBQONUMASC;
  } else if(!strcmp(order, "xdate")){
    oname = "xdate";
    otype = TDBQONUMDESC;
  } else if(!strcmp(order, "_xdate")){
    oname = "xdate";
    otype = TDBQONUMASC;
  }
  for(int i = 0; i < qnum; i++){
    if(ls) tctdbqryaddcond(qrys[i], "tags", TDBQCSTROR | TDBQCNEGATE, "?");
    tctdbqrysetorder(qrys[i], oname, otype);
  }
  for(int i = 0; i < qnum; i++){
    tctdbqrysetlimit(qrys[i], max, skip);
  }
  TCLIST *res = tcmpoolpushlist(mpool, tctdbmetasearch(qrys, qnum, TDBMSUNION));
  return res;
}


/* get the range of a date expression */
static void getdaterange(const char *expr, int64_t *lowerp, int64_t *upperp){
  while(*expr == ' '){
    expr++;
  }
  unsigned int year = 0;
  for(int i = 0; i < 4 && *expr >= '0' && *expr <= '9'; i++){
    year = year * 10 + *expr - '0';
    expr++;
  }
  if(*expr == '-' || *expr == '/') expr++;
  unsigned int month = 0;
  for(int i = 0; i < 2 && *expr >= '0' && *expr <= '9'; i++){
    month = month * 10 + *expr - '0';
    expr++;
  }
  if(*expr == '-' || *expr == '/') expr++;
  unsigned int day = 0;
  for(int i = 0; i < 2 && *expr >= '0' && *expr <= '9'; i++){
    day = day * 10 + *expr - '0';
    expr++;
  }
  int lag = tcjetlag() / 3600;
  int64_t lower, upper;
  char numbuf[NUMBUFSIZ*2];
  if(day > 0){
    sprintf(numbuf, "%04u-%02u-%02uT00:00:00%+03d:00", year, month, day, lag);
    lower = tcstrmktime(numbuf);
    upper = lower + 60 * 60 * 24 - 1;
  } else if(month > 0){
    sprintf(numbuf, "%04u-%02u-01T00:00:00%+03d:00", year, month, lag);
    lower = tcstrmktime(numbuf);
    month++;
    if(month > 12){
      year++;
      month = 1;
    }
    sprintf(numbuf, "%04u-%02u-01T00:00:00%+03d:00", year, month, lag);
    upper = tcstrmktime(numbuf) - 1;
  } else if(year > 0){
    sprintf(numbuf, "%04u-01-01T00:00:00%+03d:00", year, lag);
    lower = tcstrmktime(numbuf);
    year++;
    sprintf(numbuf, "%04u-01-01T00:00:00%+03d:00", year, lag);
    upper = tcstrmktime(numbuf) - 1;
  } else {
    lower = INT64_MIN / 2;
    upper = INT64_MAX / 2;
  }
  *lowerp = lower;
  *upperp = upper;
}


/* store a file */
static bool putfile(TCMPOOL *mpool, const char *path, const char *name,
                    const char *ptr, int size){
  if(*path != '\0'){
    if(strchr(path, '/')) return false;
  } else {
    char *enc = pathencode(name);
    path = tcmpoolpushptr(mpool, tcsprintf("%lld-%s", (long long)tctime(), enc));
    tcfree(enc);
  }
  path = tcmpoolpushptr(mpool, tcsprintf("%s/%s", g_upload, path));
  return tcwritefile(path, ptr, size);
}


/* remove a file */
static bool outfile(TCMPOOL *mpool, const char *path){
  if(*path == '\0' || strchr(path, '/')) return false;
  path = tcmpoolpushptr(mpool, tcsprintf("%s/%s", g_upload, path));
  return remove(path) == 0;
}


/* search for files */
static TCLIST *searchfiles(TCMPOOL *mpool, const char *expr, const char *order,
                           int max, int skip, bool thum){
  TCLIST *paths = tcmpoolpushlist(mpool, tcreaddir(g_upload));
  if(!paths) return tcmpoollistnew(mpool);
  tclistsort(paths);
  if(strcmp(order, "_cdate")) tclistinvert(paths);
  int pnum = tclistnum(paths);
  TCLIST *files = tcmpoolpushlist(mpool, tclistnew2(pnum));
  for(int i = 0; i < pnum && tclistnum(files) < max; i++){
    const char *path = tclistval2(paths, i);
    int64_t date = tcatoi(path);
    const char *pv = strchr(path, '-');
    if(date < 1 || !pv) continue;
    pv++;
    int nsiz;
    char *name = tcmpoolpushptr(mpool, tcurldecode(pv, &nsiz));
    if(*expr != '\0' && !strstr(name, expr)){
      tcmpoolpop(mpool, true);
      continue;
    }
    if(--skip >= 0){
      tcmpoolpop(mpool, true);
      continue;
    }
    char lpath[8192];
    snprintf(lpath, sizeof(lpath) - 1, "%s/%s", g_upload, path);
    lpath[sizeof(lpath)-1] = '\0';
    int64_t size;
    if(!tcstatfile(lpath, NULL, &size, NULL)) size = 0;
    char numbuf[NUMBUFSIZ];
    tcdatestrwww(date, INT_MAX, numbuf);
    TCMAP *file = tcmpoolpushmap(mpool, tcmapnew2(TINYBNUM));
    tcmapput2(file, "path", path);
    tcmapput2(file, "name", name);
    tcmapput2(file, "date", datestrsimple(numbuf));
    tcmapprintf(file, "size", "%lld", (long long)size);
    const char *type = mimetype(name);
    if(!type) type = "application/octet-stream";
    tcmapput2(file, "type", type);
    tcmapput2(file, "typename", mimetypename(type));
    if(thum && tcstrfwm(type, "image/") && !strchr(type, '+'))
      tcmapput2(file, "thumnail", "true");
    tclistpushmap(files, file);
  }
  return files;
}


/* compare two comments by date */
static int comparecomments(const TCLISTDATUM *a, const TCLISTDATUM *b){
  COMMENT *coma = (COMMENT *)a->ptr;
  COMMENT *comb = (COMMENT *)b->ptr;
  if(coma->date > comb->date) return -1;
  if(coma->date < comb->date) return 1;
  if(coma->id > comb->id) return -1;
  if(coma->id < comb->id) return 1;
  return strcmp(coma->owner, comb->owner);
}


/* process the update command */
static bool doupdatecmd(TCMPOOL *mpool, const char *mode, const char *baseurl, const char *user,
                        double now, int64_t id, TCMAP *ncols, TCMAP *ocols){
  const char *base = g_upload;
  if(!base || *base == '\0') base = P_tmpdir;
  if(!base || *base == '\0') base = "/tmp";
  int64_t ts = now * 1000000;
  bool err = false;
  TCXSTR *nbuf = tcmpoolxstrnew(mpool);
  if(ncols){
    tcmapprintf(ncols, "id", "%lld", (long long)id);
    wikidump(nbuf, ncols);
  }
  char *npath = tcmpoolpushptr(mpool, tcsprintf("%s/tmp-%lld-%lld-%s-new.tpw", base,
                                                (long long)id, (long long)ts, mode));
  if(!tcwritefile(npath, tcxstrptr(nbuf), tcxstrsize(nbuf))) err = true;
  TCXSTR *obuf = tcmpoolxstrnew(mpool);
  if(ocols){
    tcmapprintf(ocols, "id", "%lld", (long long)id);
    wikidump(obuf, ocols);
  }
  char *opath = tcmpoolpushptr(mpool, tcsprintf("%s/tmp-%lld-%lld-%s-old.tpw", base,
                                                (long long)id, (long long)ts, mode));
  if(!tcwritefile(opath, tcxstrptr(obuf), tcxstrsize(obuf))) err = true;
  char idbuf[NUMBUFSIZ];
  sprintf(idbuf, "%lld", (long long)id);
  char tsbuf[NUMBUFSIZ];
  sprintf(tsbuf, "%lld", (long long)ts);
  const char *args[16];
  int anum = 0;
  args[anum++] = g_updatecmd;
  args[anum++] = mode;
  args[anum++] = idbuf;
  args[anum++] = npath;
  args[anum++] = opath;
  args[anum++] = tsbuf;
  args[anum++] = user;
  args[anum++] = baseurl;
  if(tcsystem(args, anum) != 0) err = true;
  remove(opath);
  remove(npath);
  return !err;
}



// END OF FILE
