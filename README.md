# brute

## Usage

```
Usage:
./brute [options] [run mode] [brute mode]

Options:
        -l, --length uint        password length
        -a, --alph str           alphabet
        -h, --hash str           hash
        -t, --threads uint       number of threads
        -p, --port uint          server port
        -A, --addr str           server address
        -T, --timeout uint       timeout between task receiving and its processing in milliseconds

Run modes:
        -s, --single             singlethreaded mode
        -m, --multi              multithreaded mode
        -g, --gen                generator mode
        -c, --client             synchronous client mode
        -L, --load-clients uint  spawn N load clients
        -S, --server             synchronous server mode
        -v, --async-client       asynchronous client mode
        -w, --async-server       asynchronous server mode

Brute modes:
        -i, --iter               iterative bruteforce
        -r, --rec                recursive bruteforce
        -y, --rec-gen            recursive generator
```

> Note: Recursive generator is not available on MacOS
