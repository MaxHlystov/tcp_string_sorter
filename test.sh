#!/bin/bash

echo "Info: Test process starts at `date`"

# find a free port to start tests
read LOWERPORT UPPERPORT < /proc/sys/net/ipv4/ip_local_port_range
while :
do
    PORT="`shuf -i $LOWERPORT-$UPPERPORT -n 1`"
    ss -lpn | grep -q ":$PORT " || break
done
echo "Info: Use server port ${PORT}"

./tcp_string_sorter $PORT & &> /dev/null
PID=$!
echo "Info: Server started with PID ${PID}"

###################
# Tests

# Connect and send test string
END_CONNECTION="OFF\x00"
TEST_STRING="aabbccddeeffggaabbccddeeffgg\x00"
RES_GAUGE="ggggffffeeeeddddccccbbbbaaaa"
RES=`echo -n -e ${TEST_STRING}${END_CONNECTION} | nc 127.0.0.1 $PORT`

# compare result with gauge 
if [[ RES != RES_GAUGE ]]
then
    echo "Error: expect ${RES_GAUGE} receive ${RES}"
fi

# Send stop command to server
STOP_SERVER="STOP\x00"
RES=`echo -n -e $STOP_SERVER | nc 127.0.0.1 $PORT`

if [[ -n RES ]]
then
    echo "Error: Server answer on stop command: ${RES}"
fi

sleep 1
# if server did not stop
ps|grep $PID &> /dev/null
if [[ $? ]]
then
    echo "Error: server does not stop."
    kill -9 $PID &> /dev/null
fi

##################
# End tests

echo "Info: Test process ends at `date`"
