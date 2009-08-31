/*************************************************************************************************
 * The Web interface of Tokyo Promenade
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

#define SALTNAME       "[salt]"          // dummy user name of the salt
#define ADMINNAME      "admin"           // user name of the administrator


/* global variables */
const char *g_scriptname;                // script name
const char *g_scriptprefix;              // script prefix
const char *g_scriptpath;                // script path
TCTMPL *g_tmpl;                          // template serializer
const char *g_database;                  // path of the database file
const char *g_password;                  // path of the password file
TCMAP *g_users;                          // user list
const char *g_upload;                    // path of the upload directory
int64_t g_recvmax;                       // maximum size of received data
const char *g_title;                     // site title
int g_searchnum;                         // number of articles in a search page
int g_listnum;                           // number of articles in a list page
int g_filenum;                           // number of files in a file list page
const char *g_commentmode;               // comment mode
const char *g_frontpage;                 // name of the front page


/* function prototypes */
int main(int argc, char **argv);
static void showerror(int code, const char *msg);
static void readpasswd(void);
static bool writepasswd(void);
static void eventloop(void);
static void dosession(TCMPOOL *mpool);
static void setdberrmsg(TCLIST *emsgs, TCTDB *tdb, const char *msg);
static void setarthtml(TCMPOOL *mpool, TCMAP *cols, int64_t id, int bhl, bool tiny);
static TCLIST *searcharts(TCMPOOL *mpool, TCTDB *tdb, const char *cond, const char *expr,
                          const char *order, int max, int skip);
static bool putfile(TCMPOOL *mpool, const char *path, const char *name,
                    const char *ptr, int size);
static bool outfile(TCMPOOL *mpool, const char *path);
static TCLIST *searchfiles(TCMPOOL *mpool, const char *expr, const char *order,
                           int max, int skip, bool thum);


/* main routine */
int main(int argc, char **argv){
  g_scriptname = getenv("SCRIPT_NAME");
  if(!g_scriptname) g_scriptname = argv[0];
  char *prefix = tcregexreplace(g_scriptname, "\\.[a-zA-Z0-9]*$", "");
  g_scriptprefix = prefix;
  g_scriptpath = getenv("SCRIPT_FILENAME");
  if(!g_scriptpath) g_scriptpath = argv[0];
  char *barepath = tcregexreplace(g_scriptpath, "\\.[a-zA-Z0-9]*$", "");
  char *tmplpath = tcsprintf("%s.tmpl", barepath);
  g_tmpl = tctmplnew();
  if(tctmplload2(g_tmpl, tmplpath)){
    g_database = tctmplconf(g_tmpl, "database");
    if(!g_database) g_database = "promenade.tct";
    g_password = tctmplconf(g_tmpl, "password");
    g_users = NULL;
    if(g_password){
      g_users = tcmapnew2(TINYBNUM);
      readpasswd();
    }
    g_upload = tctmplconf(g_tmpl, "upload");
    const char *rp = tctmplconf(g_tmpl, "recvmax");
    g_recvmax = rp ? tcatoix(rp) : INT_MAX;
    g_title = tctmplconf(g_tmpl, "title");
    if(!g_title) g_title = "Tokyo Promenade";
    rp = tctmplconf(g_tmpl, "searchnum");
    g_searchnum = tclmax(rp ? tcatoi(rp) : 10, 1);
    rp = tctmplconf(g_tmpl, "listnum");
    g_listnum = tclmax(rp ? tcatoi(rp) : 10, 1);
    rp = tctmplconf(g_tmpl, "filenum");
    g_filenum = tclmax(rp ? tcatoi(rp) : 10, 1);
    g_commentmode = tctmplconf(g_tmpl, "commentmode");
    if(!g_commentmode) g_commentmode = "";
    g_frontpage = tctmplconf(g_tmpl, "frontpage");
    if(!g_frontpage) g_frontpage = "";
    eventloop();
    if(g_users) tcmapdel(g_users);
  } else {
    showerror(500, "The template file is missing.");
  }
  tctmpldel(g_tmpl);
  tcfree(tmplpath);
  tcfree(barepath);
  tcfree(prefix);
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


/* event loop */
static void eventloop(void){
  TCMPOOL *mpool = tcmpoolnew();
  dosession(mpool);
  tcmpooldel(mpool);
}


/* process each session */
static void dosession(TCMPOOL *mpool){
  // download a file
  const char *rp = getenv("PATH_INFO");
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
    FILE *ifp = tcmpoolpush(mpool, fopen(path, "rb"), (void (*)(void *))fclose);
    if(!ifp){
      showerror(404, "The requested file is missing.");
      return;
    }
    printf("Content-Type: %s\r\n", mimetype(rp));
    printf("Cache-Control: no-cache\r\n");
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
  // read query parameters
  rp = getenv("CONTENT_LENGTH");
  bool post = false;
  if(rp){
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
  } else {
    const char *query = getenv("QUERY_STRING");
    if(query) tcwwwformdecode(query, params);
  }
  rp = getenv("HTTP_COOKIE");
  if(rp) tcwwwformdecode(rp, params);
  const char *p_user = tcstrskipspc(tcmapget4(params, "user", ""));
  const char *p_pass = tcstrskipspc(tcmapget4(params, "pass", ""));
  const char *p_act = tcstrskipspc(tcmapget4(params, "act", ""));
  int64_t p_id = tcatoi(tcmapget4(params, "id", ""));
  const char *p_name = tcstrskipspc(tcmapget4(params, "name", ""));
  const char *p_order = tcstrskipspc(tcmapget4(params, "order", ""));
  const char *p_expr = tcstrskipspc(tcmapget4(params, "expr", ""));
  const char *p_cond = tcstrskipspc(tcmapget4(params, "cond", ""));
  int p_page = tclmax(tcatoi(tcmapget4(params, "page", "")), 1);
  const char *p_wiki = tcstrskipspc(tcmapget4(params, "wiki", ""));
  bool p_mts = *tcmapget4(params, "mts", "") != '\0';
  const char *p_hash = tcstrskipspc(tcmapget4(params, "hash", ""));
  const char *p_comowner = tcstrskipspc(tcmapget4(params, "comowner", ""));
  const char *p_comtext = tcstrskipspc(tcmapget4(params, "comtext", ""));
  const char *p_ummode = tcstrskipspc(tcmapget4(params, "ummode", ""));
  const char *p_umname = tcstrskipspc(tcmapget4(params, "umname", ""));
  const char *p_uminfo = tcstrskipspc(tcmapget4(params, "uminfo", ""));
  const char *p_umpassone = tcstrskipspc(tcmapget4(params, "umpassone", "umpassone"));
  const char *p_umpasstwo = tcstrskipspc(tcmapget4(params, "umpasstwo", "umpasstwo"));
  const char *p_fmmode = tcstrskipspc(tcmapget4(params, "fmmode", ""));
  const char *p_fmpath = tcstrskipspc(tcmapget4(params, "fmpath", ""));
  const char *p_fmname = tcstrskipspc(tcmapget4(params, "fmname", ""));
  int p_fmfilesiz;
  const char *p_fmfilebuf = tcmapget(params, "fmfile", 6, &p_fmfilesiz);
  const char *p_fmfilename = tcstrskipspc(tcmapget4(params, "fmfile_filename", ""));
  bool p_fmthum = *tcmapget4(params, "fmthum", "") != '\0';
  bool p_confirm = *tcmapget4(params, "confirm", "") != '\0';
  // perform authentication
  bool auth = true;
  const char *userinfo = NULL;
  const char *authcookie = NULL;
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
        TCLIST *elems = tcmpoolpushlist(mpool, tcstrsplit(token, ":"));
        if(tclistnum(elems) >= 4 && !strcmp(tclistval2(elems, 0), salt)){
          p_user = tclistval2(elems, 1);
          p_pass = tclistval2(elems, 2);
          cont = true;
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
          int tsiz = strlen(p_user) + strlen(p_pass) + saltsiz + NUMBUFSIZ * 2;
          char token[tsiz];
          tsiz = sprintf(token, "%s:%s:%s:%lld", salt, p_user, p_pass, (long long)tctime());
          tcmd5hash(token, tsiz, numbuf);
          sprintf(token + tsiz, ":%s", numbuf);
          tcarccipher(token, tsiz, salt, saltsiz, token);
          if(!cont) authcookie = tcmpoolpushptr(mpool, tcurlencode(token, tsiz));
        }
      }
    }
  }
  if(!strcmp(p_act, "logout")){
    p_user = "";
    p_pass = "";
    auth = false;
    authcookie = "";
  }
  bool admin = auth && !strcmp(p_user, ADMINNAME);
  bool cancom = false;
  if(!strcmp(g_commentmode, "all")){
    cancom = true;
  } else if(!strcmp(g_commentmode, "login") && auth){
    cancom = true;
  }
  // open the database
  TCTDB *tdb = tcmpoolpush(mpool, tctdbnew(), (void (*)(void *))tctdbdel);
  int omode = TDBOREADER;
  if(!strcmp(p_act, "update") && auth && post) omode = TDBOWRITER;
  if(!strcmp(p_act, "comment") && cancom && post) omode = TDBOWRITER;
  if(!tctdbopen(tdb, g_database, omode))
    setdberrmsg(emsgs, tdb, "Opening the database was failed.");
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
            TCXSTR *wiki = tcmpoolxstrnew(mpool);
            wikidump(wiki, cols);
            TCXSTR *line = tcmpoolxstrnew(mpool);
            tcxstrprintf(line, "%lld|%s|%s\n", (long long)tctime(), owner, text);
            tcmapputcat(cols, "comments", 8, tcxstrptr(line), tcxstrsize(line));
            tcmapprintf(cols, "mdate", "%lld", (long long)tctime());
            if(!dbputart(tdb, p_id, cols))
              setdberrmsg(emsgs, tdb, "Storing the article was failed.");
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
    tcmapput2(vars, "view", "login");
  } else if(!strcmp(p_act, "logincheck") && !auth){
    // login view
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
      tcmapput2(vars, "view", "edit");
      if(*p_name != '\0') tcmapput2(vars, "name", p_name);
      if(*p_user != '\0') tcmapput2(vars, "user", p_user);
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
              if(p_mts) tcmapprintf(cols, "mdate", "%lld", (long long)tctime());
              const char *name = tcmapget2(cols, "name");
              if(!name || *name == '\0'){
                tclistprintf(emsgs, "The name can not be empty.");
              } else if(checkfrozen(cols) && !admin){
                tclistprintf(emsgs, "The frozen tag is not available by normal users.");
              } else {
                setarthtml(mpool, cols, p_id, 0, false);
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
      TCMAP *cols = tcmapnew2(TINYBNUM);
      wikiload(cols, p_wiki);
      if(p_mts){
        double now = tctime();
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
        tcmapput2(vars, "view", "preview");
        tcmapputmap(vars, "art", cols);
        tcmapput2(vars, "wiki", p_wiki);
        if(p_mts) tcmapput2(vars, "mts", "on");
      }
    }
  } else if(!strcmp(p_act, "update")){
    // update view
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
              const char *name = tcmapget2(cols, "name");
              if(!name || *name == '\0'){
                tclistprintf(emsgs, "The name can not be empty.");
              } else if(checkfrozen(cols) && !admin){
                tclistprintf(emsgs, "The frozen tag is not available by normal users.");
              } else if(dbputart(tdb, p_id, cols)){
                tcmapput2(vars, "view", "store");
                tcmapputmap(vars, "art", cols);
              } else {
                setdberrmsg(emsgs, tdb, "Storing the article was failed.");
              }
            } else {
              if(dboutart(tdb, p_id)){
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
      const char *name = tcmapget2(cols, "name");
      if(!name || *name == '\0'){
        tclistprintf(emsgs, "The name can not be empty.");
        tcmapput2(vars, "view", "edit");
        tcmapput2(vars, "wiki", p_wiki);
      } else if(dbputart(tdb, 0, cols)){
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
          if(!strcmp(p_ummode, "new")){
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
          } else {
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
        tcmapput2(vars, "view", "users");
        if(tclistnum(ulist) > 0) tcmapputlist(vars, "userlist", ulist);
        if(salt) tcmapput2(vars, "salt", salt);
      } else {
        tclistprintf(emsgs, "The user management function is not available by normal users.");
      }
    } else {
      tclistprintf(emsgs, "The password file is missing.");
    }
  } else if(!strcmp(p_act, "files")){
    // files view
    bool isdir;
    if(g_upload || !tcstatfile(g_upload, &isdir, NULL, NULL) || !isdir){
      if(auth){
        if(post && (p_fmfilebuf || *p_fmpath != '\0')){
          if(!strcmp(p_fmmode, "new")){
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
        }
        int max = g_filenum;
        int skip = g_filenum * (p_page - 1);
        TCLIST *files = searchfiles(mpool, p_expr, p_order, max + 1, skip, p_fmthum);
        bool over = false;
        if(tclistnum(files) > max){
          tcfree(tclistpop2(files));
          over = true;
        }
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
    TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, p_id));
    if(cols){
      setarthtml(mpool, cols, p_id, 0, false);
      if(checkfrozen(cols) && !admin){
        tcmapput2(cols, "frozen", "true");
      } else if(cancom){
        tcmapputkeep2(cols, "comnum", "0");
        tcmapputkeep2(cols, "cancom", "true");
      }
      const char *name = tcmapget2(cols, "name");
      if(name) tcmapprintf(vars, "subtitle", name);
      tcmapput2(vars, "view", "single");
      tcmapputmap(vars, "art", cols);
    } else {
      tcmapput2(vars, "view", "empty");
    }
    tcmapprintf(vars, "cond", "id:%lld", (long long)p_id);
  } else if(*p_name != '\0'){
    // single view or search view
    int max = g_searchnum;
    int skip = g_searchnum * (p_page - 1);
    TCLIST *res = searcharts(mpool, tdb, "name", p_name, p_order, max + 1, skip);
    int rnum = tclistnum(res);
    if(rnum < 1){
      tcmapput2(vars, "view", "empty");
      tcmapput2(vars, "missname", p_name);
    } else if(rnum < 2){
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
        if(name) tcmapprintf(vars, "subtitle", name);
        tcmapput2(vars, "view", "single");
        tcmapputmap(vars, "art", cols);
      } else {
        tcmapput2(vars, "view", "empty");
      }
    } else {
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
        tcmapprintf(vars, "subtitle", "[name:%s]", p_name);
        tcmapput2(vars, "view", "search");
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
    tcmapput2(vars, "view", "search");
    if(*p_cond != '\0'){
      int max = g_searchnum;
      int skip = g_searchnum * (p_page - 1);
      TCLIST *res = searcharts(mpool, tdb, p_cond, p_expr, p_order, max + 1, skip);
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
        if(*p_expr != '\0'){
          tcmapprintf(vars, "subtitle", "[search:%s]", p_expr);
        } else {
          tcmapprintf(vars, "subtitle", "[search]", p_expr);
        }
        if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
        if(rnum > max) tcmapprintf(vars, "next", "%d", p_page + 1);
        if(tcmapget2(vars, "prev") || tcmapget2(vars, "next")) tcmapput2(vars, "page", "true");
        tcmapputlist(vars, "arts", arts);
      }
      tcmapprintf(vars, "resnum", "%d", rnum);
    }
  } else if(*g_frontpage != '\0'){
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
      TCLIST *res = searcharts(mpool, tdb, "name", name, "_cdate", 1, 0);
      if(tclistnum(res) > 0) id = tcatoi(tclistval2(res, 0));
    }
    tcmapput2(vars, "view", "front");
    if(id > 0){
      TCMAP *cols = tcmpoolpushmap(mpool, dbgetart(tdb, id));
      if(cols){
        setarthtml(mpool, cols, id, 0, false);
        if(checkfrozen(cols) && !admin) tcmapput2(cols, "frozen", "true");
        tcmapputmap(vars, "art", cols);
      }
    }
  } else {
    // list view
    int max = g_listnum;
    int skip = g_listnum * (p_page - 1);
    TCLIST *res = searcharts(mpool, tdb, NULL, NULL, p_order, max + 1, skip);
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
        tcmapput2(vars, "view", "list");
        if(p_page > 1) tcmapprintf(vars, "prev", "%d", p_page - 1);
        if(rnum > max) tcmapprintf(vars, "next", "%d", p_page + 1);
        if(tcmapget2(vars, "prev") || tcmapget2(vars, "next")) tcmapput2(vars, "page", "true");
        tcmapputlist(vars, "arts", arts);
      } else {
        tcmapput2(vars, "view", "empty");
      }
    }
  }
  if(!tctdbclose(tdb)) setdberrmsg(emsgs, tdb, "Closing the database was failed.");
  if(tclistnum(emsgs) > 0) tcmapputlist(vars, "emsgs", emsgs);
  tcmapputmap(vars, "params", params);
  if(tcxstrsize(comquery) > 0) tcmapput2(vars, "comquery", tcxstrptr(comquery));
  tcmapput2(vars, "scriptname", g_scriptname);
  tcmapput2(vars, "scriptprefix", g_scriptprefix);
  tcmapput2(vars, "scriptpath", g_scriptpath);
  if(g_users) tcmapputmap(vars, "users", g_users);
  if(auth && p_user != '\0'){
    tcmapput2(vars, "username", p_user);
    if(userinfo && *userinfo != '\0') tcmapput2(vars, "userinfo", userinfo);
  }
  if(auth) tcmapput2(vars, "auth", "true");
  if(admin) tcmapput2(vars, "admin", "true");
  if(cancom) tcmapput2(vars, "cancom", "true");
  tcmapput2(vars, "tpversion", TPVERSION);
  char numbuf[NUMBUFSIZ];
  tcdatestrwww(INT64_MAX, INT_MAX, numbuf);
  tcmapput2(vars, "now", numbuf);
  if(tcmapget2(vars, "prev") || tcmapget2(vars, "next")) tcmapput2(vars, "page", "true");
  tcmapputkeep2(vars, "view", "error");
  const char *tmplstr = tcmpoolpushptr(mpool, tctmpldump(g_tmpl, vars));
  printf("Content-Type: text/html; charset=UTF-8\r\n");
  printf("Cache-Control: no-cache\r\n");
  if(authcookie)
    printf("Set-Cookie: auth=%s; max-age=%d; path=%s\r\n", authcookie, 3600 * 24, g_scriptname);
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
      wikitohtml(xstr, rp, idbuf, g_scriptname, bhl + 1);
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
          wikitohtmlinline(xstr, ct, g_scriptname);
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
                          const char *order, int max, int skip){
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
  for(int i = 0; i < qnum; i++){
    if(!strcmp(order, "_cdate")){
      tctdbqrysetorder(qrys[i], "cdate", TDBQONUMASC);
    } else if(!strcmp(order, "mdate")){
      tctdbqrysetorder(qrys[i], "mdate", TDBQONUMDESC);
    } else if(!strcmp(order, "_mdate")){
      tctdbqrysetorder(qrys[i], "mdate", TDBQONUMASC);
    } else {
      tctdbqrysetorder(qrys[i], "cdate", TDBQONUMDESC);
    }
  }
  TCLIST *res;
  if(qnum < 2){
    tctdbqrysetlimit(qrys[0], max, skip);
    res = tcmpoolpushlist(mpool, tctdbqrysearch(qrys[0]));
  } else {
    for(int i = 0; i < qnum; i++){
      tctdbqrysetlimit(qrys[i], max + skip, 0);
    }
    res = tcmpoolpushlist(mpool, tctdbmetasearch(qrys, qnum, TDBMSUNION));
    skip = tclmin(skip, tclistnum(res));
    for(int i = 0; i < skip; i++){
      tcfree(tclistshift2(res));
    }
  }
  return res;
}


/* store a file */
static bool putfile(TCMPOOL *mpool, const char *path, const char *name,
                    const char *ptr, int size){
  if(*path != '\0'){
    if(strchr(path, '/')) return false;
  } else {
    path = tcmpoolpushptr(mpool, tcsprintf("%lld-%?", (long long)tctime(), name));
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
    tcmapput2(file, "type", type);
    tcmapput2(file, "typename", mimetypename(type));
    if(thum && tcstrfwm(type, "image/")) tcmapput2(file, "thumnail", "true");
    tclistpushmap(files, file);
  }
  return files;
}



// END OF FILE
