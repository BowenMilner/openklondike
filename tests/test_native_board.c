#include "../ios/board_layout.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void fits(OKBoardLayout b, double width, double height,
                 const int down[7], const int up[7]) {
    assert(fabs(b.card_height / b.card_width - 1.4) < 1e-6);
    assert(fabs(b.top_height / b.top_width - 1.4) < 1e-6);
    for (int c = 0; c < 7; c++) {
        assert(b.column_x[c] >= 0);
        assert(b.column_x[c] + b.card_width <= width + 1e-6);
        double bottom = b.tableau_y + b.card_height +
                        down[c] * b.down_spacing + up[c] * b.up_spacing;
        assert(bottom <= height + 1e-6);
    }
}

int main(void) {
    // Regression profile: actual visible pile lengths from the user's report,
    // with no rank/suit identities, and a compact iPhone landscape board.
    const int down[7] = {0, 1, 2, 3, 4, 5, 6};
    const int up[7] = {2, 1, 0, 2, 0, 2, 1};
    OKBoardLayout b = ok_board_layout(714, 226, 1, down, up);
    fits(b, 714, 226, down, up);
    assert(b.top_height < b.card_height * 0.75);
    assert(b.up_spacing >= b.card_height * 0.25 - 1e-6);
    assert(b.up_spacing > 18); // readable rank strip, previously near 4.6 pt
    printf("PASS: native_board_landscape_reported_stack_visibility\n");

    const int deep_down[7] = {0, 0, 0, 0, 0, 0, 6};
    const int deep_up[7] = {0, 0, 0, 0, 0, 0, 12};
    b = ok_board_layout(714, 226, 1, deep_down, deep_up);
    fits(b, 714, 226, deep_down, deep_up);
    assert(b.up_spacing >= b.card_height * 0.25 - 1e-6);
    assert(b.card_height > 42);
    printf("PASS: native_board_deep_king_to_ace_run_fits\n");

    b = ok_board_layout(396, 650, 0, down, up);
    fits(b, 396, 650, down, up);
    assert(b.top_width == b.card_width && b.top_height == b.card_height);
    assert(fabs(b.up_spacing - b.card_height * 0.29) < 1e-6);
    printf("PASS: native_board_portrait_composition_preserved\n");

    // Different columns own the deepest backs and longest exposed run. They
    // must fit independently rather than sharing an imaginary combined stack.
    const int separate_down[7] = {0, 0, 0, 0, 0, 0, 6};
    const int separate_up[7] = {7, 0, 0, 0, 0, 0, 0};
    b = ok_board_layout(896, 254, 1, separate_down, separate_up);
    fits(b, 896, 254, separate_down, separate_up);
    assert(b.card_height > 69);
    assert(b.up_spacing >= b.card_height * 0.25 - 1e-6);
    printf("PASS: native_board_independent_column_fan_budgets\n");
    return 0;
}
