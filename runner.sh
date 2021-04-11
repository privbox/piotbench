#!/bin/bash -ex

SCRIPT_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
export PREFIX=${PREFIX:-${SCRIPT_DIR}}
# export COMP_VALUES=${COMP_VALUES:-1000 10000 100000}
export COMP_VALUES=${COMP_VALUES:-1000 5000 10000}
# export IO_SIZE_VALUES=${IO_SIZE_VALUES:-8 16 32 64 128 256 512 1024}
export IO_SIZE_VALUES=${IO_SIZE_VALUES:-4 16 64 256 1024}
export KERNCALL=${KERNCALL:-1}
export DURATION=${DURATION:-30}
export CLIENT_ITERS=${CLIENT_ITERS:-11}
export EXTRA_SERVER_ARGS=${EXTRA_SERVER_ARGS:-""}
export EXTRA_CLIENT_ARGS=${EXTRA_CLIENT_ARGS:-"--nr_requests=1000000"}
export SERVER=$PREFIX/server
export CLIENT=$PREFIX/client

server_log () {
    io_size=$1
    compute_dur=$2
    kerncall=$3
    echo "/tmp/piotbench-server-io_$io_size-compute_$compute_dur-kerncall_$kerncall.log"
}

client_log () {
    io_size=$1
    compute_dur=$2
    kerncall=$3
    echo "/tmp/piotbench-client-io_$io_size-compute_$compute_dur-kerncall_$kerncall.log"
}

start_server () {
    io_size=$1
    compute_dur=$2
    kerncall=$3
    server_log=$4
    $SERVER --kerncall.global=$kerncall --load.compute_dur=$compute_dur --load.max_io_size=$io_size ${EXTRA_SERVER_ARGS} &>$server_log &
    echo $!
}

kill_server () {
    pid=$1
    kill -SIGINT $pid
}

run_client () {
    client_iters=$1
    client_log=$2

    rm -f $client_log
    for i in $(seq $client_iters);
    do
        $CLIENT --duration=${DURATION} ${EXTRA_CLIENT_ARGS} 2>&1 | tee -a $client_log
    done
}

run_test () {
    io_size=$1
    compute_dur=$2
    kerncall=$3
    server_log=$(server_log $io_size $compute_dur $kerncall)
    client_log=$(client_log $io_size $compute_dur $kerncall)

    server_pid=$(start_server $io_size $compute_dur $kerncall $server_log)
    sleep 0.1
    
    run_client ${CLIENT_ITERS} $client_log

    kill_server $server_pid
}

run_all_tests () {
    for comp in ${COMP_VALUES}
    do
        for io_size in ${IO_SIZE_VALUES}
        do
            run_test $io_size $comp ${KERNCALL}
        done
    done
}

get_one_result () {
    io_size=$1
    compute_dur=$2
    kerncall=$3
    client_log=$(client_log $io_size $compute_dur $kerncall)
    grep 'Requests/sec' $client_log | awk '{print $NF}' | tail -"$(( ${CLIENT_ITERS} - 1 ))" | awk '{s+=$1}END{print s/NR}'
}

view_results () {
    set +x
    printf 'comp\\io\t'
    for io_size in ${IO_SIZE_VALUES}
    do
        printf "%d\t" $io_size
    done
    echo

    for comp in ${COMP_VALUES}
    do
        printf '%d\t' $comp
        for io_size in ${IO_SIZE_VALUES}
        do
            printf '%f\t' $(get_one_result $io_size $comp ${KERNCALL})
        done
        printf '\n'
    done
}


case "$1" in
    test)
        run_all_tests
        ;;
    report)
        view_results
        ;;
    *)
        echo "$0 test|report"
        exit 1
        ;;
esac
