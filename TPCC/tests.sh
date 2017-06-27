#!/bin/bash


# ---------------------------------------------------------------
# FUNCTIONS
# ---------------------------------------------------------------

trap on_sigint SIGINT


on_sigint() {
  echo -e "\nShutting down test script by killing pending \`tpcc\` processes..."
  killall tpcc

  echo "Goodbye."
  exit -1
}

do_tests() {
  for i in "${!a_nthreads[@]}"; do
    nthreads=${a_nthreads[${i}]}
    ntottx=${a_tottx[${i}]}
    IFS=' ' read -ra a_ar <<< $(eval "echo \${a_ar_$nthreads[@]}")

    for txs in "${a_ar[@]}"; do

      for inputfile in "${a_inputs[@]}"; do
        inputname=$(basename ${inputfile})
        testgroup=T${nthreads}-N${ntottx}-A${txs}-I${inputname}-E$1

        for r in $(seq $run_id_start $(echo "$run_id_start+$nruns" | bc)); do
          testname=${testgroup}.${r}
          echo -e "\nRunning test ${testname}"

          # Run TPCC Server
          echo -e "\` ./Server/tpcc $port $pool ${nthreads} &"
          ./Server/tpcc $port $pool ${nthreads} \
            > ${LOGS_DIR}/log_server_${testname}.txt & # server_port pool_size num_workers
          pid_server=$!

          sleep 15

          # Run TPCC Client
          echo -e "\` python Server/client.py $client_py_addr $client_py_port ./tpcc $address $port $nclients 1 ${ntottx} ${txs} ${inputfile}"
          python Server/client.py $client_py_addr $client_py_port ./tpcc $address $port $nclients 1 ${ntottx} ${txs} ${inputfile} \
            > ${LOGS_DIR}/log_client_${testname}.txt & # server_address  port_number  group_number  group_size  txs_x_proc  arrival_rate_x_proc(txs/sec.) input_file

          wait $pid_server
          if [ -f stats_dump.tsv ]; then
            mv stats_dump.tsv ${STATS_DIR}/stats_${testname}.tsv
          fi
        done
      done

    done

  done
}

do_exp() {
  # $1 = experiment name
  # $2 = tinySTM make params

  cd ../tinySTM
  make -B $(eval "echo $2")

  # cd ../TPCC/Client
  # make -f Makefile.stm -B  $(eval "echo $TPCC_Client_make_flags")

  cd ../TPCC/Server
  make -f Makefile.stm -B $(eval "echo $TPCC_Server_make_flags")
  cd ..
  do_tests $1
}

do_exp_base() {
  do_exp "base"
}

do_exp_ea_st() {
  do_exp "ea_st" "EA=1"
}

do_exp_ea_st_sim() {
  do_exp "ea_st_sim" "EA=1 EA_SIM=1"
}

do_exp_ea_sim_et_kern() {
  do_exp "ea_sim_et_kern" "EA=1 EA_SIM_ET_KERN=1"
}

do_exp_ea_sim_et_user() {
  do_exp "ea_sim_et_user" "EA=1 EA_SIM_ET_USER=1"
}

do_exp_base_stats_commit() {
  do_exp "base_stats_commit" "EA_STATS=1 EA_STATS_COMMIT=1"
}

do_exp_base_stats_extend() {
  do_exp "base_stats_extend" "EA_STATS=1 EA_STATS_COMMIT=1 EA_STATS_EXTEND=1"
}

do_exp_base_stats_abort() {
  do_exp "base_stats_abort" "EA_STATS=1 EA_STATS_COMMIT=1 EA_STATS_ABORT=1"
}

do_exp_ea_st_stats_commit() {
  do_exp "ea_st_stats_commit" "EA=1 EA_STATS=1 EA_STATS_COMMIT=1"
}

do_exp_ea_st_stats_extend() {
  do_exp "ea_st_stats_extend" "EA=1 EA_STATS=1 EA_STATS_COMMIT=1 EA_STATS_EXTEND=1"
}

do_exp_ea_st_stats_abort() {
  do_exp "ea_st_stats_abort" "EA=1 EA_STATS=1 EA_STATS_COMMIT=1 EA_STATS_ABORT=1"
}


# ---------------------------------------------------------------
# GLOBALS
# ---------------------------------------------------------------

a_nthreads=( 8 16 24 )
a_tottx=( "200000" "400000" "600000")
a_ar_8=( "30000" )
a_ar_16=( "40000" )
a_ar_24=( "50000" )
a_inputs=( "../input_tpcc.txt" )
run_id_start=1

###
# $1 = Experiments directory
# $2 = Comma-separated list of experiments to run (see do_exp_* functions)
# $3 = Number of runs per test group
# $4 = TPCC Server address
# $5 = TPCC Server port
# $6 = TPCC Server pool size
# $7 = TPCC Client number of senders
# $8 = TPCC Client server.py address
# $9 = TPCC Client server.py port
###

exp_dir=$1
IFS=',' read -ra experiments <<< "$2"
nruns=$3
address=$4
port=$5
pool=$6
nclients=$7
client_py_addr=$8
client_py_port=$9

TPCC_Server_make_flags="NBB=1 BLD=test"
TPCC_Client_make_flags=""

# Setup experiment directories and paths
export STM_EXPERIMENT_PATH=`pwd`

LOGS_DIR=${exp_dir}/logs
STATS_DIR=${exp_dir}/stats

mkdir -p $LOGS_DIR
mkdir -p $STATS_DIR

# Create logs directory on the client and compile the client
# python Server/client.py $client_py_addr $client_py_port mkdir -p $LOGS_DIR
python Server/client.py $client_py_addr $client_py_port make -f Makefile.stm -B  $(eval "echo $TPCC_Client_make_flags")


# ---------------------------------------------------------------
# MAIN
# ---------------------------------------------------------------
export STM_STATS=1

for exp in "${experiments[@]}"; do
  echo "--------------------------------------------------------"
  echo "Running experiment $exp"
  echo "--------------------------------------------------------"

  do_exp_${exp}
done

unset STM_STATS
