#!/usr/bin/env zsh

while read line; do
    if [[ ${line:0:1} == 'q' ]]; then
        print -n "c"
        sleep 1
        print "item1"
        print "item1"
        sleep 1
        print "item2"
        print "item2"
        sleep 1
        print "item3"
        print "item3"
        print -n $'\0'
    fi
done
