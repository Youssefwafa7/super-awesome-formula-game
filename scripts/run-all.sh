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

invoke_tests() {
    local config
    for config in "$@"; do
        ./bin/GAME_APPLICATION -f=2 -c "$config"
    done
}

tests=("$@")

if has_test shader-test; then
    echo
    echo "Running shader-test:"
    echo
    invoke_tests \
        config/shader-test/test-0.jsonc \
        config/shader-test/test-1.jsonc \
        config/shader-test/test-2.jsonc \
        config/shader-test/test-3.jsonc \
        config/shader-test/test-4.jsonc \
        config/shader-test/test-5.jsonc \
        config/shader-test/test-6.jsonc \
        config/shader-test/test-7.jsonc \
        config/shader-test/test-8.jsonc \
        config/shader-test/test-9.jsonc
fi

if has_test mesh-test; then
    echo
    echo "Running mesh-test:"
    echo
    invoke_tests \
        config/mesh-test/default-0.jsonc \
        config/mesh-test/default-1.jsonc \
        config/mesh-test/default-2.jsonc \
        config/mesh-test/default-3.jsonc \
        config/mesh-test/monkey-0.jsonc \
        config/mesh-test/monkey-1.jsonc \
        config/mesh-test/monkey-2.jsonc \
        config/mesh-test/monkey-3.jsonc
fi

if has_test transform-test; then
    echo
    echo "Running transform-test:"
    echo
    invoke_tests config/transform-test/test-0.jsonc
fi

if has_test pipeline-test; then
    echo
    echo "Running pipeline-test:"
    echo
    invoke_tests \
        config/pipeline-test/fc-0.jsonc \
        config/pipeline-test/fc-1.jsonc \
        config/pipeline-test/fc-2.jsonc \
        config/pipeline-test/fc-3.jsonc \
        config/pipeline-test/dt-0.jsonc \
        config/pipeline-test/dt-1.jsonc \
        config/pipeline-test/dt-2.jsonc \
        config/pipeline-test/b-0.jsonc \
        config/pipeline-test/b-1.jsonc \
        config/pipeline-test/b-2.jsonc \
        config/pipeline-test/b-3.jsonc \
        config/pipeline-test/b-4.jsonc \
        config/pipeline-test/cm-0.jsonc \
        config/pipeline-test/dm-0.jsonc
fi

if has_test texture-test; then
    echo
    echo "Running texture-test:"
    echo
    invoke_tests config/texture-test/test-0.jsonc
fi

if has_test sampler-test; then
    echo
    echo "Running sampler-test:"
    echo
    invoke_tests \
        config/sampler-test/test-0.jsonc \
        config/sampler-test/test-1.jsonc \
        config/sampler-test/test-2.jsonc \
        config/sampler-test/test-3.jsonc \
        config/sampler-test/test-4.jsonc \
        config/sampler-test/test-5.jsonc \
        config/sampler-test/test-6.jsonc \
        config/sampler-test/test-7.jsonc
fi

if has_test material-test; then
    echo
    echo "Running material-test:"
    echo
    invoke_tests \
        config/material-test/test-0.jsonc \
        config/material-test/test-1.jsonc
fi

if has_test entity-test; then
    echo
    echo "Running entity-test:"
    echo
    invoke_tests \
        config/entity-test/test-0.jsonc \
        config/entity-test/test-1.jsonc
fi

if has_test renderer-test; then
    echo
    echo "Running renderer-test:"
    echo
    invoke_tests \
        config/renderer-test/test-0.jsonc \
        config/renderer-test/test-1.jsonc
fi

if has_test sky-test; then
    echo
    echo "Running sky-test:"
    echo
    invoke_tests \
        config/sky-test/test-0.jsonc \
        config/sky-test/test-1.jsonc
fi

if has_test postprocess-test; then
    echo
    echo "Running postprocess-test:"
    echo
    invoke_tests \
        config/postprocess-test/test-0.jsonc \
        config/postprocess-test/test-1.jsonc \
        config/postprocess-test/test-2.jsonc \
        config/postprocess-test/test-3.jsonc
fi
