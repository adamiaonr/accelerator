# script to run (and measure stuff from) tor ORs, from node-gw in one stroke
# created by: antonior@andrew.cmu.edu

#!/bin/bash

PEM_FILE=/home/$USER/.ssh/node1.pem

TOR_DATA_DIR=/home/$USER/workbench/accelerator-misc
# default torrc file (should be used most of the times)
TOR_TORRC=torrc

TOR_DIR=/home/$USER/workbench/accelerator/tor
TOR_OR_DIR=src/or
TOR_BINARY=tor

NODE_IPS=("172.31.100.10" "172.31.100.20" "172.31.100.30" "172.31.100.40")
NODE_NAMES=("node1" "node2" "node3" "node4")

usage () {
    echo "usage: ./tor-setup.sh [-p <.pem file> || -f <torrc file> || --with-tcp-dump || --with-inet-connection]"
}

W_TCP_DUMP=0
W_INET_CONNECTION=0

while [ "$1" != "" ]; do
    
    case $1 in

		-f | --torrc-file )				shift
										TOR_TORRC=$1
										;;
		-p | --pem-file )				shift
										PEM_FILE=$1
										;;
		-t | --with-tcp-dump )			W_TCP_DUMP=1
										;;
		-i | --with-inet-connection )	W_INET_CONNECTION=1
										;;
		-h | --help	)					usage
										exit
										;;
		* )								usage
										exit 1
		esac
		shift

done

# 1) if specified, setup node-gw as an Internet gateway for the private Tor 
# network

# 2) run tor as deamon...
INDEX=0

for IP in ${NODE_IPS[@]}; do

	# 2.1) if tcpdumps are to be collected, start them now...
	if [[ $W_TCP_DUMP -eq 1 ]]; then

		ssh -i $PEM_FILE $USER@$IP bash -c "'tcpdump -i eth0 -s 0 -n -w $TOR_DATA_DIR/${NODE_NAMES[$INDEX]}.cap'"
	fi

	# 2.2) if the client node, also restart privoxy, just in case: the 
	# objective is to have all network applications making HTTP requests (e.g. 
	# wget, curl) going through privoxy, which in turn passes them to 
	# 127.0.0.1:9011, where Tor is listening (why don't we do it directly? 
	# it doesn't work, and i don't know why...)
	#
	# e.g. just run:
	# $ wget http://172.31.100.50
	#
	# to communicate with an apache2 server running in node-server, and fecth 
	# the index.html page via Tor...
	if [[ $INDEX -eq 0 ]]; then

		ssh -i $PEM_FILE -t $USER@$IP bash -c "'sudo service privoxy stop; sudo service privoxy start;'"
	fi

	ssh -i $PEM_FILE $USER@$IP bash -c "'$TOR_DIR/$TOR_OR_DIR/$TOR_BINARY -f $TOR_DATA_DIR/$TOR_TORRC'"

	$((INDEX++))
done

# 3) if specified, periodically retrieve tcpdumps from targets...
if [[ $W_TCP_DUMP -eq 1 ]]; then

	cd $TOR_DATA_DIR

	# infinite loop (don't judge me...)
	KEEP_LOOPIN=1

	while [[ $KEEP_LOOPIN -eq 1 ]]; do

		INDEX=0

		for IP in ${NODE_IPS[@]}; do

			scp -i $PEM_FILE $USER@$IP:$TOR_DATA_DIR/${NODE_NAMES[$INDEX]}.cap .

			$((INDEX++))
		done

		# sleep for 10 secs before re-collecting the tcpdumps...
		sleep 10
	done
fi

exit 0
