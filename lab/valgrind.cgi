#! /bin/sh

#================================================================
# valgrind.cgi
# Check memory error of the CGI script
#================================================================


# change the current directory
cd ..


# set variables
LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$HOME/lib:/usr/local/lib"
PATH="$PATH:$HOME/bin:/usr/local/bin"
SCRIPT_FILENAME="$PWD/promenade.cgi"
LANG="C"
LC_ALL="C"
export LD_LIBRARY_PATH PATH SCRIPT_FILENAME LANG LC_ALL


# execute the CGI script
valgrind --log-file='%p.vlog' --leak-check=full ./promenade.cgi


# exit normally
exit 0



# END OF FILE
