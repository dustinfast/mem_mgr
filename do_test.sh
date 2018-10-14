ssh_ubuntu
ptc2012
cd a3
gcc -fPIC -Wall -g -O0 -c memory.c 
gcc -fPIC -Wall -g -O0 -c implementation.c
gcc -fPIC -shared -o memory.so memory.o implementation.o -lpthread

export LD_LIBRARY_PATH=`pwd`:"$LD_LIBRARY_PATH"
export LD_PRELOAD=`pwd`/memory.so 
export MEMORY_DEBUG=yes
ls
