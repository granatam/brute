# brute

## Usage
```
usage: ./main [-l length] [-a alphabet] [-h hash] [-t number] [-p port] [-A addr] [-T timeout] [-s | -m | -g | -c | -L | -S | -v | -w] [-i | -r | -y]
options:
        -l length    password length
        -a alphabet  alphabet
        -h hash      hash
        -t number    number of threads
        -p port      server port
        -A addr      server address
        -T timeout   timeout between task receiving and its processing
run modes:
        -s           singlethreaded mode
        -m           multithreaded mode
        -g           generator mode
        -c           synchronous client mode
        -L number    spawn N load clients
        -S           synchronous server mode
        -v           asynchronous client mode
        -w           asynchronous server mode
brute modes:
        -i           iterative bruteforce
        -r           recursive bruteforce
        -y           recursive generator
```

> Note: Recursive generator is not available on MacOS
