# script to measure packet processing time in each tor node
# created by: antonior@andrew.cmu.edu

#!/bin/bash

# node variables
NODE_IPS=("172.31.100.10" "172.31.100.20" "172.31.100.30" "172.31.100.40")

usage () {
    echo "usage: ./pre-process.sh [ -n or --node-num <node-index> ]"
}

while [ "$1" != "" ]; do
    
    case $1 in

		-n | --node-num )				shift
										THIS_NODE=$1
										;;
		-h | --help	)					usage
										exit
										;;
		* )								usage
										exit 1
		esac
		shift

done

DATA_FOLDER=$(pwd)
NODE_FOLDER=$DATA_FOLDER/node$THIS_NODE
NODE_FILE=$DATA_FOLDER/node$THIS_NODE/node$THIS_NODE.csv

# 4) write to the final .csv file
if [[ ! -f $NODE_FILE ]]; then
	touch $NODE_FILE
fi

echo -e "" > $NODE_FILE

echo -e "#### data file for node"$THIS_NODE "###" >> $NODE_FILE
echo -e "# format is: " >> $NODE_FILE
echo -e "# test <test number>" >> $NODE_FILE
echo -e "[SRC_IP]\t[DST_IP]" >> $NODE_FILE
echo -e "[FIRST_IN]\t[LAST_IN]" >> $NODE_FILE
echo -e "[FIRST_OUT]\t[LAST_OUT]" >> $NODE_FILE
echo -e "[PACKETS_IN]\t[PACKETS_OUT]" >> $NODE_FILE
echo -e "" >> $NODE_FILE

cd $NODE_FOLDER

$((THIS_NODE--))

# 1) start cycling through the .pcap files
TEST_NUMBER=1

for FILE in *.pcap; do

	PCAP_FILE_SIZE=$(du -k $FILE | cut -f 1)

	if [[ $PCAP_FILE_SIZE -lt 100 ]]; then
		continue
	fi

	# 2) every tor node will communicate with either 1 or 2 more nodes in the 
	# following manner: 
	#	
	#	http request 	: DST_IP -> THIS_NODE -> SRC_IP
	#	http response	: DST_IP <- THIS_NODE <- SRC_IP
	#
	# we use 'DST_IP' to identify the destination of the http response (the 
	# payload), and 'SRC_IP' to identify the source of the http response.
	#
	# we now analyze all tcp streams (tcp.stream field in .pcap files) to 
	# identify the SRC_IP and DST_IP
	SRC_IP=""
	DST_IP=""

	# 2.1) we assume no more than 4 tcp streams... this seams reasonable
	TCP_STREAM_INDEX=0
	TCP_STREAM_INDEX_SRC=0
	TCP_STREAM_INDEX_DST=0

	while [[ $TCP_STREAM_INDEX -lt 3 ]]; do
		
		# 2.2) exclude ssh traffic (with node-gw, ip = 172.31.100.22) and 
		# save everything on .csv file
		tshark -tud -r $FILE -Y "tcp.stream eq $TCP_STREAM_INDEX and (ip.src != 172.31.100.22 and ip.dst != 172.31.100.22)" -T fields -e frame.number -e ip.src -e ip.dst -e proto.string -e frame.time -E header=y > temp.csv

		# 2.3) only consider files with more than 50 lines (filter out sporadic 
		# communication with auth server)
		if [[ $(cat temp.csv | wc -l) -ge 50 ]]; then

			# 2.4) check first line, extract the first ip: if ip.src of 1st 
			# line is ${NODE_IPS[$THIS_NODE]}, then the destination IP of that 
			# line is our SRC_IP
			head -2 temp.csv > temp.csv.stripped
			FIRST_IP=$(grep -E -o "([0-9]{1,3}[\.]){3}[0-9]{1,3}" temp.csv.stripped | head -1)

			if [[ $FIRST_IP == "${NODE_IPS[$THIS_NODE]}" ]]; then

				SRC_FRAME_NUM=$(grep -E -o "^[0-9]{1,3}" temp.csv.stripped)
				SRC_IP=$(grep -E -o "([0-9]{1,3}[\.]){3}[0-9]{1,3}" temp.csv.stripped | grep -v ${NODE_IPS[$THIS_NODE]})
				TCP_STREAM_INDEX_SRC=$TCP_STREAM_INDEX

				if [[ ${NODE_IPS[$THIS_NODE]} == "${NODE_IPS[0]}" ]]; then
					TCP_STREAM_INDEX_DST=$TCP_STREAM_INDEX_SRC
				fi

			else

				DST_FRAME_NUM=$(grep -E -o "^[0-9]{1,3}" temp.csv.stripped)
				DST_IP=$(grep -E -o "([0-9]{1,3}[\.]){3}[0-9]{1,3}" temp.csv.stripped | grep -v ${NODE_IPS[$THIS_NODE]})
				TCP_STREAM_INDEX_DST=$TCP_STREAM_INDEX

				if [[ ${NODE_IPS[$THIS_NODE]} == "${NODE_IPS[0]}" ]]; then
					TCP_STREAM_INDEX_SRC=$TCP_STREAM_INDEX_DST
				fi

			fi			
		fi

		$((TCP_STREAM_INDEX++))
	done

	# 3) extract the remaining elements:

	#	FIRST_IN 	: timestamp of first packet received from SRC_IP
	#	LAST_IN 	: timestamp of last packet received from SRC_IP
	#	PACKETS_IN 	: num. of packets received from SRC_IP

	#	FIRST_OUT 	: timestamp of first packet sent to DST_IP
	#	LAST_OUT 	: timestamp of last packet sent to SRC_IP
	#	PACKETS_OUT : num. of packets sent to DST_IP

	FIRST_IN=""
	LAST_IN=""
	PACKETS_IN=0

	FIRST_OUT=""
	LAST_OUT=""
	PACKETS_OUT=0

	tshark -tud -r $FILE -Y "tcp.stream eq $TCP_STREAM_INDEX_SRC and ip.dst == ${NODE_IPS[$THIS_NODE]} ip.dst == ${NODE_IPS[$THIS_NODE]} and frame.len > 1000 and (ip.src != 172.31.100.22 and ip.dst != 172.31.100.22)" -T fields -e frame.time > temp.csv
	FIRST_IN=$(head -1 temp.csv)
	LAST_IN=$(tail -1 temp.csv)
	PACKETS_IN=$(cat temp.csv | wc -l)

	tshark -tud -r $FILE -Y "tcp.stream eq $TCP_STREAM_INDEX_DST and ip.src == ${NODE_IPS[$THIS_NODE]} and frame.number > $SRC_FRAME_NUM and frame.len > 1000 and (ip.src != 172.31.100.22 and ip.dst != 172.31.100.22)" -T fields -e frame.time > temp.csv
	FIRST_OUT=$(head -1 temp.csv)
	LAST_OUT=$(tail -1 temp.csv)
	PACKETS_OUT=$(cat temp.csv | wc -l)

	# 4) write to the final .csv file
	echo -e "# test "$TEST_NUMBER >> $NODE_FILE
	echo -e $SRC_IP"\t"$DST_IP >> $NODE_FILE
	echo -e $FIRST_IN"\t"$LAST_IN >> $NODE_FILE
	echo -e $FIRST_OUT"\t"$LAST_OUT >> $NODE_FILE
	echo -e $PACKETS_IN"\t"$PACKETS_OUT >> $NODE_FILE
	echo -e "" >> $NODE_FILE

	$((TEST_NUMBER++))

done

exit 0
