# brute

## Usage
```
usage: ./main [-l length] [-a alphabet] [-h hash] [-t number] [-p port] [-A addr] [-s | -m | -g | -c | -L | -S] [-i | -r | -y]
options:
        -l length    password length
        -a alphabet  alphabet
        -h hash      hash
        -t number    number of threads
        -p port      server port
        -A addr      server address
run modes:
        -s           singlethreaded mode
        -m           multithreaded mode
        -g           generator mode
        -c           client mode
        -L number    spawn N load clients
        -S           server mode
brute modes:
        -i           iterative bruteforce
        -r           recursive bruteforce
        -y           recursive generator
```

> Note: Recursive generator is not available on MacOS
