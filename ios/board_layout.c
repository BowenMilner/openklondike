#include "board_layout.h"
#include <math.h>

OKBoardLayout ok_board_layout(double width, double height, int landscape,
                              const int down[7], const int up[7]) {
    OKBoardLayout result = {0};
    if (width <= 0 || height <= 0) return result;
    result.column_gap = 4.5;
    double max_width = fmax(0, width - 14 - result.column_gap * 6) / 7;
    double gap = landscape ? 8 : 12;
    double top_scale = landscape ? 0.72 : 1.0;
    double card_height = fmin(max_width * 1.4, fmax(42, (height - 12) / 2.35));
    if (landscape) {
        // Reserve a quarter-card strip for every exposed rank. Compress backs
        // first; very deep runs reduce card size rather than hiding the ranks.
        for (int c = 0; c < 7; c++) {
            double limit = (height - gap - 2 - down[c] * 2.5) /
                           (1 + top_scale + up[c] * 0.25);
            card_height = fmin(card_height, fmax(1, limit));
        }
    }
    result.card_height = card_height;
    result.card_width = card_height / 1.4;
    result.top_height = card_height * top_scale;
    result.top_width = result.top_height / 1.4;
    result.top_inset = (result.card_width - result.top_width) / 2;
    double left = (width - result.card_width * 7 - result.column_gap * 6) / 2;
    for (int c = 0; c < 7; c++)
        result.column_x[c] = left + c * (result.card_width + result.column_gap);
    result.tableau_y = result.top_height + gap;
    double room = fmax(0, height - result.tableau_y - 2 - card_height);
    double natural_down = card_height * 0.19, natural_up = card_height * 0.29;
    if (landscape) {
        double back = natural_down;
        for (int c = 0; c < 7; c++)
            if (down[c]) back = fmin(back, (room - up[c] * natural_up) / down[c]);
        result.down_spacing = fmax(2.5, back);
        result.up_spacing = natural_up;
        for (int c = 0; c < 7; c++)
            if (up[c]) result.up_spacing = fmin(result.up_spacing,
                (room - down[c] * result.down_spacing) / up[c]);
    } else {
        // Keep the portrait composition and spacing unchanged.
        int max_down = 0, max_up = 0;
        for (int c = 0; c < 7; c++) {
            if (down[c] > max_down) max_down = down[c];
            if (up[c] > max_up) max_up = up[c];
        }
        double need = max_down * natural_down + max_up * natural_up;
        double scale = need > room && need > 0 ? room / need : 1;
        result.down_spacing = max_down ? fmax(3.2, natural_down * scale) : natural_down;
        result.up_spacing = max_up ? fmax(4.6, natural_up * scale) : natural_up;
    }
    return result;
}
