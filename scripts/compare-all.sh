#!/usr/bin/env bash
set -u

has_test() {
    local wanted="$1"
    if [ "$#" -eq 1 ] && [ "${#tests[@]}" -eq 0 ]; then
        return 0
    fi

    local t
    for t in "${tests[@]}"; do
        if [ "$t" = "$wanted" ]; then
            return 0
        fi
    done
    return 1
}

run_group() {
    local requirement="$1"
    local tolerance="$2"
    local threshold="$3"
    shift 3

    echo
    echo "Comparing $requirement output:"
    ./scripts/compare-group.sh --requirement "$requirement" --tolerance "$tolerance" --threshold "$threshold" "$@"
    local ec=$?
    failure=$((failure + ec))
}

tests=("$@")
failure=0

if has_test shader-test; then
    run_group shader-test 0.01 0 \
        test-0.png test-1.png test-2.png test-3.png test-4.png \
        test-5.png test-6.png test-7.png test-8.png test-9.png
fi

if has_test mesh-test; then
    run_group mesh-test 0.01 0 \
        default-0.png default-1.png default-2.png default-3.png \
        monkey-0.png monkey-1.png monkey-2.png monkey-3.png
fi

if has_test transform-test; then
    run_group transform-test 0.01 0 test-0.png
fi

if has_test pipeline-test; then
    run_group pipeline-test 0.01 64 \
        fc-0.png fc-1.png fc-2.png fc-3.png \
        dt-0.png dt-1.png dt-2.png \
        b-0.png b-1.png b-2.png b-3.png b-4.png \
        cm-0.png dm-0.png
fi

if has_test texture-test; then
    run_group texture-test 0.01 0 test-0.png
fi

if has_test sampler-test; then
    run_group sampler-test 0.01 0 \
        test-0.png test-1.png test-2.png test-3.png \
        test-4.png test-5.png test-6.png test-7.png
fi

if has_test material-test; then
    run_group material-test 0.02 64 test-0.png test-1.png
fi

if has_test entity-test; then
    run_group entity-test 0.04 64 test-0.png test-1.png
fi

if has_test renderer-test; then
    run_group renderer-test 0.04 64 test-0.png test-1.png
fi

if has_test sky-test; then
    run_group sky-test 0.04 64 test-0.png test-1.png
fi

if has_test postprocess-test; then
    run_group postprocess-test 0.04 64 test-0.png test-1.png test-2.png test-3.png
fi

echo
echo "Overall Results"
if [ "$failure" -eq 0 ]; then
    echo "SUCCESS: All outputs are correct"
else
    if [ "$failure" -eq 1 ]; then
        echo "FAILURE: $failure output is incorrect"
    else
        echo "FAILURE: $failure outputs are incorrect"
    fi
fi

exit "$failure"
