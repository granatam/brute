hash=$(tests/encrypt -p 1234567)
cmd="./main -h $hash -l 7 -a 1234567"
output_single=$($cmd)
output_multi=$($cmd -m)

if [[ "$output_single" = "Password found: 1234567" && "$output_multi" = "$output_single" ]] 
then
    echo "Test 3 passed"
else 
    echo "Test 3 failed"
fi