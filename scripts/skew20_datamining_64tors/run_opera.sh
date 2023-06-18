#!/bin/sh

BASE_PATH="/home/sim/johannes/debruijn-packet-level"
BINARY="../../src/opera/datacenter/htsim_ndp_dynexpTopology"

QUEUE=8

PARAMS="-simtime 10.00001 -cutoff 15000000 -rlbflow 0 -cwnd 20 -q $QUEUE -pullrate 1"
TOPO="-topfile $BASE_PATH/topologies/dynexp_1path_N=64_k=16_G=1.txt"  # kshortest 1path
HOSTS=512

DATA_PREFIX="$BASE_PATH/data/datamining_skew/64racks_8hosts_10s/opera-1500-global/"
mkdir -p "$DATA_PREFIX"

run(){
  fname="$DATA_PREFIX"FCT_opera_"$HOSTS"_q"$QUEUE"_"$2"perc.txt
  echo "Starttime `date +%s`" > "$fname"
  $BINARY $PARAMS $TOPO -flowfile $BASE_PATH/traffic_gen/flows_skewed"$1"_100percLoad_10sec_"$2"hosts.htsim >> "$fname"
  echo "Endtime `date +%s`" >> "$fname"
}

#run 10 &
#run 2 16 &
#run 7 56
#run 13 104
#run 26 208
#run 39 312
#run 80
#run 50 
#run 30
#run 70
run 20 160
run 32 256
#run 45 360 &
