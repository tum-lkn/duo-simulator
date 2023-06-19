# duo-simulator
Packet-level simulator for "Duo: A High-Throughput Reconfigurable Datacenter Network Using Local Routing and Control"

The simulator is an extension of the [Opera Simulator](https://github.com/TritonNetworking/opera-sim) which itself is an extension of the `htsim` [NDP simulator](https://github.com/nets-cs-pub-ro/NDP/tree/master/sim)

## Folders:
- `src`: Source files of the simulator
- `scripts`: Bash scripts to start the simulations
- `topologies`: Octave/Matlab scripts to generate topology files and topology files for Duo
- `traffic_gen`: Octave/Matlab scripts to generate traffic

## Requirements:
- Working installation of Octave/Matlab
- C++ compiler (tested with g++-9)

## Steps:
- Change into `src`. For every topology change into the folder and run `make -j8`. If existing, change into the folder 
`datacenter` and run again `make` (only Expander and Opera)
- Make sure that the needed topology and traffic files exist (Check Opera simulator for details)
- Change into `scripts` and run a specific script. If needed, adapt the paths in the file to match your working directory
(we assume you are in the folder `scripts`)
- Wait (Note that particularly, the Opera simulations occupy quite a lot of memory)
- Evaluation: tbd
