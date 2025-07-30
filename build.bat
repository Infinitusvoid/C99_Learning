@echo off
echo --- Compiling C99 Window ---

clang -std=c99 -g -Wall main.c -o window.exe -luser32 -lgdi32

echo --- Finished ---
pause