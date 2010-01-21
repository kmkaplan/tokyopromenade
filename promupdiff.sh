#! /bin/sh

#================================================================
# promupdiff
# Save diff data of articles
#================================================================


# configuration
basedir="./diff"
mode="$1"
id="$2"
newfile="$3"
oldfile="$4"
ts="$5"
user="$6"
url="$7"
outfile="$basedir/$id-$ts-$mode-$user.diff"


# set variables
LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/local/lib"
PATH="$PATH:/sbin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
LANG=C
LC_ALL=C
export LD_LIBRARY_PATH PATH LANG LC_ALL


# generate the diff data
mkdir -p "$basedir"
diff "$oldfile" "$newfile" > "$outfile"


# exit
[ -n "$outfile" ] || exit 1
exit 0



# END OF FILE
