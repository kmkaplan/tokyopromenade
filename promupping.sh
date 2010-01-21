#! /bin/sh

#================================================================
# promupping
# Notify site update to public subscription services
#================================================================


# configuration
mode="$1"
id="$2"
newfile="$3"
oldfile="$4"
ts="$5"
user="$6"
baseurl="$7"
curl="curl --silent --fail --connect-timeout 1 --max-time 1"


# set variables
LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/local/lib"
PATH="$PATH:/sbin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
LANG=C
LC_ALL=C
export LD_LIBRARY_PATH PATH LANG LC_ALL


# ping to Google blog search
pingurl="http://blogsearch.google.co.jp/ping"
ename=`printf '%s' "$TP_TITLE" | tcucodec url`
if [ "$mode" = "comment" ] ; then
    ehtmlurl=`printf '%s' "$baseurl?id=$id" | tcucodec url`
    eatomurl=`printf '%s' "$baseurl?format=atom&id=$id" | tcucodec url`
else
    ehtmlurl=`printf '%s' "$baseurl" | tcucodec url`
    eatomurl=`printf '%s' "$baseurl?format=atom&act=timeline&order=cdate" | tcucodec url`
fi
fullurl="$pingurl?name=$ename&url=$ehtmlurl&changesURL=$eatomurl"
$curl "$fullurl" >/dev/null 2>&1
[ "$?" = 0 ] || err="$err[google]"


# ping to Yahoo Japan blog search
pingurl="http://api.my.yahoo.co.jp/rss/ping"
if [ "$mode" = "comment" ] ; then
    eatomurl=`printf '%s' "$baseurl?format=atom&id=$id" | tcucodec url`
else
    eatomurl=`printf '%s' "$baseurl?format=atom&act=timeline&order=cdate" | tcucodec url`
fi
fullurl="$pingurl?u=$eatomurl"
$curl "$fullurl" >/dev/null 2>&1
[ "$?" = 0 ] || err="$err[yahooj]"


# ping to a hub of Pubsubhubbub
pingurl="http://pubsubhubbub.appspot.com/publish"
if [ "$mode" = "comment" ] ; then
    eatomurl=`printf '%s' "$baseurl?format=atom&id=$id" | tcucodec url`
else
    eatomurl=`printf '%s' "$baseurl?format=atom&act=timeline&order=cdate" | tcucodec url`
fi
$curl -X POST --data "hub.mode=publish" --data "hub.url=$eatomurl" \
  "$pingurl" >/dev/null 2>&1
[ "$?" = 0 ] || err="$err[pubsub]"


# exit normally
[ -z "$err" ] || exit 1
exit 0



# END OF FILE
