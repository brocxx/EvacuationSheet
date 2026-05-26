/*
 * evacuation.c
 * Full-stack Evacuation Route Planner — C Backend
 * Compile: gcc -o evacuation evacuation.c -lm
 * Run:     ./evacuation [fire|gas] [seed]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ─────────────────────────────────────────────────────── */
/*  CONSTANTS                                              */
/* ─────────────────────────────────────────────────────── */

#define ROWS      20
#define COLS      30
#define MAX_PEOPLE 6
#define MAX_EXITS  4
#define MAX_PATH   600
#define MAX_QUEUE  (ROWS * COLS)
#define MAX_TICKS  60

/* ─────────────────────────────────────────────────────── */
/*  ENUMS                                                  */
/* ─────────────────────────────────────────────────────── */

typedef enum { MOVING, SAFE, TRAPPED, DEAD } Status;
typedef enum { FIRE, GAS } DisasterType;

/* ─────────────────────────────────────────────────────── */
/*  STRUCTS                                                */
/* ─────────────────────────────────────────────────────── */

typedef struct {
    int row, col;
} Cell;

typedef struct {
    int row, col;
    int id;
    Status status;
    int steps_taken;
    int path_r[MAX_PATH];
    int path_c[MAX_PATH];
    int path_len;
    int path_idx;
    int start_r, start_c;
} Person;

typedef struct {
    char cells[ROWS][COLS];
    int tick;
    int alive;
    int safe;
    int dead;
    int trapped;
    int person_row[MAX_PEOPLE];
    int person_col[MAX_PEOPLE];
    char person_status[MAX_PEOPLE][10];
    int person_steps[MAX_PEOPLE];
} TickSnapshot;

/* ─────────────────────────────────────────────────────── */
/*  GLOBALS                                                */
/* ─────────────────────────────────────────────────────── */

char grid[ROWS][COLS];
Person people[MAX_PEOPLE];
int num_people = MAX_PEOPLE;
int num_exits  = 3;
DisasterType disaster_type = FIRE;
int tick = 0;
TickSnapshot snapshots[MAX_TICKS + 1];
int num_snapshots = 0;

/* ─────────────────────────────────────────────────────── */
/*  HELPERS                                                */
/* ─────────────────────────────────────────────────────── */

static int abs_int(int x) { return x < 0 ? -x : x; }

static int in_bounds(int r, int c) {
    return r >= 0 && r < ROWS && c >= 0 && c < COLS;
}

static int manhattan(int r1, int c1, int r2, int c2) {
    return abs_int(r1 - r2) + abs_int(c1 - c2);
}

/* ─────────────────────────────────────────────────────── */
/*  MODULE 1 — RANDOM MAP GENERATOR                        */
/* ─────────────────────────────────────────────────────── */

static int flood_visited[ROWS][COLS];

static void flood_fill(int r, int c) {
    if (!in_bounds(r, c)) return;
    if (flood_visited[r][c]) return;
    if (grid[r][c] == '#') return;
    flood_visited[r][c] = 1;
    flood_fill(r - 1, c);
    flood_fill(r + 1, c);
    flood_fill(r, c - 1);
    flood_fill(r, c + 1);
}

void generate_map(int seed) {
    srand(seed);

    /* Step 1 — Fill with open space */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            grid[r][c] = '.';

    /* Step 2 — Border walls */
    for (int c = 0; c < COLS; c++) { grid[0][c] = '#'; grid[ROWS-1][c] = '#'; }
    for (int r = 0; r < ROWS; r++) { grid[r][0] = '#'; grid[r][COLS-1] = '#'; }

    /* Step 3 — Internal wall clusters */
    int dr[] = {0, 0, 1, -1};
    int dc[] = {1, -1, 0, 0};

    for (int i = 0; i < 18; i++) {
        int cr = 2 + rand() % (ROWS - 4);
        int cc = 2 + rand() % (COLS - 4);
        int len = 3 + rand() % 6;
        int dir = rand() % 4;
        for (int j = 0; j < len; j++) {
            if (cr >= 2 && cr < ROWS - 2 && cc >= 2 && cc < COLS - 2)
                grid[cr][cc] = '#';
            /* Occasionally change direction slightly */
            if (rand() % 3 == 0) dir = rand() % 4;
            cr += dr[dir];
            cc += dc[dir];
            if (!in_bounds(cr, cc)) break;
        }
    }

    /* Flood-fill connectivity fix from center */
    memset(flood_visited, 0, sizeof(flood_visited));
    int center_r = ROWS / 2, center_c = COLS / 2;
    /* Make sure center is open */
    grid[center_r][center_c] = '.';
    flood_fill(center_r, center_c);
    for (int r = 1; r < ROWS - 1; r++)
        for (int c = 1; c < COLS - 1; c++)
            if (grid[r][c] == '#' && !flood_visited[r][c]) {
                /* Already a wall, fine */
            } else if (grid[r][c] == '.' && !flood_visited[r][c]) {
                /* Unreachable open cell — clear any nearby walls */
                grid[r][c] = '.'; /* keep it open and re-flood */
            }

    /* Re-run flood after clearing to update visited */
    memset(flood_visited, 0, sizeof(flood_visited));
    flood_fill(center_r, center_c);

    /* Step 4 — Place exits on border edges (not corners) */
    /* Possible exit positions: top edge, bottom edge, left edge, right edge */
    typedef struct { int r, c; } ExitPos;
    ExitPos exit_candidates[2*(ROWS+COLS)];
    int ne = 0;

    /* top edge */
    for (int c = 2; c < COLS - 2; c++) { exit_candidates[ne].r = 0;       exit_candidates[ne].c = c; ne++; }
    /* bottom edge */
    for (int c = 2; c < COLS - 2; c++) { exit_candidates[ne].r = ROWS-1;  exit_candidates[ne].c = c; ne++; }
    /* left edge */
    for (int r = 2; r < ROWS - 2; r++) { exit_candidates[ne].r = r;       exit_candidates[ne].c = 0; ne++; }
    /* right edge */
    for (int r = 2; r < ROWS - 2; r++) { exit_candidates[ne].r = r;       exit_candidates[ne].c = COLS-1; ne++; }

    /* Shuffle candidates */
    for (int i = ne - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        ExitPos tmp = exit_candidates[i]; exit_candidates[i] = exit_candidates[j]; exit_candidates[j] = tmp;
    }

    int exits_placed = 0;
    int exit_r[MAX_EXITS], exit_c[MAX_EXITS];
    for (int i = 0; i < ne && exits_placed < 3; i++) {
        int er = exit_candidates[i].r;
        int ec = exit_candidates[i].c;
        int ok = 1;
        for (int j = 0; j < exits_placed; j++) {
            if (manhattan(er, ec, exit_r[j], exit_c[j]) < 5) { ok = 0; break; }
        }
        if (ok) {
            exit_r[exits_placed] = er;
            exit_c[exits_placed] = ec;
            grid[er][ec] = 'E';

            /* Open the cell adjacent to the border exit so it's reachable */
            if (er == 0 && in_bounds(er+1, ec)) { if (grid[er+1][ec] == '#') grid[er+1][ec] = '.'; }
            else if (er == ROWS-1 && in_bounds(er-1, ec)) { if (grid[er-1][ec] == '#') grid[er-1][ec] = '.'; }
            else if (ec == 0 && in_bounds(er, ec+1)) { if (grid[er][ec+1] == '#') grid[er][ec+1] = '.'; }
            else if (ec == COLS-1 && in_bounds(er, ec-1)) { if (grid[er][ec-1] == '#') grid[er][ec-1] = '.'; }

            exits_placed++;
        }
    }
    num_exits = exits_placed;

    /* Step 5 — Place people */
    int placed = 0;
    int attempts = 0;
    while (placed < MAX_PEOPLE && attempts < 10000) {
        attempts++;
        int pr = 2 + rand() % (ROWS - 4);
        int pc = 2 + rand() % (COLS - 4);
        if (grid[pr][pc] != '.') continue;
        if (!flood_visited[pr][pc]) continue; /* Must be reachable */

        /* At least 2 away from walls */
        int wall_near = 0;
        for (int dr2 = -2; dr2 <= 2 && !wall_near; dr2++)
            for (int dc2 = -2; dc2 <= 2 && !wall_near; dc2++)
                if (in_bounds(pr+dr2, pc+dc2) && grid[pr+dr2][pc+dc2] == '#')
                    wall_near = 1;
        if (wall_near) continue;

        /* At least 2 away from other people */
        int person_near = 0;
        for (int j = 0; j < placed && !person_near; j++)
            if (manhattan(pr, pc, people[j].row, people[j].col) < 2)
                person_near = 1;
        if (person_near) continue;

        people[placed].row    = pr;
        people[placed].col    = pc;
        people[placed].id     = placed;
        people[placed].status = MOVING;
        people[placed].steps_taken = 0;
        people[placed].path_len    = 0;
        people[placed].path_idx    = 0;
        people[placed].start_r     = pr;
        people[placed].start_c     = pc;
        grid[pr][pc] = 'P';
        placed++;
    }
    num_people = placed;

    /* Step 6 — Place disaster start 'F' */
    attempts = 0;
    int fire_placed = 0;
    while (!fire_placed && attempts < 10000) {
        attempts++;
        int fr = 2 + rand() % (ROWS - 4);
        int fc = 2 + rand() % (COLS - 4);
        if (grid[fr][fc] != '.') continue;
        if (!flood_visited[fr][fc]) continue;

        /* At least 6 away from all exits */
        int exit_near = 0;
        for (int j = 0; j < exits_placed && !exit_near; j++)
            if (manhattan(fr, fc, exit_r[j], exit_c[j]) < 6)
                exit_near = 1;
        if (exit_near) continue;

        /* Not on a person's cell */
        int person_on = 0;
        for (int j = 0; j < num_people && !person_on; j++)
            if (people[j].row == fr && people[j].col == fc)
                person_on = 1;
        if (person_on) continue;

        grid[fr][fc] = 'F';
        fire_placed = 1;

        /* Gas starts with cluster of 2 more adjacent cells */
        if (disaster_type == GAS) {
            int adj4r[] = {-1,1,0,0};
            int adj4c[] = {0,0,-1,1};
            int added = 0;
            for (int d = 0; d < 4 && added < 2; d++) {
                int nr = fr + adj4r[d];
                int nc = fc + adj4c[d];
                if (in_bounds(nr, nc) && grid[nr][nc] == '.') {
                    grid[nr][nc] = 'F';
                    added++;
                }
            }
        }
    }
    if (!fire_placed) {
        /* Fallback: place fire somewhere open */
        for (int r = 2; r < ROWS-2 && !fire_placed; r++)
            for (int c = 2; c < COLS-2 && !fire_placed; c++)
                if (grid[r][c] == '.') { grid[r][c] = 'F'; fire_placed = 1; }
    }
}

/* ─────────────────────────────────────────────────────── */
/*  MODULE 2 — DISASTER ENGINE                             */
/* ─────────────────────────────────────────────────────── */

void spread_disaster(void) {
    char temp[ROWS][COLS];
    memcpy(temp, grid, sizeof(grid));

    int dirs4r[] = {-1,1,0,0};
    int dirs4c[] = {0,0,-1,1};
    int dirs8r[] = {-1,-1,-1,0,0,1,1,1};
    int dirs8c[] = {-1,0,1,-1,1,-1,0,1};

    int nd = (disaster_type == FIRE) ? 4 : 8;
    int *drs = (disaster_type == FIRE) ? dirs4r : dirs8r;
    int *dcs = (disaster_type == FIRE) ? dirs4c : dirs8c;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (grid[r][c] == 'F') {
                for (int d = 0; d < nd; d++) {
                    int nr = r + drs[d];
                    int nc = c + dcs[d];
                    if (!in_bounds(nr, nc)) continue;
                    if (temp[nr][nc] == '#' || temp[nr][nc] == 'E') continue;
                    temp[nr][nc] = 'F';
                }
            }
        }
    }
    memcpy(grid, temp, sizeof(grid));
}

/* ─────────────────────────────────────────────────────── */
/*  MODULE 3 — BFS PATHFINDER                              */
/* ─────────────────────────────────────────────────────── */

int bfs_find_path(Person *p) {
    int visited[ROWS][COLS];
    int prev_r[ROWS][COLS];
    int prev_c[ROWS][COLS];
    memset(visited, 0, sizeof(visited));
    memset(prev_r, -1, sizeof(prev_r));
    memset(prev_c, -1, sizeof(prev_c));

    Cell queue[MAX_QUEUE];
    int head = 0, tail = 0;

    queue[tail].row = p->row;
    queue[tail].col = p->col;
    tail++;
    visited[p->row][p->col] = 1;

    int found_r = -1, found_c = -1;

    int dr[] = {-1,1,0,0};
    int dc[] = {0,0,-1,1};

    while (head < tail) {
        Cell cur = queue[head++];
        if (grid[cur.row][cur.col] == 'E') {
            found_r = cur.row;
            found_c = cur.col;
            break;
        }
        for (int d = 0; d < 4; d++) {
            int nr = cur.row + dr[d];
            int nc = cur.col + dc[d];
            if (!in_bounds(nr, nc)) continue;
            if (visited[nr][nc]) continue;
            /* Blocked by walls or fire */
            char cell = grid[nr][nc];
            if (cell == '#' || cell == 'F') continue;
            visited[nr][nc] = 1;
            prev_r[nr][nc] = cur.row;
            prev_c[nr][nc] = cur.col;
            queue[tail].row = nr;
            queue[tail].col = nc;
            tail++;
        }
    }

    if (found_r == -1) {
        p->status = TRAPPED;
        p->path_len = 0;
        return 0;
    }

    /* Reconstruct path (from exit back to start) */
    int tmp_r[MAX_PATH], tmp_c[MAX_PATH];
    int len = 0;
    int cr = found_r, cc = found_c;
    while (cr != -1 && cc != -1 && len < MAX_PATH) {
        tmp_r[len] = cr;
        tmp_c[len] = cc;
        len++;
        int pr2 = prev_r[cr][cc];
        int pc2 = prev_c[cr][cc];
        cr = pr2; cc = pc2;
    }
    /* Reverse */
    p->path_len = 0;
    for (int i = len - 1; i >= 0; i--) {
        p->path_r[p->path_len] = tmp_r[i];
        p->path_c[p->path_len] = tmp_c[i];
        p->path_len++;
    }
    p->path_idx = 0;
    return 1;
}

/* ─────────────────────────────────────────────────────── */
/*  MODULE 4 — MOVE PEOPLE                                 */
/* ─────────────────────────────────────────────────────── */

void move_people(void) {
    for (int i = 0; i < num_people; i++) {
        Person *p = &people[i];
        if (p->status != MOVING) continue;

        /* Re-run BFS fresh every tick */
        if (!bfs_find_path(p)) continue; /* TRAPPED set inside */

        /* Skip first element (current position) */
        int next_idx = 1;
        if (p->path_len <= next_idx) continue;

        int nr = p->path_r[next_idx];
        int nc = p->path_c[next_idx];

        /* Clear old cell */
        if (grid[p->row][p->col] == 'P' || grid[p->row][p->col] == 'X')
            grid[p->row][p->col] = '.';

        /* Move */
        p->row = nr;
        p->col = nc;
        p->steps_taken++;

        /* Mark new cell (unless it's an exit) */
        if (grid[nr][nc] != 'E')
            grid[nr][nc] = 'X';
    }
}

/* ─────────────────────────────────────────────────────── */
/*  MODULE 5 — STATUS CHECKER                              */
/* ─────────────────────────────────────────────────────── */

void update_status(void) {
    for (int i = 0; i < num_people; i++) {
        Person *p = &people[i];
        if (p->status != MOVING) continue;

        char cell = grid[p->row][p->col];
        if (cell == 'F') {
            p->status = DEAD;
            grid[p->row][p->col] = 'F'; /* Fire consumed the person */
        } else if (cell == 'E') {
            p->status = SAFE;
        }
    }
}

/* ─────────────────────────────────────────────────────── */
/*  MODULE 6 — SNAPSHOT RECORDER                           */
/* ─────────────────────────────────────────────────────── */

void record_snapshot(void) {
    if (num_snapshots > MAX_TICKS) return;
    TickSnapshot *s = &snapshots[num_snapshots];

    memcpy(s->cells, grid, sizeof(grid));
    s->tick    = tick;
    s->alive   = 0;
    s->safe    = 0;
    s->dead    = 0;
    s->trapped = 0;

    for (int i = 0; i < num_people; i++) {
        s->person_row[i]   = people[i].row;
        s->person_col[i]   = people[i].col;
        s->person_steps[i] = people[i].steps_taken;

        switch (people[i].status) {
            case MOVING:  strcpy(s->person_status[i], "MOVING");  s->alive++;   break;
            case SAFE:    strcpy(s->person_status[i], "SAFE");    s->safe++;    break;
            case DEAD:    strcpy(s->person_status[i], "DEAD");    s->dead++;    break;
            case TRAPPED: strcpy(s->person_status[i], "TRAPPED"); s->trapped++; break;
        }
    }
    num_snapshots++;
}

/* ─────────────────────────────────────────────────────── */
/*  MODULE 7 — JSON EXPORTER                               */
/* ─────────────────────────────────────────────────────── */

void export_json(void) {
    FILE *f = fopen("simulation.json", "w");
    if (!f) { fprintf(stderr, "ERROR: Cannot open simulation.json for writing\n"); return; }

    fprintf(f, "{\n");
    fprintf(f, "  \"rows\": %d,\n", ROWS);
    fprintf(f, "  \"cols\": %d,\n", COLS);
    fprintf(f, "  \"num_people\": %d,\n", num_people);
    fprintf(f, "  \"disaster\": \"%s\",\n", disaster_type == FIRE ? "FIRE" : "GAS");
    fprintf(f, "  \"ticks\": [\n");

    for (int t = 0; t < num_snapshots; t++) {
        TickSnapshot *s = &snapshots[t];
        fprintf(f, "    {\n");
        fprintf(f, "      \"tick\": %d,\n", s->tick);
        fprintf(f, "      \"alive\": %d,\n", s->alive);
        fprintf(f, "      \"safe\": %d,\n", s->safe);
        fprintf(f, "      \"dead\": %d,\n", s->dead);
        fprintf(f, "      \"trapped\": %d,\n", s->trapped);
        fprintf(f, "      \"grid\": [\n");
        for (int r = 0; r < ROWS; r++) {
            fprintf(f, "        \"");
            for (int c = 0; c < COLS; c++) {
                char ch = s->cells[r][c];
                /* Escape backslash and quote just in case */
                if (ch == '"') fprintf(f, "\\\"");
                else if (ch == '\\') fprintf(f, "\\\\");
                else fprintf(f, "%c", ch);
            }
            fprintf(f, "\"");
            if (r < ROWS - 1) fprintf(f, ",");
            fprintf(f, "\n");
        }
        fprintf(f, "      ],\n");
        fprintf(f, "      \"people\": [\n");
        for (int i = 0; i < num_people; i++) {
            fprintf(f, "        { \"id\": %d, \"row\": %d, \"col\": %d, \"status\": \"%s\", \"steps\": %d }",
                    i, s->person_row[i], s->person_col[i], s->person_status[i], s->person_steps[i]);
            if (i < num_people - 1) fprintf(f, ",");
            fprintf(f, "\n");
        }
        fprintf(f, "      ]\n");
        fprintf(f, "    }");
        if (t < num_snapshots - 1) fprintf(f, ",");
        fprintf(f, "\n");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    printf("simulation.json written (%d ticks).\n", num_snapshots);
}

/* ─────────────────────────────────────────────────────── */
/*  MODULE 8 — TERMINAL PRINTER                            */
/* ─────────────────────────────────────────────────────── */

void print_grid_terminal(void) {
    printf("\n=== Tick %d | Disaster: %s ===\n", tick, disaster_type == FIRE ? "FIRE" : "GAS");
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            printf("%c ", grid[r][c]);
        }
        printf("\n");
    }
    printf("People: ");
    for (int i = 0; i < num_people; i++) {
        const char *stat;
        switch (people[i].status) {
            case MOVING:  stat = "MOV"; break;
            case SAFE:    stat = "SAF"; break;
            case DEAD:    stat = "DED"; break;
            case TRAPPED: stat = "TRP"; break;
            default:      stat = "???"; break;
        }
        printf("P%d(%s,s%d) ", i, stat, people[i].steps_taken);
    }
    printf("\n");
}

/* ─────────────────────────────────────────────────────── */
/*  MAIN                                                   */
/* ─────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    int seed = (int)time(NULL);

    /* Parse disaster type */
    if (argc >= 2) {
        if (strcmp(argv[1], "gas") == 0) disaster_type = GAS;
        else disaster_type = FIRE;
    }

    /* Parse seed */
    if (argc >= 3) seed = atoi(argv[2]);

    printf("Evacuation Simulator\n");
    printf("Disaster: %s | Seed: %d\n", disaster_type == FIRE ? "FIRE" : "GAS", seed);

    generate_map(seed);

    /* Tick 0 snapshot */
    tick = 0;
    record_snapshot();
    print_grid_terminal();

    /* Simulation loop */
    while (tick < MAX_TICKS) {
        move_people();
        spread_disaster();
        update_status();
        tick++;
        record_snapshot();
        print_grid_terminal();

        /* Break if all people have final status */
        int all_done = 1;
        for (int i = 0; i < num_people; i++) {
            if (people[i].status == MOVING) { all_done = 0; break; }
        }
        if (all_done) {
            printf("All people resolved at tick %d. Ending early.\n", tick);
            break;
        }
    }

    /* Final summary */
    int final_safe = 0, final_dead = 0, final_trapped = 0;
    for (int i = 0; i < num_people; i++) {
        if (people[i].status == SAFE)    final_safe++;
        if (people[i].status == DEAD)    final_dead++;
        if (people[i].status == TRAPPED) final_trapped++;
    }
    printf("\n══════════════════════════════\n");
    printf(" FINAL SUMMARY\n");
    printf("══════════════════════════════\n");
    printf(" Total Ticks:   %d\n", tick);
    printf(" Escaped:       %d / %d\n", final_safe, num_people);
    printf(" Dead:          %d\n", final_dead);
    printf(" Trapped:       %d\n", final_trapped);
    printf(" Safety Score:  %.0f%%\n", num_people > 0 ? (100.0 * final_safe / num_people) : 0.0);
    printf("══════════════════════════════\n");

    export_json();
    return 0;
}
