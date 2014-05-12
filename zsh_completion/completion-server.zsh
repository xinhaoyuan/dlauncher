#!/usr/bin/env zsh
# Based on:
# https://github.com/Valodim/zsh-capture-completion/blob/master/capture.zsh

# read everything until a line containing the byte 0 is found
read-to-null() {
	while zpty -r z chunk; do
		[[ $chunk == *$'\0'* ]] && break
		[[ $chunk != $'\1'* ]] && continue # ignore what doesnt start with '1'
		print -r -n - ${chunk:1}
	done
}

last-word() {
    local input=$1
    integer i=0
    integer m=0
    integer s=0
    while [[ $i -lt ${#input} ]]; do 
        if [[ ${input:$i:1} == '\' && $m != 2 ]]; then i=$i + 1
        elif [[ ${input:$i:1} == '"' ]]; then
            if [[ $m == 0 ]]; then 
                m=1
                s=$(( $i + 1 ))
            elif [[ $m == 1 ]]; then
                m=0
            fi
        elif [[ ${input:$i:1} == "'" ]]; then
            if [[ $m == 0 ]]; then 
                m=2
                s=$(( $i + 1 ))
            elif [[ $m == 2 ]]; then
                m=0
            fi
        elif [[ ${input:$i:1} == ' ' && $m == 0 ]]; then s=$(( $i + 1 ))
        fi
        i=$(( $i + 1 ))
    done
    print $s
}

accept-connection() {
	zsocket -a $server
	fds[$REPLY]=1
    cached_input[$REPLY]=$'\0'
	print "connection accepted, fd: $REPLY" >&2
}

handle-request() {
	local connection=$1 current line input cached
	integer read_something=0
	print "request received from fd $connection"
	while IFS= read -r -u $connection prefix &> /dev/null; do
		read_something=1
        if [[ ${prefix[1]} == 'o' ]]; then
            # execute the command
            sh -c "${prefix:1}" &
        elif [[ ${prefix[1]} == 'O' ]]; then
            urxvt -e sh -c "${prefix:1}" &
        else
		    # send the prefix to be completed followed by a TAB to force
		    # completion
            input="${prefix:1}"
            cached="$cached_input[$connection]"
            lw=$(last-word "$input")
            
            if [[ -n "$input" && "${input:0:${#cached}}" == "$cached" && $lw -le ${#cached} ]]; then
                print -u $connection "filter"
                print -n -u $connection $'\0'
                break;
            fi

            print -u $connection "clear"
            if [[ -n "$input" ]]; then
                cached_input[$connection]=$input
		        zpty -w -n z "$input"
                zpty -w -n z $'\t'
		        zpty -r z chunk &> /dev/null # read empty line before completions
		        read-to-null | while IFS= read -r line; do
                    if (( $#line )); then
                        print -r -u $connection - ${line:0:-1}
                        print -r -u $connection - "${input:0:$lw}${line:0:-1}"
                    fi
		        done
            else
                cached_input[$connection]=$'\0'
            fi

            # empty line to end
		    print -n -u $connection $'\0'
		    # clear input buffer
		    zpty -w z $'\n'
		    break # handle more requests/return to zselect
        fi
	done
	if ! (( read_something )); then
		print "connection with fd $connection closed" >&2
	  unset fds[$connection]
		exec {connection}>&- # free the file descriptor
	fi
}


if [[ -n $ZLE_AUTOSUGGEST_SERVER_LOG ]]; then
	exec >> "$HOME/.autosuggest-server.log"
else
	exec > /dev/null
fi

if [[ -n $ZLE_AUTOSUGGEST_SERVER_LOG_ERRORS ]]; then
	exec 2>> "$HOME/.autosuggest-server-errors.log"
else
	exec 2> /dev/null
fi

exec < /dev/null

zmodload zsh/zpty
zmodload zsh/zselect
zmodload zsh/net/socket
setopt noglob
print "autosuggestion server started, pid: $$" >&2

# Start an interactive zsh connected to a zpty
zpty z ZLE_DISABLE_AUTOSUGGEST=1 zsh -i || exit 1
print 'interactive shell started'
# Source the init script
# 2 spaces for skip the init process if there is no .zshrc
zpty -w z "  setopt HIST_IGNORE_SPACE"
zpty -w z "  source '${0:a:h}/completion-server-init.zsh'"

# wait for ok from shell
read-to-null &> /dev/null
print 'interactive shell ready'

# listen on a socket for completion requests
server_dir=$1
pid_file="$server_dir/pid"
socket_path="$server_dir/socket"

cleanup() {
	print 'removing socket and pid file...'
	rm -f $socket_path $pid_file
	print "autosuggestion server stopped, pid: $$"
	exit
}

trap cleanup TERM INT HUP EXIT

mkdir -m 700 $server_dir

while ! zsocket -l $socket_path; do
	if [[ ! -r $pid_file ]] || ! kill -0 $(<$pid_file); then
		rm -f $socket_path
	else
		exit 1
	fi
	print "will retry listening on '$socket_path'"
done

server=$REPLY

print "server listening on '$socket_path'"

print $$ > $pid_file

typeset -A fds ready cached_input

fds[$server]=1

while zselect -A ready ${(k)fds}; do
	queue=(${(k)ready})
	for fd in $queue; do
		if (( fd == server )); then
			accept-connection
		else
			handle-request $fd
		fi
	done
done
