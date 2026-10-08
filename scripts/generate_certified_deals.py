#!/usr/bin/env python3
"""Generate the checked-in, replay-verified opening deal bank."""

import itertools
import json
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "scripts/data/draw-one-certificates.json"
OUTPUT = ROOT / "src/certified_deals_data.h"
SUIT_MAP = (0, 1, 3, 2)  # prior certificate order: clubs, diamonds, spades, hearts
DRAW, FOUNDATION, TABLEAU = 0, 1, 2
WASTE, FOUNDATION_PILE, TABLEAU_PILE = 1, 2, 3


def card_id(rank, suit):
    return suit * 13 + rank - 1


def rank(card):
    return card % 13 + 1


def suit(card):
    return card // 13


def shuffled_deal(seed):
    t = seed & 0xFFFFFFFF

    def rng():
        nonlocal t
        t = (t + 0x6D2B79F5) & 0xFFFFFFFF
        n = t
        n = ((n ^ (n >> 15)) * (n | 1)) & 0xFFFFFFFF
        product = ((n ^ (n >> 7)) * (n | 61)) & 0xFFFFFFFF
        n = (n ^ ((n + product) & 0xFFFFFFFF)) & 0xFFFFFFFF
        return ((n ^ (n >> 14)) & 0xFFFFFFFF) / 4294967296

    deck = list(range(52))
    for i in range(51, 0, -1):
        j = int(rng() * (i + 1))
        deck[i], deck[j] = deck[j], deck[i]
    columns = [[] for _ in range(7)]
    for i in range(7):
        for col in range(i, 7):
            js_card = deck.pop()
            columns[col].append(card_id(js_card % 13 + 1, SUIT_MAP[js_card // 13]))
    stock = [card_id(js_card % 13 + 1, SUIT_MAP[js_card // 13]) for js_card in deck]
    return columns, stock


class Position:
    def __init__(self, columns, stock, draw_mode):
        self.columns = [[(c, i == len(p) - 1) for i, c in enumerate(p)] for p in columns]
        self.stock = [(c, False) for c in stock]
        self.waste = []
        self.foundations = [[] for _ in range(4)]
        self.draw_mode = draw_mode
        self.steps = []

    def target_foundation(self, card):
        r, s = rank(card), suit(card)
        for i, pile in enumerate(self.foundations):
            if not pile:
                if r == 1:
                    return i
            elif suit(pile[-1][0]) == s and rank(pile[-1][0]) + 1 == r:
                return i
        return None

    def move_top_to_foundation(self, kind, col):
        pile = self.waste if kind == WASTE else self.columns[col]
        if not pile:
            return False
        card, up = pile[-1]
        if not up:
            return False
        dest = self.target_foundation(card)
        if dest is None:
            return False
        if kind == WASTE:
            source_col = 0
            index = len(pile) - 1
        else:
            source_col = col
            index = len(pile) - 1
        self.steps.append((1, kind, source_col, index, FOUNDATION_PILE, dest))
        self.foundations[dest].append(pile.pop())
        if kind == TABLEAU_PILE and pile and not pile[-1][1]:
            pile[-1] = (pile[-1][0], True)
        return True

    def move_tableau_top_to_foundation(self, col, expected_card=None):
        if not self.columns[col]:
            return False
        if expected_card is not None and self.columns[col][-1][0] != expected_card:
            return False
        return self.move_top_to_foundation(TABLEAU_PILE, col)

    def draw(self):
        self.steps.append((0, 0, 0, 0, 0, 0))
        if not self.stock:
            if not self.waste:
                raise ValueError("attempted an empty stock action")
            while self.waste:
                card, _ = self.waste.pop()
                self.stock.append((card, False))
            return
        want = 3 if self.draw_mode == 1 else 1
        for _ in range(want):
            if not self.stock:
                break
            card, _ = self.stock.pop()
            self.waste.append((card, True))

    def old_move(self, move):
        kind = move["type"]
        if kind in ("draw", "recycle"):
            self.draw()
            return
        if move["from"] == "waste":
            source_kind, source_col = WASTE, 0
            source = self.waste
            index = len(source) - 1
        elif move["from"] == "foundation":
            source_kind = FOUNDATION_PILE
            source_col = SUIT_MAP[move["col"]]
            source = self.foundations[source_col]
            index = len(source) - 1
        else:
            source_kind = TABLEAU_PILE
            source_col = move["col"]
            source = self.columns[source_col]
            index = len(source) - 1 if kind == "foundation" else move["index"]
        if index < 0 or index >= len(source):
            raise ValueError("certificate source index is invalid")
        dest_kind = FOUNDATION_PILE if kind == "foundation" else TABLEAU_PILE
        dest_col = SUIT_MAP[move["to"]] if kind == "foundation" else move["to"]
        self.steps.append((1, source_kind, source_col, index, dest_kind, dest_col))
        moving = source[index:]
        if source_kind in (WASTE, FOUNDATION_PILE):
            if index != len(source) - 1:
                raise ValueError("single-card source was not its top")
            source.pop()
        else:
            source[index:] = []
            if source and not source[-1][1]:
                source[-1] = (source[-1][0], True)
        if dest_kind == FOUNDATION_PILE:
            if len(moving) != 1:
                raise ValueError("a run cannot go to a foundation")
            self.foundations[dest_col].append(moving[0])
        else:
            self.columns[dest_col].extend(moving)

    def verify_won(self):
        return all(len(p) == 13 for p in self.foundations)


def draw_one_record(record):
    columns, stock = shuffled_deal(record["seed"])
    position = Position(columns, stock, 0)
    for move in record["path"]:
        position.old_move(move)
    if not position.verify_won():
        raise ValueError(f"DRAW_ONE certificate for seed {record['seed']} did not win")
    cards = [c for col in columns for c in col] + stock
    if len(cards) != 52:
        raise ValueError("invalid opening card count")
    return cards, position.steps


def draw_three_records(count=24):
    rng = random.Random(20261008)
    out = []
    seen = set()
    for ace_order in itertools.permutations(range(4)):
        if len(out) >= count:
            break
        # Order each rank so every straddling three-card deal has its required
        # lower-rank cards exposed first.
        rank2 = list(ace_order)
        allowed = rank2[:3]
        rank3 = rng.sample(allowed, 2)
        rank3 += rng.sample([s for s in range(4) if s not in rank3], 2)
        rank4_first = rng.choice(rank3[:2])
        rank4 = [rank4_first] + rng.sample([s for s in range(4) if s != rank4_first], 3)
        rank5 = rng.sample(range(4), 4)
        rank6 = rng.sample(rank5[:3], 2)
        rank6 += rng.sample([s for s in range(4) if s not in rank6], 2)
        rank7_first = rng.choice(rank6[:2])
        rank7 = [rank7_first] + rng.sample([s for s in range(4) if s != rank7_first], 3)
        stock_draw_order = [card_id(r, s) for r, row in zip(range(2, 8),
                            (rank2, rank3, rank4, rank5, rank6, rank7)) for s in row]

        hidden_high = [card_id(8, ace_order[3])]
        for r in range(9, 14):
            hidden_high.extend(card_id(r, s) for s in rng.sample(range(4), 4))
        columns = []
        cursor = 0
        for col in range(7):
            length = col + 1
            if col < 4:
                top = card_id(1, ace_order[col])
            else:
                top = card_id(8, ace_order[col - 4])
            hidden_count = length - 1
            top_first = hidden_high[cursor:cursor + hidden_count]
            cursor += hidden_count
            columns.append(list(reversed(top_first)) + [top])
        if cursor != len(hidden_high):
            raise ValueError("draw-three tableau construction mismatch")
        stock = list(reversed(stock_draw_order))
        key = tuple(c for col in columns for c in col) + tuple(stock)
        if key in seen:
            continue
        seen.add(key)

        position = Position(columns, stock, 1)
        for col in range(4):
            if not position.move_tableau_top_to_foundation(col):
                raise ValueError("could not open draw-three Ace")
        for _ in range(8):
            position.draw()
            # Every accessible stock card is placed before the next stock draw.
            while position.waste and position.move_top_to_foundation(WASTE, 0):
                pass
        if position.stock or position.waste:
            raise ValueError("draw-three stock proof failed to clear the stock")
        for r in range(8, 14):
            left = 4
            while left:
                found = False
                for col in range(7):
                    if position.columns[col] and rank(position.columns[col][-1][0]) == r:
                        if position.move_tableau_top_to_foundation(col):
                            left -= 1
                            found = True
                            break
                if not found:
                    remaining = sum(len(p) for p in position.foundations)
                    visible = sum(bool(col and col[-1][1]) for col in position.columns)
                    raise ValueError(f"draw-three tableau proof stalled (foundation total {remaining}, visible columns {visible})")
        if not position.verify_won():
            raise ValueError("draw-three certificate did not win")
        cards = [c for col in columns for c in col] + stock
        if len(cards) != 52:
            raise ValueError("invalid draw-three opening card count")
        out.append((cards, position.steps))
    if len(out) != count:
        raise ValueError(f"expected {count} unique draw-three deals, got {len(out)}")
    return out


def c_move(move):
    typ, from_kind, from_index, card_index, to_kind, to_index = move
    if typ == DRAW:
        return "{ SOLVER_MOVE_DRAW, LOC_STOCK, 0, 0, LOC_STOCK, 0 }"
    return (f"{{ SOLVER_MOVE_CARD, {from_kind}, {from_index}, {card_index}, "
            f"{to_kind}, {to_index} }}")


def emit_bank(name, records):
    lines = []
    for i, (_, steps) in enumerate(records):
        lines.append(f"static const SolverMove {name}_steps_{i}[] = {{")
        lines.extend(f"    {c_move(m)}," for m in steps)
        lines.append("};")
    lines.append(f"static const CertifiedRecord {name}_bank[] = {{")
    for i, (cards, steps) in enumerate(records):
        contents = ", ".join(str(c) for c in cards)
        lines.append(f"    {{ {{ {contents} }}, {name}_steps_{i}, {len(steps)} }},")
    lines.append("};")
    return lines


def main():
    source = json.loads(SOURCE.read_text())
    draw_one = [draw_one_record(record) for record in source]
    draw_three = draw_three_records()
    text = [
        "/* Generated by scripts/generate_certified_deals.py; certificates are replayed at runtime. */",
        "#ifndef OPENKLONDIKE_CERTIFIED_DEALS_DATA_H",
        "#define OPENKLONDIKE_CERTIFIED_DEALS_DATA_H",
        "#include \"solver.h\"",
        "typedef struct { uint8_t cards[52]; const SolverMove *moves; uint16_t move_count; } CertifiedRecord;",
    ]
    text += emit_bank("draw_one", draw_one)
    text += emit_bank("draw_three", draw_three)
    text += ["#endif", ""]
    OUTPUT.write_text("\n".join(text))
    print(f"wrote {OUTPUT.relative_to(ROOT)}: {len(draw_one)} draw-one, {len(draw_three)} draw-three")


if __name__ == "__main__":
    main()
