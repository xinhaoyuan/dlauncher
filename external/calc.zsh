#!/usr/bin/env zsh

while read line; do
    if [[ ${line:0:1} == 'q' ]]; then
        print -n "c"
        if [[ ${line:1:1} == '?' ]]; then
            r=$(print "${line:2}" | bc -l 2>/dev/null)
            if [[ -n "$r" ]]; then
                print "$r"
                print "?$r"
            fi
        fi
        print -n $'\0'
    elif [[ ${line:0:1} == 'o' ]]; then
        r=$(print "${line:2}" | bc -l 2>/dev/null)
        print -n "$r" | xclip -i
    elif [[ ${line:0:1} == 'O' ]]; then
        r=$(print "${line:2}" | bc -l 2>/dev/null)
        print -n "$r" | xclip -i -selection clipboard
    fi
done
