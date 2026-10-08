// Read a user-exported position without printing any hidden card identities.
#include "../src/position.h"
#include "../src/solver.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) { fprintf(stderr, "usage: solve_position FILE [state-cap<=2000000]\n"); return 3; }
    size_t cap = 500000;
    if (argc == 3) {
        char *end; errno = 0;
        unsigned long parsed = strtoul(argv[2], &end, 10);
        if (errno || *end || !parsed || parsed > 2000000) { fprintf(stderr, "invalid state cap\n"); return 3; }
        cap = parsed;
    }
    FILE *file = fopen(argv[1], "rb");
    uint8_t bytes[OK_POSITION_BYTES + 1];
    if (!file) { fprintf(stderr, "could not read position file\n"); return 3; }
    size_t length = fread(bytes, 1, sizeof(bytes), file);
    int read_error = ferror(file); fclose(file);
    Game game;
    if (read_error || !position_decode(bytes, length, &game)) { fprintf(stderr, "invalid position file\n"); return 3; }
    clock_t start = clock();
    Solver *solver = solver_create_limited(&game, cap);
    if (!solver) { fprintf(stderr, "could not create solver\n"); return 3; }
    while (solver_status(solver) == SOLVER_CHECKING) solver_step(solver, 2048);
    SolverStatus status = solver_status(solver);
    if (status == SOLVER_WINNABLE) {
        Game replay = game;
        for (size_t i = 0; i < solver_solution_length(solver); i++) {
            SolverMove move;
            if (!solver_solution_move(solver, i, &move) || !solver_apply_move(&replay, &move)) {
                fprintf(stderr, "proof replay failed\n"); solver_destroy(solver); return 3;
            }
        }
        if (replay.phase != PHASE_WON) { fprintf(stderr, "proof did not win\n"); solver_destroy(solver); return 3; }
    }
    printf("%s — %zu positions, %zu proof actions, %.3f CPU seconds\n",
           status == SOLVER_WINNABLE ? "WINNABLE" : status == SOLVER_UNWINNABLE ? "UNWINNABLE" : "NOT DETERMINED",
           solver_state_count(solver), solver_solution_length(solver), (double)(clock() - start) / CLOCKS_PER_SEC);
    solver_destroy(solver);
    return status == SOLVER_UNKNOWN ? 2 : 0;
}
