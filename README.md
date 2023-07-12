# duo-simulator
Packet-level simulator for "Duo: A High-Throughput Reconfigurable Datacenter Network Using Local Routing and Control"

The simulator is an extension of the [Opera Simulator](https://github.com/TritonNetworking/opera-sim) which itself is an extension of the `htsim` [NDP simulator](https://github.com/nets-cs-pub-ro/NDP/tree/master/sim)

## Folders:
- `src`: Source files of the simulator
- `scripts`: Bash scripts to start the simulations
- `topologies`: Octave/Matlab scripts to generate topology files and topology files for Duo
- `traffic_gen`: Octave/Matlab scripts to generate traffic
- `evaluation`: Evaluation scripts

## Requirements:
- Working installation of Octave/Matlab
- C++ compiler (tested with g++-9)
- Python3 environment with Numpy, pandas and matplotlib

## Steps:
- Change into `src`. For every topology change into the folder and run `make -j8`. If existing, change into the folder 
`datacenter` and run again `make` (only Expander and Opera)
- Make sure that the needed topology and traffic files exist (Check Opera simulator for details on traffic generation). 
In short, execute the following scripts with `octave`:
  - `generate_traffic.m`: for all flow size distributions (datamining, websearch and hadoop), loads (change variable loadfrac0) and topology sizes
  - `generate_traffic_permutation.m`: for all needed loads (change variable loadfrac0)
  - `generate_traffic_skewed.m`: for all needed loads (change variable loadfrac0)
- Change into `scripts` and run a specific script. If needed, adapt the paths in the file to match your working directory
(we assume you are in the folder `scripts`)
  - Note that the evaluation of the reordering requires to recompile the Opera part with rlb.cpp lines 165-210 
  commented in. These lines are commented out in the other cases to significantly reduce the simulation duration.
- Wait (Note that particularly, the Opera simulations occupy quite a lot of memory)
- Evaluation:
  - The individual `plot_XXX.py` contain different parts of the evaluation (Figures in the paper). All scripts contain 
  a method to preprocess the data (`aggregate_XXX`) and one or more actual plotting function. The `__main__` part at the
  bottom illustrates how to run the methods. Make sure that the folders match to where your simulation data is located.
