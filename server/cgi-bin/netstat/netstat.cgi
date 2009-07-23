#!/bin/sh

#################################################################
#
# name:    netstat port statistics writer
# version: 2.1
# date:    February 24, 2002
# author:  Devin Teske
#
# dependencies:
#
#      netstat   uname   grep   tr   sed   ifconfig
#
#################################################################

######################### Configuration options

# To configure nspsw edit the config file

config="./netstat.conf";

######################### Begin script

# execute the configuration file
source $config;

exr=$0;

# strip the execution path down to the binary name
while [ "`echo $exr | grep ./`" != "" ]; do
   exr=`echo $exr | sed -e s/.'\/'/'\/'/g`;
done
exr=`echo $exr | tr -d '/'`;

# check to see if the configure file has been modified
# (the user must modify the config file before using)
if [ "$device" = "" ]; then
   printf "$exr: you must configure \`$config'";
   exit 0;
fi

# use netstat to look up the active connections
case `uname` in
Darwin)
   # BSD standards
   ns=`/usr/sbin/netstat -n -f inet`;
   address=`/sbin/ifconfig $device  \
      | grep inet             \
      | awk '{print $2}'`;;
FreeBSD)
   # BSD standards
   ns=`/usr/bin/netstat -n -f inet`;
   address=`ifconfig $device  \
      | grep inet             \
      | awk '{print $2}'`;;
Linux)
   # GNU standards
   ns=`netstat -nt`;
   address=`/sbin/ifconfig eth0     \
      | grep inet             \
      | awk '{print $2}'      \
      | sed s/addr://`;;
OSF1)
   # Alpha system. BSD standards
   ns=`/usr/sbin/netstat -n -f inet`;
   address=`ifconfig $device  \
      | grep inet             \
      | awk '{print $2}'`;;
SunOS)
   # Solaris. BSD standards
   ns=`/usr/ucb/netstat -n -f inet`;
   address=`ifconfig $device  \
      | grep inet             \
      | awk '{print $2}'`;;
*)
   # assume GNU standards for everything else
   ns=`netstat -nt`;;
   address=`/sbin/ifconfig eth0     \
      | grep inet             \
      | awk '{print $2}'      \
      | sed s/addr://`;;
esac

# exit if we could not determine address
if [ "$address" = "" ]; then
   printf "$exr: could not determine address of device";
   exit 0;
fi

# get a list of only established connections
nt=`echo "$ns" | grep "EST"`;

# find the row that contains the header
head=`echo "$ns" | grep -i "Send-Q"`;

# replace column names that have two words with one word
head=`echo "$head" | sed -e s/Local\ Address/Local_Address/`;
head=`echo "$head" | sed -e s/Foreign\ Address/Foreign_Address/`;

# find the parameter index value of specified text
# >syntax: get_index <var> <search_arg> <search_text>
# >params:
#   var -> name of variable to store result in (byref)
#   seach_arg -> the text to look for
#   search_text -> the text to look in
#
function get_index () {
   local _key _var _index _word;
   _var=$1; shift 1; _key=$1; shift 1; _index=0;
   for _word in $@; do
      (( _index = _index + 1 ));
      [ "$_word" = "$_key" ] && break;
   done
   eval $_var=$_index;
}

# get the total number of words in a string
# >syntax: get_wordtotal <var> <string>
# >params:
#   var -> name of variable to store result in (byref)
#   string -> string to count words in
#
function get_wordtotal () {
   local _var;
   _var=$1; shift 1;
   eval $_var=$#;
}

# count the number of active transactions from netstat
# >syntax: count_transfers <var> <address> <port> <type>
# >params:
#   var -> name of variable to store result in (byref)
#   address -> local address to read for
#   port -> the port to look at
#   type -> 0 = no send data, 1 = send data
#
function count_transfers () {
   local sq la wt x y sqval laval curval;

   get_index "sq" Send-Q $head;
   get_index "la" Local_Address $head;
   get_wordtotal "wt" $head;

   x=0; y=0; z=0;
   for curval in `echo "$nt" | grep $2[:.]$3`; do
      if [ $x -eq 0 ]; then
         sqval=""; laval="";
      fi
      [ $x -lt $wt ] && ((x=x+1));
      [ $x -eq $sq ] && sqval=$curval;
      [ $x -eq $la ] && laval=$curval;

      if [ $x -eq $wt ]; then
         if [ "$laval" = "$2:$3" -o "$laval" = "$2.$3" ]; then
            [ "$sqval"  = 0 -a $4 -eq 0 ] && ((y=y+1));
            [ "$sqval" != 0 -a $4 -ne 0 ] && ((z=z+1));
	 fi
      fi
      [ $x -eq $wt ] && x=0;
   done

   [ $4 -eq 0 ] && eval $1=$y;
   [ $4 -eq 1 ] && eval $1=$z;
}

# do the actual counting
count_transfers "connected" $address $uport  0;
count_transfers "downloads" $address $txport 1;
count_transfers "uploads"   $address $txport 0;

# display zeroes if we couldn't obtain values
[ "$connected" = "" ] && connected=0;
[ "$downloads" = "" ] && downloads=0;
[ "$uploads"   = "" ] && uploads=0;

# re-execute the conf file so it now knows these values
source $config;

# output the results
printf "$format";

















# old source
# ---------------------------------------------------------------
# 
# # find out how manu users are connected
# connected=`echo "$ns" | grep "EST" | grep -i -c "[:.]$uport "`;
# 
# # get a list of running file transfers
# transfers=`echo "$ns" | grep "EST" | grep -i "[:.]$txport "`;
# 
# # find the column that contains "Send-Q" data
# head=`echo "$ns" | grep -i "Send-Q"`;
# 
# # replace column names that have two words with one word
# head=`echo "$head" | sed -e s/Local\ Address/L_Addr/`;
# head=`echo "$head" | sed -e s/Foreign\ Address/F_Addr/`;
# 
# # count columns and find Send-Q
# sq=0; ht=0; y=0;
# for col in $head; do
#    if [ $y -eq 0 ]; then
#       ((sq=$sq+1));
#    fi
#    if [ "$col" = "Send-Q" -a $y -eq 0 ]; then
#       y=1;
#    fi
#    ((ht=$ht+1));
# done
# 
# # set initial variable values
# downloads=0; uploads=0; z=0;
# 
# # loop through list of transfers counting up/down loads
# for col in $transfers; do
#    if [ $z -lt $ht ]; then
#       ((z=$z+1));
#    else
#       z=1;
#    fi
#    if [ $z -eq $sq ]; then
#       if [ "$col" = "0" ]; then
#          ((uploads=$uploads+1));
#       else
#          ((downloads=$downloads+1));
#       fi
#    fi
# done
# 
# # re-execute the conf file so it now knows these values
# source $config;
# 
# # output the results
# printf "$format";
