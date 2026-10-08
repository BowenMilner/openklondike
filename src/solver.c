#include "solver.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* 52 six-bit card ids, thirteen six-bit pile sizes, mode, and face-up bits. */
#define KEY_SIZE 57
#define DEFAULT_MAX_STATES 100000u
#define CACHE_SLOTS 8192u
#define CACHE_MAX_ENTRIES 4096u
#define MAX_ACTIONS 1024u
#define MAX_SYNC_JOIN_STEPS 1024u

typedef struct { uint8_t bytes[KEY_SIZE]; } StateKey;

typedef struct {
    StateKey key;
    int32_t parent;
    SolverMove move;
} SearchNode;

typedef struct {
    size_t refs;
    size_t length;
    SolverMove *moves;
} ProofBundle;

typedef struct {
    StateKey key;
    ProofBundle *bundle;
    size_t offset;
    uint8_t used;
} CacheEntry;

typedef struct {
    SolverMove move;
    int priority;
} Candidate;

struct Solver {
    SolverStatus status;
    bool cancelled;
    size_t max_states;
    size_t node_count;
    size_t expanded;
    size_t table_capacity;
    SearchNode *nodes;
    uint32_t *visited;
    uint32_t *frontier;
    size_t frontier_count;
    ProofBundle *cached_bundle;
    size_t cached_offset;
    SolverMove *solution;
    size_t solution_length;
};

static CacheEntry g_cache[CACHE_MAX_ENTRIES];
static uint32_t g_cache_index[CACHE_SLOTS];
static size_t g_cache_next;
static size_t g_bundle_bytes;
static size_t g_cache_mutations;

static uint64_t key_hash(const StateKey *key) {
    uint64_t h = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < KEY_SIZE; i++) {
        h ^= key->bytes[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static bool is_won(const Game *g) {
    int i;
    for (i = 0; i < 4; i++) if (g->foundation[i].count != 13) return false;
    return true;
}

static int card_id(Card c) {
    return (int)c.suit * 13 + (int)c.rank - 1;
}

static bool validate_game(const Game *g) {
    bool seen[52] = { false };
    int total = 0, i, j;
    const Pile *p;
    if (!g || (g->draw_mode != DRAW_ONE && g->draw_mode != DRAW_THREE)) return false;
    if (g->phase != PHASE_PLAY && g->phase != PHASE_WON) return false;
    p = &g->stock;
    if (p->count < 0 || p->count > 52) return false;
    for (j = 0; j < p->count; j++) {
        Card c = p->cards[j]; int id;
        if (c.rank < 1 || c.rank > 13 || c.suit > 3 || c.face_up) return false;
        id = card_id(c); if (seen[id]) return false; seen[id] = true; total++;
    }
    p = &g->waste;
    if (p->count < 0 || p->count > 52) return false;
    for (j = 0; j < p->count; j++) {
        Card c = p->cards[j]; int id;
        if (c.rank < 1 || c.rank > 13 || c.suit > 3 || c.face_up != 1) return false;
        id = card_id(c); if (seen[id]) return false; seen[id] = true; total++;
    }
    for (i = 0; i < 4; i++) {
        int foundation_suit = -1;
        p = &g->foundation[i];
        if (p->count < 0 || p->count > 13) return false;
        for (j = 0; j < p->count; j++) {
            Card c = p->cards[j]; int id;
            if (c.rank != j + 1 || c.suit > 3 || c.face_up != 1) return false;
            if (foundation_suit < 0) foundation_suit = c.suit;
            if (c.suit != foundation_suit) return false;
            id = card_id(c); if (seen[id]) return false; seen[id] = true; total++;
        }
    }
    for (i = 0; i < 7; i++) {
        p = &g->tableau[i];
        int first_up = -1;
        if (p->count < 0 || p->count > 52) return false;
        for (j = 0; j < p->count; j++) {
            Card c = p->cards[j]; int id;
            if (c.rank < 1 || c.rank > 13 || c.suit > 3 || c.face_up > 1) return false;
            if (c.face_up && first_up < 0) first_up = j;
            if (!c.face_up && first_up >= 0) return false;
            id = card_id(c); if (seen[id]) return false; seen[id] = true; total++;
        }
        if (p->count && first_up != p->count - 1) {
            if (first_up < 0) return false;
            for (j = first_up; j < p->count - 1; j++) {
                Card a = p->cards[j], b = p->cards[j + 1];
                if (b.rank != a.rank - 1 || card_is_red(a) == card_is_red(b)) return false;
            }
        } else if (p->count && first_up >= 0) {
            for (j = first_up; j < p->count - 1; j++) {
                Card a = p->cards[j], b = p->cards[j + 1];
                if (b.rank != a.rank - 1 || card_is_red(a) == card_is_red(b)) return false;
            }
        }
    }
    if (total != 52) return false;
    if (g->phase == PHASE_WON && !is_won(g)) return false;
    return true;
}

static void put_bits(uint8_t *bytes, unsigned *bit, unsigned value, unsigned count) {
    unsigned i;
    for (i = 0; i < count; i++, (*bit)++)
        if ((value >> i) & 1u) bytes[*bit >> 3] |= (uint8_t)(1u << (*bit & 7u));
}

static unsigned get_bits(const uint8_t *bytes, unsigned *bit, unsigned count) {
    unsigned i, value = 0;
    for (i = 0; i < count; i++, (*bit)++)
        value |= ((bytes[*bit >> 3] >> (*bit & 7u)) & 1u) << i;
    return value;
}

static void pack_pile(const Pile *p, StateKey *key, unsigned *card_bit,
                      unsigned *face_bit) {
    int i;
    for (i = 0; i < p->count; i++) {
        put_bits(key->bytes + 11, card_bit, (unsigned)card_id(p->cards[i]), 6);
        if (p->cards[i].face_up)
            key->bytes[50 + (*face_bit >> 3)] |= (uint8_t)(1u << (*face_bit & 7u));
        (*face_bit)++;
    }
}

static bool pack_game(const Game *g, StateKey *key) {
    const Pile *piles[13];
    unsigned count_bit = 0, card_bit = 0, face_bit = 0;
    int i;
    if (!validate_game(g)) return false;
    memset(key, 0, sizeof(*key));
    piles[0] = &g->stock; piles[1] = &g->waste;
    for (i = 0; i < 4; i++) piles[2 + i] = &g->foundation[i];
    for (i = 0; i < 7; i++) piles[6 + i] = &g->tableau[i];
    for (i = 0; i < 13; i++) put_bits(key->bytes, &count_bit, (unsigned)piles[i]->count, 6);
    key->bytes[10] = (uint8_t)g->draw_mode;
    for (i = 0; i < 13; i++) pack_pile(piles[i], key, &card_bit, &face_bit);
    return card_bit == 312 && face_bit == 52;
}

static void unpack_game(const StateKey *key, Game *g) {
    Pile *piles[13];
    unsigned count_bit = 0, card_bit = 0, face_bit = 0;
    unsigned counts[13];
    int i, j;
    memset(g, 0, sizeof(*g));
    g->draw_mode = (DrawMode)key->bytes[10];
    g->phase = PHASE_PLAY;
    piles[0] = &g->stock; piles[1] = &g->waste;
    for (i = 0; i < 4; i++) piles[2 + i] = &g->foundation[i];
    for (i = 0; i < 7; i++) piles[6 + i] = &g->tableau[i];
    for (i = 0; i < 13; i++) counts[i] = get_bits(key->bytes, &count_bit, 6);
    for (i = 0; i < 13; i++) {
        piles[i]->count = (int)counts[i];
        for (j = 0; j < piles[i]->count; j++) {
            unsigned id = get_bits(key->bytes + 11, &card_bit, 6);
            bool face = (key->bytes[50 + (face_bit >> 3)] & (1u << (face_bit & 7u))) != 0;
            piles[i]->cards[j] = (Card){ (uint8_t)(id % 13 + 1), (uint8_t)(id / 13), (uint8_t)face };
            face_bit++;
        }
    }
}

static CacheEntry *cache_find(const StateKey *key) {
    size_t slot = (size_t)key_hash(key) & (CACHE_SLOTS - 1u), probes;
    for (probes = 0; probes < CACHE_SLOTS; probes++, slot = (slot + 1u) & (CACHE_SLOTS - 1u)) {
        uint32_t stored = g_cache_index[slot];
        if (stored == 0) return NULL;
        if (stored != UINT32_MAX) {
            CacheEntry *entry = &g_cache[stored - 1u];
            if (entry->used && memcmp(entry->key.bytes, key->bytes, KEY_SIZE) == 0) return entry;
        }
    }
    return NULL;
}

static void cache_index_remove(const StateKey *key);
static void bundle_release(ProofBundle *bundle);

static void cache_evict_oldest(void) {
    CacheEntry *entry = &g_cache[g_cache_next];
    if (entry->used) {
        cache_index_remove(&entry->key);
        bundle_release(entry->bundle);
        memset(entry, 0, sizeof(*entry));
    }
    g_cache_next = (g_cache_next + 1u) % CACHE_MAX_ENTRIES;
}

static void cache_index_remove(const StateKey *key) {
    size_t slot = (size_t)key_hash(key) & (CACHE_SLOTS - 1u), probes;
    for (probes = 0; probes < CACHE_SLOTS; probes++, slot = (slot + 1u) & (CACHE_SLOTS - 1u)) {
        uint32_t stored = g_cache_index[slot];
        if (stored == 0) return;
        if (stored != UINT32_MAX &&
            memcmp(g_cache[stored - 1u].key.bytes, key->bytes, KEY_SIZE) == 0) {
            g_cache_index[slot] = UINT32_MAX;
            return;
        }
    }
}

static void bundle_release(ProofBundle *bundle) {
    if (!bundle) return;
    if (bundle->refs) bundle->refs--;
    if (bundle->refs == 0) {
        g_bundle_bytes -= sizeof(*bundle) + bundle->length * sizeof(*bundle->moves);
        free(bundle->moves);
        free(bundle);
    }
}

static void cache_index_add(const StateKey *key, size_t entry_index) {
    size_t slot = (size_t)key_hash(key) & (CACHE_SLOTS - 1u), first_deleted = SIZE_MAX;
    size_t probes;
    for (probes = 0; probes < CACHE_SLOTS; probes++) {
        uint32_t stored = g_cache_index[slot];
        if (stored == UINT32_MAX && first_deleted == SIZE_MAX) first_deleted = slot;
        if (stored == 0) {
            if (first_deleted != SIZE_MAX) slot = first_deleted;
            g_cache_index[slot] = (uint32_t)entry_index + 1u;
            return;
        }
        slot = (slot + 1u) & (CACHE_SLOTS - 1u);
    }
    if (first_deleted != SIZE_MAX) g_cache_index[first_deleted] = (uint32_t)entry_index + 1u;
}

static void cache_index_rebuild(void) {
    size_t i;
    memset(g_cache_index, 0, sizeof(g_cache_index));
    for (i = 0; i < CACHE_MAX_ENTRIES; i++)
        if (g_cache[i].used) cache_index_add(&g_cache[i].key, i);
}

static void cache_add(const StateKey *key, ProofBundle *bundle, size_t offset) {
    CacheEntry *entry = cache_find(key);
    if (entry) {
        if (entry->bundle != bundle || entry->offset != offset) {
            if (bundle) bundle->refs++;
            bundle_release(entry->bundle);
            entry->bundle = bundle;
            entry->offset = offset;
        }
        return;
    }
    entry = &g_cache[g_cache_next];
    if (entry->used) {
        cache_index_remove(&entry->key);
        bundle_release(entry->bundle);
    } else {
        /* The FIFO slot was never occupied. */
    }
    entry->used = 1;
    entry->key = *key;
    entry->bundle = bundle;
    entry->offset = offset;
    if (bundle) bundle->refs++;
    cache_index_add(key, g_cache_next);
    g_cache_next = (g_cache_next + 1u) % CACHE_MAX_ENTRIES;
    if (++g_cache_mutations % 1024u == 0) cache_index_rebuild();
}

static ProofBundle *bundle_create(const SolverMove *moves, size_t count) {
    ProofBundle *bundle;
    size_t bytes;
    if (count > (SIZE_MAX - sizeof(*bundle)) / sizeof(*moves)) return NULL;
    bytes = sizeof(*bundle) + count * sizeof(*moves);
    while (bytes <= 8u * 1024u * 1024u &&
           bytes > 8u * 1024u * 1024u - g_bundle_bytes &&
           g_cache[g_cache_next].used) cache_evict_oldest();
    if (bytes > 8u * 1024u * 1024u - g_bundle_bytes) return NULL;
    bundle = (ProofBundle *)calloc(1, sizeof(*bundle));
    if (!bundle) return NULL;
    if (count) {
        bundle->moves = (SolverMove *)malloc(count * sizeof(*moves));
        if (!bundle->moves) { free(bundle); return NULL; }
        memcpy(bundle->moves, moves, count * sizeof(*moves));
    }
    bundle->length = count;
    g_bundle_bytes += bytes;
    return bundle;
}

static bool winning_path_cache(const StateKey *keys, const SolverMove *moves,
                               size_t count) {
    ProofBundle *bundle = bundle_create(moves, count);
    size_t i;
    if (!bundle) return false;
    for (i = 0; i <= count; i++) cache_add(&keys[i], bundle, i);
    if (bundle->refs == 0) {
        g_bundle_bytes -= sizeof(*bundle) + bundle->length * sizeof(*bundle->moves);
        free(bundle->moves); free(bundle);
    }
    return true;
}

static size_t table_size_for(size_t states) {
    size_t n = 1024u;
    if (states > SIZE_MAX / 2u) return 0;
    while (n < states * 2u) {
        if (n > SIZE_MAX / 2u) return 0;
        n *= 2u;
    }
    return n;
}

static bool visited_insert(Solver *s, const StateKey *key, uint32_t node) {
    size_t slot = (size_t)key_hash(key) & (s->table_capacity - 1u), probes;
    for (probes = 0; probes < s->table_capacity; probes++, slot = (slot + 1u) & (s->table_capacity - 1u)) {
        uint32_t stored = s->visited[slot];
        if (!stored) { s->visited[slot] = node + 1u; return true; }
        if (memcmp(s->nodes[stored - 1u].key.bytes, key->bytes, KEY_SIZE) == 0) return false;
    }
    return false;
}

static bool visited_contains(const Solver *s, const StateKey *key) {
    size_t slot = (size_t)key_hash(key) & (s->table_capacity - 1u), probes;
    for (probes = 0; probes < s->table_capacity; probes++, slot = (slot + 1u) & (s->table_capacity - 1u)) {
        uint32_t stored = s->visited[slot];
        if (!stored) return false;
        if (memcmp(s->nodes[stored - 1u].key.bytes, key->bytes, KEY_SIZE) == 0) return true;
    }
    return false;
}

static int candidate_priority(const Game *g, const SolverMove *m) {
    const Pile *src = NULL;
    Card c = { 0, 0, 0 };
    if (m->type == SOLVER_MOVE_DRAW) return 0;
    switch ((PileKind)m->from_kind) {
        case LOC_WASTE: src = &g->waste; break;
        case LOC_FOUNDATION: src = &g->foundation[m->from_index]; break;
        case LOC_TABLEAU: src = &g->tableau[m->from_index]; break;
        default: return -10000;
    }
    if (m->card_index < src->count) c = src->cards[m->card_index];
    if (m->from_kind == LOC_FOUNDATION) return -1000;
    if (m->to_kind == LOC_FOUNDATION) return 500;
    if (m->from_kind == LOC_WASTE) return 1200;
    if (m->from_kind == LOC_TABLEAU) {
        const Pile *tableau = &g->tableau[m->from_index];
        if (m->card_index > 0 && !tableau->cards[m->card_index - 1].face_up)
            return 10000 + (int)(m->card_index - 1);
        if (m->card_index == 0 && c.rank == 13 && tableau->count > 1) return 250;
        return 150;
    }
    return 10;
}

static void add_candidate(Candidate *items, size_t *count, const Game *g,
                          SolverMove move) {
    size_t i;
    Candidate c;
    if (*count >= MAX_ACTIONS) return;
    c.move = move; c.priority = candidate_priority(g, &move);
    i = *count;
    while (i > 0 && items[i - 1u].priority > c.priority) {
        items[i] = items[i - 1u]; i--;
    }
    items[i] = c; (*count)++;
}

static size_t enumerate_moves(const Game *g, Candidate *items, bool *overflow) {
    size_t count = 0;
    int from, index, to;
    *overflow = false;
    if (g->stock.count > 0 || g->waste.count > 0) {
        SolverMove draw = { SOLVER_MOVE_DRAW, LOC_STOCK, 0, 0, LOC_STOCK, 0 };
        add_candidate(items, &count, g, draw);
    }
    for (from = 0; from < 3; from++) {
        int n = from == 0 ? (g->waste.count ? 1 : 0) : from == 1 ? 4 : 7;
        PileKind kind = from == 0 ? LOC_WASTE : from == 1 ? LOC_FOUNDATION : LOC_TABLEAU;
        for (index = 0; index < n; index++) {
            const Pile *src = kind == LOC_WASTE ? &g->waste
                              : kind == LOC_FOUNDATION ? &g->foundation[index]
                              : &g->tableau[index];
            int first = kind == LOC_TABLEAU ? 0 : src->count - 1;
            if (src->count == 0) continue;
            for (; first < src->count; first++) {
                if (!game_can_grab(g, kind, index, first)) continue;
                for (to = 0; to < 4; to++) {
                    SolverMove m;
                    if (!game_can_drop(g, kind, index, first, LOC_FOUNDATION, to)) continue;
                    m = (SolverMove){ SOLVER_MOVE_CARD, (uint8_t)kind, (uint8_t)index,
                                      (uint8_t)first, LOC_FOUNDATION, (uint8_t)to };
                    if (count >= MAX_ACTIONS) { *overflow = true; return count; }
                    add_candidate(items, &count, g, m);
                }
                for (to = 0; to < 7; to++) {
                    SolverMove m;
                    if (!game_can_drop(g, kind, index, first, LOC_TABLEAU, to)) continue;
                    m = (SolverMove){ SOLVER_MOVE_CARD, (uint8_t)kind, (uint8_t)index,
                                      (uint8_t)first, LOC_TABLEAU, (uint8_t)to };
                    if (count >= MAX_ACTIONS) { *overflow = true; return count; }
                    add_candidate(items, &count, g, m);
                }
            }
        }
    }
    return count;
}

bool solver_apply_move(Game *g, const SolverMove *m) {
    if (!g || !m) return false;
    if (m->type == SOLVER_MOVE_DRAW) return game_draw(g);
    if (m->type != SOLVER_MOVE_CARD || m->from_kind > LOC_TABLEAU ||
        m->to_kind > LOC_TABLEAU || m->from_kind == LOC_STOCK ||
        m->to_kind == LOC_STOCK || m->to_kind == LOC_WASTE) return false;
    if ((m->from_kind == LOC_FOUNDATION && m->from_index >= 4) ||
        (m->from_kind == LOC_TABLEAU && m->from_index >= 7) ||
        (m->to_kind == LOC_FOUNDATION && m->to_index >= 4) ||
        (m->to_kind == LOC_TABLEAU && m->to_index >= 7)) return false;
    return game_move(g, (PileKind)m->from_kind, m->from_index, m->card_index,
                     (PileKind)m->to_kind, m->to_index);
}

static void finish_winnable(Solver *s, uint32_t winning_node) {
    size_t length = 0, i;
    uint32_t current = winning_node;
    uint32_t *reverse = NULL;
    StateKey *keys = NULL;
    SolverMove *moves = NULL;
    while (s->nodes[current].parent >= 0) {
        if (++length > MAX_SYNC_JOIN_STEPS) {
            s->status = SOLVER_UNKNOWN;
            return;
        }
        current = (uint32_t)s->nodes[current].parent;
    }
    reverse = (uint32_t *)malloc((length ? length : 1u) * sizeof(*reverse));
    keys = (StateKey *)malloc((length + 1u) * sizeof(*keys));
    moves = (SolverMove *)malloc((length ? length : 1u) * sizeof(*moves));
    if (!reverse || !keys || !moves) {
        free(reverse); free(keys); free(moves);
        s->status = SOLVER_UNKNOWN;
        return;
    }
    current = winning_node;
    for (i = 0; i < length; i++) {
        reverse[i] = current;
        current = (uint32_t)s->nodes[current].parent;
    }
    keys[0] = s->nodes[current].key;
    for (i = 0; i < length; i++) {
        uint32_t node = reverse[length - 1u - i];
        moves[i] = s->nodes[node].move;
        keys[i + 1u] = s->nodes[node].key;
    }
    s->solution = moves;
    s->solution_length = length;
    moves = NULL;
    (void)winning_path_cache(keys, s->solution, length);
    s->status = SOLVER_WINNABLE;
    free(reverse); free(keys); free(moves);
}

static bool finish_with_cached_suffix(Solver *s, uint32_t winning_node,
                                      const CacheEntry *entry) {
    size_t prefix_length = 0, tail_length, total, i;
    uint32_t current = winning_node;
    uint32_t *reverse;
    SolverMove *combined;
    Game start;
    if (!entry || !entry->bundle || entry->offset > entry->bundle->length) return false;
    tail_length = entry->bundle->length - entry->offset;
    if (tail_length > MAX_SYNC_JOIN_STEPS) return false;
    while (s->nodes[current].parent >= 0) {
        if (++prefix_length > MAX_SYNC_JOIN_STEPS) return false;
        current = (uint32_t)s->nodes[current].parent;
    }
    if (prefix_length > SIZE_MAX - tail_length) return false;
    total = prefix_length + tail_length;
    if (total > MAX_SYNC_JOIN_STEPS) return false;
    reverse = (uint32_t *)malloc((prefix_length ? prefix_length : 1u) * sizeof(*reverse));
    combined = (SolverMove *)malloc((total ? total : 1u) * sizeof(*combined));
    if (!reverse || !combined) { free(reverse); free(combined); return false; }
    current = winning_node;
    for (i = 0; i < prefix_length; i++) {
        reverse[i] = current;
        current = (uint32_t)s->nodes[current].parent;
    }
    unpack_game(&s->nodes[current].key, &start);
    for (i = 0; i < prefix_length; i++)
        combined[i] = s->nodes[reverse[prefix_length - 1u - i]].move;
    if (tail_length)
        memcpy(combined + prefix_length,
               entry->bundle->moves + entry->offset,
               tail_length * sizeof(*combined));
    free(reverse);
    if (!solver_verify_and_cache(&start, combined, total)) {
        free(combined);
        return false;
    }
    s->solution = combined;
    s->solution_length = total;
    s->status = SOLVER_WINNABLE;
    return true;
}

Solver *solver_create_limited(const Game *snapshot, size_t max_states) {
    Solver *s;
    StateKey root;
    CacheEntry *known;
    size_t table_capacity;
    if (!snapshot || !pack_game(snapshot, &root)) return NULL;
    s = (Solver *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->status = SOLVER_CHECKING;
    s->max_states = max_states ? max_states : DEFAULT_MAX_STATES;
    if (s->max_states > INT32_MAX || s->max_states == 0) {
        s->status = SOLVER_UNKNOWN;
        return s;
    }
    known = cache_find(&root);
    if (known) {
        s->status = SOLVER_WINNABLE;
        s->cached_bundle = known->bundle;
        s->cached_offset = known->offset;
        if (s->cached_bundle) s->cached_bundle->refs++;
        return s;
    }
    if (is_won(snapshot)) {
        StateKey *keys = (StateKey *)malloc(sizeof(*keys));
        if (keys) {
            *keys = root;
            (void)winning_path_cache(keys, NULL, 0);
            free(keys);
        }
        s->status = SOLVER_WINNABLE;
        known = cache_find(&root);
        if (known) {
            s->cached_bundle = known->bundle;
            s->cached_offset = known->offset;
            if (s->cached_bundle) s->cached_bundle->refs++;
        }
        return s;
    }
    table_capacity = table_size_for(s->max_states);
    if (!table_capacity || s->max_states > SIZE_MAX / sizeof(*s->nodes) ||
        table_capacity > SIZE_MAX / sizeof(*s->visited) ||
        s->max_states > SIZE_MAX / sizeof(*s->frontier)) {
        s->status = SOLVER_UNKNOWN;
        return s;
    }
    s->nodes = (SearchNode *)malloc(s->max_states * sizeof(*s->nodes));
    s->visited = (uint32_t *)calloc(table_capacity, sizeof(*s->visited));
    s->frontier = (uint32_t *)malloc(s->max_states * sizeof(*s->frontier));
    if (!s->nodes || !s->visited || !s->frontier) {
        free(s->nodes); free(s->visited); free(s->frontier);
        s->nodes = NULL; s->visited = NULL; s->frontier = NULL;
        s->status = SOLVER_UNKNOWN;
        return s;
    }
    s->table_capacity = table_capacity;
    s->nodes[0].key = root;
    s->nodes[0].parent = -1;
    memset(&s->nodes[0].move, 0, sizeof(s->nodes[0].move));
    s->node_count = 1;
    s->frontier[0] = 0;
    s->frontier_count = 1;
    (void)visited_insert(s, &root, 0);
    return s;
}

Solver *solver_create(const Game *snapshot) {
    return solver_create_limited(snapshot, DEFAULT_MAX_STATES);
}

void solver_cancel(Solver *s) {
    if (s && s->status == SOLVER_CHECKING) {
        s->cancelled = true;
        s->status = SOLVER_UNKNOWN;
    }
}

void solver_destroy(Solver *s) {
    if (!s) return;
    free(s->nodes); free(s->visited); free(s->frontier); free(s->solution);
    bundle_release(s->cached_bundle);
    free(s);
}

SolverStatus solver_step(Solver *s, unsigned expansion_budget) {
    unsigned spent = 0;
    if (!s) return SOLVER_UNKNOWN;
    if (s->status != SOLVER_CHECKING) return s->status;
    if (s->cancelled || !s->nodes || !s->frontier || !s->visited) {
        s->status = SOLVER_UNKNOWN;
        return s->status;
    }
    while (spent < expansion_budget && s->status == SOLVER_CHECKING) {
        uint32_t node_id;
        Game state;
        Candidate actions[MAX_ACTIONS];
        bool overflow;
        size_t action_count, ai;
        if (s->frontier_count == 0) {
            s->status = SOLVER_UNWINNABLE;
            break;
        }
        node_id = s->frontier[--s->frontier_count];
        unpack_game(&s->nodes[node_id].key, &state);
        s->expanded++; spent++;
        if (is_won(&state)) {
            finish_winnable(s, node_id);
            break;
        }
        action_count = enumerate_moves(&state, actions, &overflow);
        if (overflow) {
            s->status = SOLVER_UNKNOWN;
            break;
        }
        for (ai = 0; ai < action_count; ai++) {
            Game next = state;
            StateKey key;
            uint32_t child;
            CacheEntry *cached_tail;
            if (!solver_apply_move(&next, &actions[ai].move) || !pack_game(&next, &key)) {
                s->status = SOLVER_UNKNOWN;
                break;
            }
            if (visited_contains(s, &key)) continue;
            if (s->node_count >= s->max_states || s->frontier_count >= s->max_states) {
                s->status = SOLVER_UNKNOWN;
                break;
            }
            child = (uint32_t)s->node_count;
            s->nodes[child].key = key;
            s->nodes[child].parent = (int32_t)node_id;
            s->nodes[child].move = actions[ai].move;
            if (!visited_insert(s, &key, child)) continue;
            s->node_count++;
            if (is_won(&next)) {
                finish_winnable(s, child);
                break;
            }
            cached_tail = cache_find(&key);
            if (cached_tail && finish_with_cached_suffix(s, child, cached_tail)) break;
            s->frontier[s->frontier_count++] = child;
        }
    }
    return s->status;
}

SolverStatus solver_status(const Solver *s) {
    return s ? s->status : SOLVER_UNKNOWN;
}

size_t solver_solution_length(const Solver *s) {
    if (!s || s->status != SOLVER_WINNABLE) return 0;
    if (s->solution) return s->solution_length;
    if (s->cached_bundle && s->cached_offset <= s->cached_bundle->length)
        return s->cached_bundle->length - s->cached_offset;
    return 0;
}

bool solver_solution_move(const Solver *s, size_t index, SolverMove *out) {
    if (!s || !out || s->status != SOLVER_WINNABLE) return false;
    if (s->solution) {
        if (index >= s->solution_length) return false;
        *out = s->solution[index];
        return true;
    }
    if (!s->cached_bundle || index >= s->cached_bundle->length - s->cached_offset)
        return false;
    *out = s->cached_bundle->moves[s->cached_offset + index];
    return true;
}

size_t solver_state_count(const Solver *s) {
    return s ? s->node_count : 0;
}

bool solver_verify_and_cache(const Game *start, const SolverMove *moves,
                             size_t move_count) {
    Game game;
    StateKey *keys;
    size_t i;
    if (!start || (move_count && !moves) || !pack_game(start, &(StateKey){{0}})) return false;
    if (move_count > (SIZE_MAX / sizeof(*keys)) - 1u) return false;
    keys = (StateKey *)malloc((move_count + 1u) * sizeof(*keys));
    if (!keys) return false;
    game = *start;
    (void)pack_game(&game, &keys[0]);
    for (i = 0; i < move_count; i++) {
        if (!solver_apply_move(&game, &moves[i]) || !pack_game(&game, &keys[i + 1u])) {
            free(keys); return false;
        }
    }
    if (!is_won(&game)) { free(keys); return false; }
    (void)winning_path_cache(keys, moves, move_count);
    free(keys);
    return true;
}

bool solver_same_position(const Game *a, const Game *b) {
    StateKey ka, kb;
    return pack_game(a, &ka) && pack_game(b, &kb) &&
           memcmp(ka.bytes, kb.bytes, KEY_SIZE) == 0;
}
