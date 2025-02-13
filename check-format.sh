#!/bin/bash

retcode=0

for file in src/*.[ch]; do
    if ! clang-format --dry-run --Werror "${file}"; then
        retcode=1
    fi
done

exit $retcode
