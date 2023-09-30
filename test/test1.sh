cmd="./main"
output_single=$($cmd)
output_multi=$($cmd -m)

if [[ "$output_single" = "Password found: abc" && "$output_multi" = "$output_single" ]] 
then
    echo "Test 1 passed"
else 
    echo "Test 1 failed"
fi