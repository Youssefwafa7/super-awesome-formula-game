#!/usr/bin/env bash
set -u

requirement=""
tolerance=""
threshold=""
files=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        -r|--requirement)
            requirement="$2"
            shift 2
            ;;
        -t|--tolerance)
            tolerance="$2"
            shift 2
            ;;
        -e|--threshold)
            threshold="$2"
            shift 2
            ;;
        --)
            shift
            while [ "$#" -gt 0 ]; do
                files+=("$1")
                shift
            done
            ;;
        *)
            files+=("$1")
            shift
            ;;
    esac
done

if [ -z "$requirement" ] || [ -z "$tolerance" ] || [ -z "$threshold" ] || [ "${#files[@]}" -eq 0 ]; then
    echo "Usage: $0 --requirement NAME --tolerance VALUE --threshold VALUE file1 [file2 ...]"
    exit 2
fi

if [ ! -x ./scripts/imgcmp ]; then
    chmod +x ./scripts/imgcmp 2>/dev/null || true
fi

if [ ! -x ./scripts/imgcmp ]; then
    echo "ERROR: ./scripts/imgcmp is not executable or missing"
    exit 2
fi

expected="expected/$requirement"
output="screenshots/$requirement"
errors="errors/$requirement"

mkdir -p "$errors"

success=0
for file in "${files[@]}"; do
    echo "Testing $file ..."
    ./scripts/imgcmp "$expected/$file" "$output/$file" -o "$errors/$file" -t "$tolerance" -e "$threshold"
    if [ "$?" -eq 0 ]; then
        success=$((success+1))
    fi
done

total="${#files[@]}"
echo
echo "Matches: $success/$total"

if [ "$success" -eq "$total" ]; then
    echo "SUCCESS: All outputs are correct"
    exit 0
else
    failure=$((total-success))
    if [ "$failure" -eq 1 ]; then
        echo "FAILURE: $failure output is incorrect"
    else
        echo "FAILURE: $failure outputs are incorrect"
    fi
    exit "$failure"
fi
