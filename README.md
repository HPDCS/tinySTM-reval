An implementation of a prompt and application-transparent transaction revalidation scheme for the tinySTM package.

## Introduction
Software Transactional Memory (STM) is a powerful tool for managing shared-data accesses in multi-thread applications. STM allows encapsulating code blocks within transactions, whose accesses to shared-data are executed with atomicity and isolation guarantees. Checking the consistency of the running transaction is performed by the STM layer at precise points of the execution of a transaction, such as when it has read or written an object, or prior to committing its changes. However, besides correctness reasons, performance and energy efficiency issues may still arise in scenarios where no consistency check is performed by a thread for a while and its currently executed transaction has become inconsistent in the meantime.

We designed and implemented an STM architecture that, thanks to a lightweight operating system support, enables a fine-grain periodic revalidation of transactions. It exploits very fine-grain hardware-timer events, which are directly delivered and handled by user-space code to carry out a transaction's revalidation independently of the application code willingness to pass control to the STM layer. This enables performing timer-based revalidation at execution points where the traditional approach may result ineffective. Notably, this approach adds new capabilities in terms of support for early aborts of transactions, which are orthogonal to those already offered by traditional STM platforms which perform snapshot extensions or anyway validate transactions upon read/write accesses to shared data.

Notice that our approach does not require manual instrumentation of the application code, thus being fully transparent. It targets Linux systems and has been integrated with the open source TinySTM package.

## Software organization
Our architecture is composed of two baseline components: (1) a `timestretch` kernel module to generate hardware-timer interrupts at a higher rate than the original scheduling time quantum, and (2) a patched TinySTM layer capable of running software transactions with the aforementioned prompt revalidation support.

The organization of code is as follows:

- `include/` contains header files to be used by all TinySTM benchmarks in order to safely revalidate transactions at any point during execution;
- `timestretch/` contains all code related to the `timestretch` kernel module;
- `tinySTM/` contains the patched TinySTM layer;
- `TPCC/` is the TPC-C transactional benchmark.

All the remaining folders are of little interest and are only needed to support the compilation of the aforementioned software components.

## Compilation and usage
Before running experiments against our transaction revalidation scheme, one first needs to compile and prepare the `timestretch` kernel module and the patched TinySTM layer.

### The `timestretch` kernel module
To compile and run the `timestretch` kernel module, follow the instructions in the [README](https://github.com/HPDCS/tinySTM-reval/blob/master/timestretch/INSTALL) located inside the `timestretch` folder. Recall that to run this module, you'll need:

- Linux Kernel version >= 2.6.26, headers included
- `gcc` C compiler
- System map and configuration for the current kernel version installed
  in `/boot`
- `udev` to set correct permissions on the created device file, looking
  for new rules in `/etc/udev/rules.d`

The module comes with a _scale_ parameter that represents the number of partitions created out of the original time quantum. Therefore, if the OS generates a tick every 1 millisecond (as in typical Linux installations), a scale of `10` will generate 10 times more extra-ticks than before, with a period of around 100 microseconds between two consecutive extra-ticks. You can always change the default scale parameter (`10`) by writing the desired value into the associated pseudo-file in the `/sys` filesystem. For example, to set `scale` to `20`, one can issue the following root command:

```
# echo 20 > /sys/module/timestretch/parameters/scale
```

Alternatively, the desired `scale` value can be set at module load-time:

```
# modprobe timestretch scale=20
```

### The patched TinySTM layer
To compile TinySTM, move into the `tinySTM` directory and launch `make`. It will produce a `lib/libstm.a` archive which can be further linked against different STM benchmarks (such as TPCC). Notice that benchmarks included in this repository have been already patched so as to expect to find the TinySTM library archive in the path reported above.

The original TinySTM Makefile has been augmented with additional parameters to enable/disable our revalidation architecture. They have to be passed to `make` using the syntax `NAME=value`. Here's a quick tour of the supported parameters:

* `EA`: If set to `1`, enables our revalidation scheme. Notice that this parameter alone requires the `timestretch` module to be compiled and that the resulting TinySTM library archive expects the module to be running prior to launching any benchmark.
* `EA_SIM`: If set to `1` together with `EA`, simulates all aborts coming from our revalidation architecture (i.e., resulting from a revalidation triggered by an extra-tick). This is useful to measure when the original TinySTM code is able to detect a doomed transaction.
* `EA_SIM_ET_USER`: If set to `1` together with `EA`, forces the periodic revalidation to be a no-operation. This is useful to estimate the overhead of our architecture in terms of extra-tick delivery to user-space code.
* `EA_SIM_ET_KERN`: If set to `1` together with `EA`, forces the extra-tick generation to be constrained in kernel-space, i.e., without delivering extra-ticks to user-space code. This is useful to estimate the overhead of extra-tick generation only.
* `EA_STATS`: If set to `1`, enables the tracking of a series of statistics concerning the number of and mean time between the following events: commits, reads, writes, revalidations and aborts. At the end of the benchmark experiment, a `stats_dump.tsv` file will be generated in the benchmark root folder with the following syntax per each tracked event and distinct transactional profile: `Number of events [TAB] Mean time between events [TAB] Variance wrt mean time`. The distinct events that can be tracked are:
    - `commit`: Commit of a transaction;
    - `completion`: Commit of a transaction since last abort;
    - `read`: Transactional read operation;
    - `write`: Transactional write operation;
    - `extend_plt_real`: Snapshot extension (including validation) coming from original TinySTM code;
    - `extend_plt_fake`: Snapshot extension (including validation) coming from original TinySTM code after an early abort has been simulated via `EA_SIM` (i.e., after the first `extend_ea_real` event); 
    - `extend_ea_real`: Snapshot extension (including validation) coming from our revalidation architecture;
    - `extend_ea_fake`: Snapshot extension (including validation) coming from our revalidation architecture after an early abort has been simulated via `EA_SIM` (i.e., after the first `extend_ea_real` event); 
    - `abort_plt_real`, `abort_plt_fake`, `abort_ea_real`, `abort_ea_fake`: Same as `extend_*` events, but for aborts.

To compile the TinySTM layer with periodic revalidation support, one would therefore launch the following command:

```
$ make EA=1
```

Any additional `EA_*` parameter must be passed along with `EA=1`. For example:

```
$ make EA=1 EA_STATS=1 EA_SIM_ET_USER=1
```

compiles TinySTM with revalidation and statistics, but maps the revalidation routine to a no-operation.

Finally, to compile a vanilla (i.e., original) TinySTM layer, it is sufficient to run `make` with no parameters.

### TPC-C benchmark
To compile and launch the TPC-C benchmarks, the `Client` and the `Server` modules must be compiled as follows in their own paths:

```
$ cd Server
$ make -f Makefile.stm NBB=1 BLD=test
$ cd Client
$ make -f Makefile.stm
```

In order to make a correct execution of the program, launch the Server before the Client in the relative paths as follows:

```
$ cd Server
$ ./tpcc  ServerPort  PoolSize  NumberOfTransactionalThreads
$ cd Client
$ ./tpcc  ServerAddress  ServerPort  GroupNumber  GroupSize NumberOfTransactionPerClient  ArrivalRatePerClient(txs/sec.) InputFile
```

where each parameter has the following meaning:

- `ServerAddress` is the IP address of the Server
- `ServerPort` is the TCP port the Server listens to
- `PoolSize` is the size of the backlog of requests at the Server
- `NumberOfTransactionalThreads` is the number of worker threads running transactions at the Server
- `GroupNumber` is the number of parallel connections between the Client and the Server, as well as the number of I/O Server threads listening to each connection
- `GroupSize` is the number of Client threads issuing requests to the same connection
- `NumberOfTransactionPerClient` is the number of requests issues per each connection between the Client and the Server
- `ArrivalRatePerClient(txs/sec.)` is the number of requests per second issued per each connection between the Client and the Server
- `InputFile` is the specific distribution of profiles to be used; each line _i_ is a profile (at most 5), and corresponds to the _i_-th profile in the order they are declared inside `TPCC/manager.h`.

## Experiments automation
We have prepared a small script that is able to automatize all experiments for the same `scale` parameter. This means that one needs to launch as many script instances as the number of different extra-tick scaling factors to experiment with.

The script is located inside the `TPCC/` directory and is named `tests.sh`. It runs one _experiment_ made of several _test groups_, each of which is composed of:

- A given experiment: baseline, extra-tick'd, etc. See `do_exp_*` functions inside the script, as well as `make` parameters for TinySTM.
- A given number of Server worker threads (`NumberOfTransactionalThreads`)
- A given number of requests per Client process (`NumberOfTransactionPerClient` )
- A given number of requests per second per Client process (`ArrivalRatePerClient(txs/sec.)`)
- A given distribution of profiles (`InputFile`)

Each test group is also repeated _N_ times, where _N_ is the number of runs.

The script relies on two additional python scripts: a Client `server.py` process and a Server `client.py` process. Although confusing, the roles of these process are straightforward:

- The Client `server.py` process listens to incoming remote commands and replays them locally on the Client machine.
- The Server `client.py` process sends commands to the `Client` server process.

These two side-scripts allow the main one to automate all network communication between the Server and the Client (security issues aside... we are not responsible for damages on your servers!), which can therefore be installed on two different machines.

The script accepts the following positional arguments:

1. An experiment directory name, inside which it creates two sub-directories: `logs/` for logging standard output; `stats/` for logging statistics.
2. A comma-separated list of experiments to run.
3. The number of runs per test group.
4. TPCC Server address. This is equivalent to `ServerAddress`.
5. TPCC Server port. This is equivalent to `ServerPort`.
6. TPCC Server pool size. This is equivalent to `PoolSize`.
7. TPCC Client number of senders. This is equivalent to `GroupNumber`, with `GroupSize` set to `1`.
8. TPCC Client server.py address. This is the IP address of the `Client` server python script.
9. TPCC Client server.py port. This is the TCP port number of the `Client` server python script.

The script also has some hard-coded configuration which can be changed by editing the script file. It includes:

- An array `a_nthreads=(...)` of Server worker threads.
- An array `a_tottx=(...)` of requests per each Client process (one entry per each number of Server worker threads).
- An array `a_ar_<T>=(...)` per each number of Server worker threads `<T>` of arrival rates per each Client process.
- An array `a_inputs=( "../input_tpcc.txt" )` of input files to be used at the Client side to generate a distribution of profiles.

## NCA 2017
For NCA 2017, we launched our script four times, using always:

- 10 runs per test group
- 4096 as the pool size
- 6 as the number of senders
- `a_nthreads=( 8 16 24 )`
- `a_tottx=( "200000" "400000" "600000")`
- `a_ar_8=( "30000" )`, `a_ar_16=( "40000" )`, `a_ar_24=( "50000" )`
- `a_inputs=( "../input_tpcc.txt" )`

The various address/port parameters depend on the specific network configuration and are therefore not reported here.

The first invocation only launches the following baseline experiments:

- `base`, meaning that it is the baseline case (vanilla TinySTM)
- `base_stats_commit`, meaning that we only take statistics concerning the turnaround time and the time from last abort to commit
- `base_stats_abort`, meaning that we only take abort statistics (both early abort and normal aborts)
- `base_stats_extend`, meaning that we only take snapshot-extension statistics (both extra-tick driven and TinySTM-driven)

The other three invocations launch, for the scale parameters `5`, `10` and `20`, the following extra-tick experiments:

- `ea_st`, meaning that extra-ticks are issued and revalidations are performed
- `ea_sim_et_kern`, meaning that extra-ticks are issued but revalidations are never performed (the OS kernel filters out all extra-ticks)
- `ea_st_stats_commit`, meaning that we only take statistics concerning the turnaround time and the time from last abort to commit, when extra-ticks are issued
- `ea_st_stats_abort`, meaning that we only take abort statistics (both early abort and normal aborts), when extra-ticks are issued
- `ea_st_stats_extend`, meaning that we only take snapshot-extension statistics (both extra-tick driven and TinySTM-driven), when extra-ticks are issued
