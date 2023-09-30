hash=$(test/encrypt -p artemiy)
cmd="./main -h $hash -l 7 -a artimey"
output_single=$($cmd)
output_multi=$($cmd -m)

if [[ "$output_single" = "Password found: artemiy" && "$output_multi" = "$output_single" ]] 
then
    echo "Test 2 passed"
else 
    echo "Test 2 failed"
fi