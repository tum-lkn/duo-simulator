#!/bin/sh

BASE_PATH=..
BINARY="$BASE_PATH/src/opera/datacenter/htsim_ndp_dynexpTopology"

QUEUE=8

PARAMS="-simtime 10.00001 -cutoff 0 -rlbflow 0 -cwnd 20 -q $QUEUE -pullrate 1"
TOPO="-topfile $BASE_PATH/topologies/dynexp_1path_N=64_k=16_G=1.txt"
HOSTS=512

DATA_PREFIX="$BASE_PATH/data/permutation/64racks_8hosts_10s/opera-1500-global/"
mkdir -p "$DATA_PREFIX"

run(){
  fname="$DATA_PREFIX"FCT_opera_"$HOSTS"_q"$QUEUE"_"$1"perc.txt
  echo "Starttime `date +%s`" > "$fname"
  $BINARY $PARAMS $TOPO -flowfile $BASE_PATH/traffic_gen/flows_permutation_"$1"percLoad_10sec_"$HOSTS"hosts.htsim >> "$fname"
  echo "Endtime `date +%s`" >> "$fname"
}

run 10 &
run 20 &
run 30
run 40
run 50
run 60
