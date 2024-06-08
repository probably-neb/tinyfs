watch:
    watchexec -e zig,c,h -rc -- just test

test:
    zig test ./tests/test.zig -lc -I.
