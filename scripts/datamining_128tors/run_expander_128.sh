#!/bin/sh

BASE_PATH=..
BINARY="$BASE_PATH/src/expander/datacenter/htsim_mixed_expander"

QUEUE=50
RTO=15

PARAMS="-simtime 5.00001 -q $QUEUE -pullrate 1 -cwnd 30 -VLB 0 -rto $RTO -hpflowsize 1000000"
TOPO="-topfile $BASE_PATH/topologies/expander_N=128_u=8_ecmp.txt"
HOSTS=1024

DATA_PREFIX="$BASE_PATH/data/datamining/128racks_8hosts_10s/expander-mixed-rto${RTO}ms-q${QUEUE}-hp1mb-1500/"
mkdir -p "$DATA_PREFIX"

run(){
  fname="$DATA_PREFIX"FCT_exp_"$HOSTS""$VLB"_q"$QUEUE"_"$1"perc.txt
  echo "Starttime `date +%s`" > "$fname"
  $BINARY $PARAMS $TOPO -flowfile $BASE_PATH/traffic_gen/flows_"$1"percLoad_5sec_"$HOSTS"hosts.htsim >> "$fname"
  echo "Endtime `date +%s`" >> "$fname"
}

run 60