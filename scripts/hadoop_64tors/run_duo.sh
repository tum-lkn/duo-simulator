#!/bin/sh

BASE_PATH=..
BINARY="$BASE_PATH/src/debruijn/htsim_mixed_dyndebruijn"

QUEUE=50
CUTOFF=10000000
RTO=15
CWND=30

if [ -z $1 ]; then DAY=9900; else DAY=$1; fi
if [ -z $2 ]; then NIGHT=100; else NIGHT=$2; fi


PARAMS="-simtime 5.00001 -cutoff $CUTOFF -q $QUEUE -rto $RTO -day $DAY -night $NIGHT -hpflowsize 1000000 -cwnd $CWND"
TOPO="-topfile $BASE_PATH/topologies/debruijn_N=64_2s_6da.txt"
HOSTS=512
VLB=""

DATA_PREFIX="$BASE_PATH/data/hadoop/64racks_8hosts_10s/ddb-2s-6da-mixed-co$CUTOFF-rto${RTO}ms-cwnd${CWND}-day${DAY}us-night${NIGHT}us-q${QUEUE}-hp1mb-1500/"
mkdir -p "$DATA_PREFIX"

run(){
  fname="$DATA_PREFIX"FCT_ddb_2s_6da_df_bfs_"$HOSTS""$VLB"_q"$QUEUE"_"$1"perc.txt
  echo "Starttime `date +%s`" > "$fname"
  $BINARY $PARAMS $TOPO -flowfile $BASE_PATH/traffic_gen/flows_hadoop_"$1"percLoad_10sec_"$HOSTS"hosts.htsim >> "$fname"
  echo "Endtime `date +%s`" >> "$fname"
}

run 10 &
run 20 &
run 30
run 40
run 50
run 60
run 70
run 80


