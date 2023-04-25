
## Build
`mkdir build`
`cd build`
`cmake ..`
`make -jx`

## Run tests

    bin/hydride "y(i)=a(i) + b(i)" -t=y:int -t=A:int -t=x:int

    bin/hydride "y(i)=a(i) + b(i)" -t=y:int -t=A:int -t=x:int -write-source=taco_kernel.c -write-compute=taco_compute.c -write-assembly=taco_assembly.c