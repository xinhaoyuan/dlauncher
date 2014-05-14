#!/usr/bin/env zsh

while read line; do
    # process queries only
    if [[ ${line:0:1} == 'q' ]]; then
        print "clear"
        if [[ ${line:1:1} == '?' ]]; then
            r=$(print "${line:2}" | bc -l 2>/dev/null)
            if [[ -n "$r" ]]; then
                print "$r"
                print "?$r"
            fi
        fi
        print -n $'\0'
    fi
done
