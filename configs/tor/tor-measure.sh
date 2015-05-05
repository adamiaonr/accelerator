# script to measure packet processing time in each tor node
# created by: antonior@andrew.cmu.edu

#!/bin/bash

# general system variables
PEM_FILE=/home/$USER/.ssh/node1.pem

# tor variables
TOR_DATA_DIR=/home/$USER/workbench/accelerator-misc
# default torrc file (should be used most of the times)
TOR_TORRC=torrc
TOR_DIR=/home/$USER/workbench/accelerator/tor
TOR_OR_DIR=src/or
TOR_BINARY=tor
TOR_CONFIGS=/home/$USER/workbench/accelerator/configs/tor

# node variables
NODE_IPS=("172.31.100.10" "172.31.100.20" "172.31.100.30" "172.31.100.40")
NODE_NAMES=("node1" "node2" "node3" "node4")
NODE_SRVR="172.31.100.50"

usage () {
    echo "usage: ./tor-measure.sh [ -n or --number-runs <number of test runs> || -l or --limit-rate <limit rate kbps> || -f or --file <filename to wget> ]"
}

# other testing variables (and their default values)
NUM_RUNS=2
TEST_FOLDER=$TOR_DATA_DIR/measurements
FILE="1MB.txt"
LIMIT_RATE="700k"

while [ "$1" != "" ]; do
    
    case $1 in

		-n | --number-runs )			shift
										NUM_RUNS=$1
										;;
		-l | --limit-rate )				shift
										LIMIT_RATE=$1
										;;
		-f | --file )					shift
										FILE=$1
										;;
		-h | --help	)					usage
										exit
										;;
		* )								usage
										exit 1
		esac
		shift

done

# 0) prepare the testing environment
if [[ ! -d $TEST_FOLDER ]]; then
	mkdir -p $TEST_FOLDER
fi

cd $TEST_FOLDER
rm -rf *.pcap
#rm -rf *.csv
#rm -rf *.times

# 1) start NUM_RUNS measurements on each one of the tor nodes
while [[ $NUM_RUNS -gt 0 ]]; do

	INDEX=3

	# 2) start tcpdump, tor (and privoxy on node 1)
	while [[ $INDEX -ge 0 ]]; do

		# 2.1) keep a separate folder for each one of the tor nodes
		if [[ ! -d $TEST_FOLDER/${NODE_NAMES[$INDEX]} ]]; then
			mkdir -p $TEST_FOLDER/${NODE_NAMES[$INDEX]}
		fi		

		cd $TEST_FOLDER/${NODE_NAMES[$INDEX]}

		ssh -i $PEM_FILE -t $USER@${NODE_IPS[$INDEX]} bash -c "'sudo killall tcpdump'"

		# # 2.2) if node 1, stop privoxy (proxy for tor + wget)
		# if [[ $INDEX -eq 0 ]]; then
		# 	ssh -i $PEM_FILE -t $USER@${NODE_IPS[$INDEX]} bash -c "'sudo service privoxy stop'"
		# fi

		# 2.3) remote start (1) tcpdump and (2) tor, in that 
		# order. the tcpdump files are saved as [node_num].[num_run].pcap
		ssh -i $PEM_FILE $USER@${NODE_IPS[$INDEX]} bash -c "'(sudo nohup tcpdump -i eth0 -w $TOR_DATA_DIR/${NODE_NAMES[$INDEX]}.$NUM_RUNS.pcap >/dev/null 2>&1) &'"
#		ssh -i $PEM_FILE $USER@${NODE_IPS[$INDEX]} bash -c "'$TOR_DIR/$TOR_OR_DIR/$TOR_BINARY -f $TOR_DATA_DIR/$TOR_TORRC'"

		# # 2.4) in node 1, start privoxy AFTER tor is initialized
		# if [[ $INDEX -eq 0 ]]; then

		# 	# 2.4.1) x second rule...
		# 	sleep 1
		# 	ssh -i $PEM_FILE -t $USER@${NODE_IPS[$INDEX]} bash -c "'sudo service privoxy start'"
		# fi

		$((INDEX--))

	done

	# 3) start a file transaction at a precise rate given as argument...
	# ... and remove the file, as we don't need it (ALWAYS CLEAN UP 
	# AFTER YOURSELF!!!)
	ssh -i $PEM_FILE $USER@${NODE_IPS[0]} bash -c "'wget --tries=3 --timeout=3 --limit-rate=$LIMIT_RATE http://$NODE_SRVR/$FILE; rm $FILE'"

	INDEX=0

	# 4) now, the trickiest part...
	for IP in ${NODE_IPS[@]}; do

		# 4.1) killall (1) tcpdump and (2) tor, in that order
		ssh -i $PEM_FILE $USER@$IP bash -c "'sudo killall tcpdump'"
#		ssh -i $PEM_FILE $USER@$IP bash -c "'killall tor'"

		# 4.2) scp the files into node-gw
		cd $TEST_FOLDER/${NODE_NAMES[$INDEX]}
		scp -i $PEM_FILE $USER@$IP:$TOR_DATA_DIR/${NODE_NAMES[$INDEX]}.$NUM_RUNS.pcap .

		# 4.3) clean up stuff on remote side
		ssh -i $PEM_FILE $USER@$IP bash -c "'cd $TOR_DATA_DIR; rm *.pcap;'"

		$((INDEX++))

	done

	$((NUM_RUNS--))

done

# 5) finally, stop all ec2 instances
#$TOR_CONFIGS/tor-setup.sh --stop-instances

exit 0
